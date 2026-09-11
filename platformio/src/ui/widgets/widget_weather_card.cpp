#include "ui/widgets/widget_weather_card.h"

#include "ui/theme_dashboard.h"

using json = nlohmann::json;

namespace
{
	constexpr unsigned long REFRESH_INTERVAL_MS = 900000; // 15 min - a forecast doesn't need to be hammered
	// Until the first fetch ever succeeds, retry much sooner than the normal
	// interval - otherwise one transient failure (e.g. a WiFi status blip
	// right at boot) leaves the card stuck on "NO INTERNET" for a full 15
	// minutes instead of seconds.
	constexpr unsigned long RETRY_INTERVAL_MS = 15000; // 15 sec

	// Sakamoto's version of Zeller's congruence - 0=Sunday..6=Saturday.
	int day_of_week(int y, int m, int d)
	{
		static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
		if (m < 3)
			y -= 1;
		return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
	}

	const char *WEEKDAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

	// Fixed real-world-temperature (Celsius) to color stops, interpolated
	// piecewise so the bar's color always means the same actual temperature
	// regardless of which day's range is being drawn.
	uint16_t temp_to_color(float c)
	{
		struct Stop
		{
				float temp;
				uint8_t r, g, b;
		};
		static const Stop stops[] = {
			{-10, 60, 90, 220},  // cold blue
			{0, 80, 190, 220},	  // cyan
			{10, 100, 200, 120}, // green
			{20, 230, 200, 70},  // yellow
			{30, 230, 110, 60},  // orange
		};
		constexpr int n = sizeof(stops) / sizeof(stops[0]);

		if (c <= stops[0].temp)
			return ((stops[0].r & 0xf8) << 8) | ((stops[0].g & 0xfc) << 3) | (stops[0].b >> 3);
		if (c >= stops[n - 1].temp)
			return ((stops[n - 1].r & 0xf8) << 8) | ((stops[n - 1].g & 0xfc) << 3) | (stops[n - 1].b >> 3);

		for (int i = 0; i < n - 1; i++)
		{
			if (c >= stops[i].temp && c <= stops[i + 1].temp)
			{
				float t = (c - stops[i].temp) / (stops[i + 1].temp - stops[i].temp);
				uint8_t r = (uint8_t)(stops[i].r + t * (stops[i + 1].r - stops[i].r));
				uint8_t g = (uint8_t)(stops[i].g + t * (stops[i + 1].g - stops[i].g));
				uint8_t b = (uint8_t)(stops[i].b + t * (stops[i + 1].b - stops[i].b));
				return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
			}
		}
		return TFT_WHITE;
	}

	// Streaming (SAX) parser for the forecast response, used instead of
	// json::parse()'s full DOM tree. The 5-day/3-hour response carries ~40
	// entries with a lot of fields we never read (wind, clouds, pressure,
	// visibility, sys...); building the whole tree just to pull out a
	// handful of values per entry is real, measurable time on this device -
	// large allocations land in PSRAM, which is much slower than internal
	// SRAM for the many small, scattered nodes a JSON DOM produces. This
	// walks the token stream once and only keeps what we need.
	class WeatherSax : public json::json_sax_t
	{
		public:
			explicit WeatherSax(std::vector<DayForecast> &out) : days(out) {}

			bool ok = true;

			bool null() override { return true; }
			bool boolean(bool) override { return true; }
			bool binary(json::binary_t &) override { return true; }

			bool number_integer(json::number_integer_t val) override { return number((double)val); }
			bool number_unsigned(json::number_unsigned_t val) override { return number((double)val); }
			bool number_float(json::number_float_t val, const json::string_t &) override { return number((double)val); }

			bool string(json::string_t &val) override
			{
				Ctx top = ctx_stack.empty() ? Ctx::ROOT : ctx_stack.back();
				if (top == Ctx::ENTRY_OBJ && pending_key == "dt_txt")
					cur_dt_txt = val;
				else if (top == Ctx::WEATHER_ITEM && pending_key == "icon")
					cur_icon = val;
				return true;
			}

			bool start_object(std::size_t) override
			{
				Ctx top = ctx_stack.empty() ? Ctx::ROOT : ctx_stack.back();
				Ctx next = Ctx::SKIP;

				if (ctx_stack.empty())
					next = Ctx::ROOT;
				else if (top == Ctx::LIST_ARRAY)
				{
					next = Ctx::ENTRY_OBJ;
					cur_dt_txt.clear();
					cur_temp_min = 0;
					cur_temp_max = 0;
					cur_pop = 0;
					cur_icon.clear();
				}
				else if (top == Ctx::ENTRY_OBJ && pending_key == "main")
					next = Ctx::MAIN_OBJ;
				else if (top == Ctx::WEATHER_ARRAY)
					next = Ctx::WEATHER_ITEM;

				ctx_stack.push_back(next);
				return true;
			}

			bool end_object() override
			{
				if (ctx_stack.empty())
					return true;
				Ctx popped = ctx_stack.back();
				ctx_stack.pop_back();
				if (popped == Ctx::ENTRY_OBJ)
					finish_entry();
				return true;
			}

			bool start_array(std::size_t) override
			{
				Ctx top = ctx_stack.empty() ? Ctx::ROOT : ctx_stack.back();
				Ctx next = Ctx::SKIP;

				if (top == Ctx::ROOT && pending_key == "list")
					next = Ctx::LIST_ARRAY;
				else if (top == Ctx::ENTRY_OBJ && pending_key == "weather")
					next = Ctx::WEATHER_ARRAY;

				ctx_stack.push_back(next);
				return true;
			}

			bool end_array() override
			{
				if (!ctx_stack.empty())
					ctx_stack.pop_back();
				return true;
			}

			bool key(json::string_t &val) override
			{
				pending_key = val;
				return true;
			}

			bool parse_error(std::size_t, const std::string &, const json::exception &ex) override
			{
				Serial.print("WeatherCard SAX parse error: ");
				Serial.println(ex.what());
				ok = false;
				return false;
			}

		private:
			enum class Ctx
			{
				ROOT,
				LIST_ARRAY,
				ENTRY_OBJ,
				MAIN_OBJ,
				WEATHER_ARRAY,
				WEATHER_ITEM,
				SKIP
			};

			std::vector<DayForecast> &days;
			std::vector<Ctx> ctx_stack;
			std::string pending_key;

			std::string cur_dt_txt;
			double cur_temp_min = 0;
			double cur_temp_max = 0;
			double cur_pop = 0;
			std::string cur_icon;
			std::string last_date;

			bool number(double val)
			{
				Ctx top = ctx_stack.empty() ? Ctx::ROOT : ctx_stack.back();
				if (top == Ctx::MAIN_OBJ)
				{
					if (pending_key == "temp_min")
						cur_temp_min = val;
					else if (pending_key == "temp_max")
						cur_temp_max = val;
				}
				else if (top == Ctx::ENTRY_OBJ && pending_key == "pop")
				{
					cur_pop = val;
				}
				return true;
			}

			void finish_entry()
			{
				if (cur_dt_txt.size() < 10)
					return;

				std::string date = cur_dt_txt.substr(0, 10); // "YYYY-MM-DD"
				std::string time_part = cur_dt_txt.size() >= 16 ? cur_dt_txt.substr(11, 5) : "";

				if (date != last_date)
				{
					DayForecast day;
					if (days.empty())
						day.label = "Today";
					else
					{
						int y = atoi(date.substr(0, 4).c_str());
						int m = atoi(date.substr(5, 2).c_str());
						int d = atoi(date.substr(8, 2).c_str());
						day.label = WEEKDAY_NAMES[day_of_week(y, m, d)];
					}
					days.push_back(day);
					last_date = date;
				}

				DayForecast &day = days.back();

				int16_t tmin = (int16_t)round(cur_temp_min);
				int16_t tmax = (int16_t)round(cur_temp_max);
				day.low = min(day.low, tmin);
				day.high = max(day.high, tmax);

				int pop_pct = (int)round(cur_pop * 100.0);
				day.precip_pct = max(day.precip_pct, pop_pct);

				if (!cur_icon.empty())
				{
					String icon = cur_icon.c_str();
					if (icon.substring(0, 2) != "01" && icon.substring(0, 2) != "02")
						icon = icon.substring(0, 2);
					if (day.icon_name.isEmpty() || time_part == "12:00")
						day.icon_name = icon;
				}

				day.has_data = true;
			}
	};
}

std::string widgetWeatherCard::build_url()
{
	if (settings.config.location.city == "" || settings.config.location.country == "" || !settings.config.open_weather.has_key())
		return "";

	String url = "http://api.openweathermap.org/data/2.5/forecast?q=" + settings.config.location.city + "," + settings.config.location.country +
				 "&APPID=" + settings.config.open_weather.api_key + "&units=" + (settings.config.open_weather.units_metric ? "metric" : "imperial") + "&cnt=40";
	return std::string(url.c_str());
}

void widgetWeatherCard::load_icon(const String &name)
{
	if (icons.count(name) > 0 && icons[name].getBuffer())
		return;

	Serial.printf("WeatherCard: loading icon '%s'\n", name.c_str());
	icons[name].create(64, 64);

	if (name == "01d")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_01d, sizeof(um_ow_01d));
	else if (name == "01n")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_01n, sizeof(um_ow_01n));
	else if (name == "02d")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_02d, sizeof(um_ow_02d));
	else if (name == "02n")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_02n, sizeof(um_ow_02n));
	else if (name == "03")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_03d, sizeof(um_ow_03d));
	else if (name == "04")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_04d, sizeof(um_ow_04d));
	else if (name == "09")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_09d, sizeof(um_ow_09d));
	else if (name == "10")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_10d, sizeof(um_ow_10d));
	else if (name == "11")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_11d, sizeof(um_ow_11d));
	else if (name == "13")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_13d, sizeof(um_ow_13d));
	else if (name == "50")
		squixl.loadPNG_into(&icons[name], 0, 0, um_ow_50d, sizeof(um_ow_50d));
}

void widgetWeatherCard::process_forecast_data(bool success, const String &response)
{
	bool ok = true;
	std::vector<DayForecast> new_days;

	if (response == "ERROR")
	{
		ok = false;
	}
	else
	{
		try
		{
			unsigned long parse_start = millis();

			WeatherSax handler(new_days);
			bool parsed = json::sax_parse(response, &handler);

			Serial.printf("WeatherCard: SAX parse took %lums (%d bytes)\n", millis() - parse_start, response.length());

			ok = parsed && handler.ok;
		}
		catch (json::exception &e)
		{
			Serial.println("WeatherCard SAX parse threw:");
			Serial.println(e.what());
			ok = false;
			new_days.clear();
		}
	}

	if (ok && !new_days.empty())
	{
		days = new_days;
		for (const DayForecast &d : days)
			if (!d.icon_name.isEmpty())
				load_icon(d.icon_name);
		has_data = true;
	}
	else
	{
		Serial.println("WeatherCard: failed to update forecast");
	}

	should_redraw = true;
	is_fetching = false;
	next_update = millis();

	// This is required - this is responsible for determining the lifetime of the response String
	// to ensure it survives until after it's been used.
	delete &response;
}

void widgetWeatherCard::maybe_fetch()
{
	// Safety net: if a queued request's callback never comes back for any
	// reason (lost mid-reset, a stuck wifi_task, etc.), is_fetching would
	// otherwise stay true forever and silently block every future retry.
	if (is_fetching && millis() - fetch_started_at > 30000)
	{
		Serial.println("WeatherCard: fetch appears stuck, resetting is_fetching");
		is_fetching = false;
	}

	unsigned long interval = has_data ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS;

	if (!is_fetching && (next_update == 0 || millis() - next_update > interval))
	{
		std::string url = build_url();
		if (!url.empty() && !wifi_controller.wifi_blocking_access)
		{
			is_fetching = true;
			fetch_started_at = millis();
			wifi_controller.add_to_queue(url, [this](bool success, const String &response) { this->process_forecast_data(success, response); });
			next_update = millis();
		}
		// If the URL couldn't be built yet (settings not loaded, no key/city set)
		// or WiFi is busy, don't stamp next_update - retry on the very next
		// redraw instead of waiting out the full 15-minute backoff for a
		// request that was never actually sent.
	}
}

void widgetWeatherCard::prefetch()
{
	maybe_fetch();
}

bool widgetWeatherCard::redraw(uint8_t fade_amount, int8_t tab_group)
{
	if (millis() < delay_first_draw)
		return false;

	maybe_fetch();

	bool was_dirty = false;

	if (should_redraw)
	{
		should_redraw = false;
		is_dirty = true;
	}

	if (is_dirty || is_dirty_hard)
	{
		ui_parent->_sprite_back.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_back.getBuffer());
		delay(10);

		_sprite_clean.fillRect(0, 0, _w, _h, TFT_MAGENTA);
		_sprite_clean.fillRoundRect(0, 0, _w, _h, 14, dashboard_theme::card);
		squixl.lcd.blendSprite(&_sprite_clean, &_sprite_back, &_sprite_back, _t, TFT_MAGENTA);

		constexpr int16_t HEADER_H = 56;
		constexpr int16_t CORNER_R = 14;

		_sprite_back.fillRoundRect(0, 0, _w, HEADER_H, CORNER_R, dashboard_theme::header_bg);
		_sprite_back.fillRect(0, HEADER_H - CORNER_R, _w, CORNER_R, dashboard_theme::header_bg);

		int hdr_w, hdr_h;
		_sprite_back.setFreeFont(UbuntuMono_B[3]);
		calc_text_size("Weather", UbuntuMono_B[3], &hdr_w, &hdr_h);
		_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
		_sprite_back.setCursor((_w - hdr_w) / 2, HEADER_H / 2 + hdr_h / 2);
		_sprite_back.print("Weather");

		if (!has_data || days.empty())
		{
			const char *msg = wifi_controller.is_connected() ? (settings.config.open_weather.has_key() ? "LOADING FORECAST..." : "NO API KEY SET") : "NO INTERNET";

			int msg_w, msg_h;
			_sprite_back.setFreeFont(UbuntuMono_B[2]);
			calc_text_size(msg, UbuntuMono_B[2], &msg_w, &msg_h);
			_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
			_sprite_back.setCursor((_w - msg_w) / 2, HEADER_H + (_h - HEADER_H) / 2);
			_sprite_back.print(msg);
		}
		else
		{
			// Global low/high across every day, so the bar color always means
			// the same absolute temperature no matter which row it's on.
			int16_t global_low = 999, global_high = -999;
			for (const DayForecast &d : days)
			{
				global_low = min(global_low, d.low);
				global_high = max(global_high, d.high);
			}
			if (global_high <= global_low)
				global_high = global_low + 1;

			int16_t row_y0 = HEADER_H + 6;
			int16_t row_h = (_h - row_y0 - padding.bottom) / (int16_t)days.size();

			constexpr int16_t ICON_SIZE = 36;
			constexpr int16_t BAR_H = 10;

			int text_w, text_h;

			// "Today" is the widest day label this list ever shows - measure it
			// once and place the icon column after it, instead of a fixed
			// offset that "Today" (5 chars) would overlap but "Thu" wouldn't.
			_sprite_back.setFreeFont(UbuntuMono_B[2]);
			calc_text_size("Today", UbuntuMono_B[2], &text_w, &text_h);
			int16_t ICON_X = padding.left + text_w + 16;
			int16_t LOW_X_END = ICON_X + ICON_SIZE + 90;
			int16_t BAR_X = LOW_X_END + 14;
			int16_t BAR_W = _w - padding.right - 60 - BAR_X;

			for (size_t i = 0; i < days.size(); i++)
			{
				const DayForecast &d = days[i];
				int16_t row_top = row_y0 + (int16_t)i * row_h;
				int16_t row_mid = row_top + row_h / 2;

				_sprite_back.setFreeFont(UbuntuMono_B[2]);
				_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
				_sprite_back.setCursor(padding.left, row_mid + 6);
				_sprite_back.print(d.label.c_str());

				if (!d.icon_name.isEmpty() && icons.count(d.icon_name) > 0)
					_sprite_back.drawSprite(ICON_X, row_mid - ICON_SIZE / 2, &icons[d.icon_name], (float)ICON_SIZE / 64.0f, 0x0);

				if (d.precip_pct >= 10)
				{
					char pop_buf[6];
					snprintf(pop_buf, sizeof(pop_buf), "%d%%", d.precip_pct);
					_sprite_back.setFreeFont(UbuntuMono_R[0]);
					_sprite_back.setTextColor(dashboard_theme::accent_teal, -1);
					_sprite_back.setCursor(ICON_X, row_mid + ICON_SIZE / 2 + 12);
					_sprite_back.print(pop_buf);
				}

				char low_buf[6];
				snprintf(low_buf, sizeof(low_buf), "%d\xB0", d.low);
				_sprite_back.setFreeFont(UbuntuMono_B[2]);
				_sprite_back.setTextColor(dashboard_theme::text_secondary, -1);
				calc_text_size(low_buf, UbuntuMono_B[2], &text_w, &text_h);
				_sprite_back.setCursor(LOW_X_END - text_w, row_mid + 6);
				_sprite_back.print(low_buf);

				// Track (full card range) + gradient-filled segment for this day's [low, high]
				_sprite_back.fillRoundRect(BAR_X, row_mid - BAR_H / 2, BAR_W, BAR_H, BAR_H / 2, dashboard_theme::card_track);

				int16_t seg_x0 = BAR_X + (int16_t)((float)(d.low - global_low) / (global_high - global_low) * BAR_W);
				int16_t seg_x1 = BAR_X + (int16_t)((float)(d.high - global_low) / (global_high - global_low) * BAR_W);
				if (seg_x1 - seg_x0 < BAR_H)
					seg_x1 = seg_x0 + BAR_H;

				for (int16_t x = seg_x0; x < seg_x1; x++)
				{
					float t = (float)(x - BAR_X) / (float)BAR_W;
					float temp_at_x = global_low + t * (global_high - global_low);
					int16_t y0 = row_mid - BAR_H / 2;
					_sprite_back.drawLine(x, y0, x, y0 + BAR_H - 1, temp_to_color(temp_at_x));
				}
				// Re-round just this segment's corners by touching up with the track color outside it - cheap approximation, close enough at this scale.

				char high_buf[6];
				snprintf(high_buf, sizeof(high_buf), "%d\xB0", d.high);
				_sprite_back.setFreeFont(UbuntuMono_B[2]);
				_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
				calc_text_size(high_buf, UbuntuMono_B[2], &text_w, &text_h);
				_sprite_back.setCursor(_w - padding.right - text_w, row_mid + 6);
				_sprite_back.print(high_buf);
			}
		}

		is_dirty_hard = false;
		is_dirty = true;
	}

	squixl.lcd.blendSprite(&_sprite_back, &_sprite_clean, &_sprite_mixed, constrain(fade_amount, 0, 32));
	ui_parent->_sprite_content.drawSprite(_x, _y, &_sprite_mixed, 1.0f, -1);
	next_refresh = millis();

	if (is_dirty && !was_dirty)
		was_dirty = true;

	is_dirty = false;
	is_busy = false;

	return (fade_amount < 32 || was_dirty);
}

bool widgetWeatherCard::process_touch(touch_event_t touch_event)
{
	return false;
}
