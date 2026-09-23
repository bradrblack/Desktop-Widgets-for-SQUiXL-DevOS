#include "ui/widgets/widget_stock_list.h"

#include "fonts/ubuntu_mono_bold_18pt_aa.h"
#include "ui/theme_dashboard.h"

using json = nlohmann::json;

namespace
{
	std::string url_encode_symbol(const std::string &symbol)
	{
		std::string out;
		out.reserve(symbol.size() + 4);
		for (char c : symbol)
		{
			if (c == '^')
				out += "%5E";
			else if (c == '=')
				out += "%3D";
			else
				out += c;
		}
		return out;
	}

	// Refresh cadence for the whole batch. A single request now covers every
	// symbol, so this can be fairly frequent while still being polite to an
	// unauthenticated public endpoint.
	constexpr unsigned long REFRESH_INTERVAL_MS = 300000; // 5 min
	// Until the first fetch ever succeeds, retry much sooner than the normal
	// interval - otherwise one transient failure (e.g. a WiFi status blip
	// right at boot) leaves the card stuck on "NO INTERNET" for a full 5
	// minutes instead of seconds.
	constexpr unsigned long RETRY_INTERVAL_MS = 15000; // 15 sec
}

void widgetStockList::init_symbols()
{
	struct SymbolRef
	{
			const String &label;
			const String &ticker;
	};

	// Configurable via the web portal's Markets Settings (SettingsOption
	// fields declared in settings_async.h) - tickers must be in Yahoo
	// Finance's own symbol format since that's the API this card queries.
	const SymbolRef defs[] = {
		{settings.config.stocks.label1, settings.config.stocks.ticker1},
		{settings.config.stocks.label2, settings.config.stocks.ticker2},
		{settings.config.stocks.label3, settings.config.stocks.ticker3},
		{settings.config.stocks.label4, settings.config.stocks.ticker4},
		{settings.config.stocks.label5, settings.config.stocks.ticker5},
		{settings.config.stocks.label6, settings.config.stocks.ticker6},
	};

	std::string joined_symbols;
	for (const SymbolRef &def : defs)
	{
		if (def.ticker.length() < 1)
			continue;

		StockQuote q;
		q.label = def.label.c_str();
		q.ticker = def.ticker.c_str();
		quotes.push_back(q);

		if (!joined_symbols.empty())
			joined_symbols += ",";
		joined_symbols += url_encode_symbol(def.ticker.c_str());
	}

	// The "spark" endpoint returns every symbol in one response, so this is a
	// single request instead of one per symbol. interval/range=1d keeps each
	// symbol down to its latest quote rather than a full day of 1-minute bars
	// - for a 24hr market like CAD/US that full history can run to 80KB+,
	// which silently failed to parse on-device.
	psram_string url_prefix = "https://query1.finance.yahoo.com/v7/finance/spark?symbols=";
	batch_url = url_prefix + joined_symbols.c_str() + "&range=1d&interval=1d";

	squixl.get_cached_char_sizes(FONT_SPEC::FONT_WEIGHT_B, 3, &row_char_w, &row_char_h);
}

void widgetStockList::maybe_fetch()
{
	if (quotes.empty())
		init_symbols();

	// Safety net: if a queued request's callback never comes back for any
	// reason (lost mid-reset, a stuck wifi_task, etc.), is_fetching would
	// otherwise stay true forever and silently block every future retry -
	// exactly what was happening when this card was permanently stuck on
	// "NO INTERNET" despite WiFi being fine.
	if (is_fetching && millis() - fetch_started_at > 30000)
	{
		Serial.println("StockList: fetch appears stuck, resetting is_fetching");
		is_fetching = false;
	}

	unsigned long interval = has_data ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS;

	if (!is_fetching && !quotes.empty() && (next_update == 0 || millis() - next_update > interval))
	{
		if (settings.has_wifi_creds() && !wifi_controller.wifi_blocking_access)
		{
			is_fetching = true;
			fetch_started_at = millis();
			wifi_controller.add_to_queue((std::string)batch_url, [this](bool success, const String &response) { this->process_quote_data(success, response); });
		}
		next_update = millis();
	}
}

void widgetStockList::prefetch()
{
	maybe_fetch();
}

void widgetStockList::reload_symbols()
{
	quotes.clear();
	batch_url.clear();
	next_update = 0;
	has_data = false;
	is_fetching = false;
	should_redraw = true;
}

void widgetStockList::process_quote_data(bool success, const String &response)
{
	bool ok = true;
	int updated = 0;
	try
	{
		if (response == "ERROR")
		{
			ok = false;
		}
		else
		{
			json data = json::parse(response);

			if (data.contains("spark") && data["spark"].contains("result") && data["spark"]["result"].is_array())
			{
				for (auto &entry : data["spark"]["result"])
				{
					std::string sym = entry.value("symbol", "");
					if (sym.empty() || !entry.contains("response") || !entry["response"].is_array() || entry["response"].empty())
						continue;

					json meta = entry["response"][0]["meta"];

					float price = (float)(meta.value("regularMarketPrice", 0.0));
					if (price == 0.0f)
						continue;

					float change_pct;
					if (meta.contains("regularMarketChangePercent") && meta["regularMarketChangePercent"].is_number())
					{
						change_pct = (float)(meta.value("regularMarketChangePercent", 0.0));
					}
					else
					{
						float prev_close = (float)(meta.value("previousClose", meta.value("chartPreviousClose", 0.0)));
						if (prev_close == 0.0f)
							continue;
						change_pct = ((price - prev_close) / prev_close) * 100.0f;
					}

					for (StockQuote &q : quotes)
					{
						if (q.ticker == sym)
						{
							q.price = price;
							q.change_pct = change_pct;
							q.has_data = true;
							updated++;
							break;
						}
					}
				}
			}
			else
			{
				ok = false;
			}
		}
	}
	catch (json::exception &e)
	{
		Serial.printf("response: %s\n", response.c_str());
		Serial.println("StockList Json parse error:");
		Serial.println(e.what());
		ok = false;
	}

	Serial.printf("StockList: process_quote_data callback ran - ok=%d updated=%d/%d has_data_before=%d response_len=%d\n", ok, updated, (int)quotes.size(), has_data, response.length());

	if (updated > 0)
		has_data = true;

	should_redraw = true;
	is_fetching = false;
	next_update = millis();

	// This is required - this is responsible for determining the lifetime of the response String
	// to ensure it survives until after it's been used.
	delete &response;
}

bool widgetStockList::redraw(uint8_t fade_amount, int8_t tab_group)
{
	static unsigned long last_call = 0;
	unsigned long now_ts = millis();
	if (last_call != 0 && now_ts - last_call > 3000)
	{
		String msg = "stocklist redraw gap=" + String(now_ts - last_call) + "ms dirty=" + String(is_dirty) + " dirty_hard=" + String(is_dirty_hard) + " busy=" + String(is_busy) + " parent_content_buf=" + String((bool)(ui_parent && ui_parent->_sprite_content.getBuffer()));
		Serial.println(msg);
	}
	last_call = now_ts;

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
		Serial.printf("StockList: REPAINTING at %lums - has_data=%d quotes[0].has_data=%d is_dirty_hard=%d\n", now_ts, has_data, quotes.empty() ? -1 : quotes[0].has_data, is_dirty_hard);

		ui_parent->_sprite_back.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_back.getBuffer());
		delay(10);

		// Antialiased text only - see UbuntuMono_Bold18pt7bAA's own header
		// comment for why this can't mix with a 1-bit font on this canvas.
		_sprite_back.setAntialias(true);

		_sprite_clean.fillRect(0, 0, _w, _h, TFT_MAGENTA);
		_sprite_clean.fillRoundRect(0, 0, _w, _h, 14, dashboard_theme::card);
		squixl.lcd.blendSprite(&_sprite_clean, &_sprite_back, &_sprite_back, _t, TFT_MAGENTA);

		// Header band: a distinct-color strip across the top of the card,
		// rounded to match the card's top corners and squared off at its
		// own bottom edge so it reads as a header, not a floating pill.
		constexpr int16_t HEADER_H = 56;
		constexpr int16_t CORNER_R = 14;

		_sprite_back.fillRoundRect(0, 0, _w, HEADER_H, CORNER_R, dashboard_theme::header_bg);
		_sprite_back.fillRect(0, HEADER_H - CORNER_R, _w, CORNER_R, dashboard_theme::header_bg);

		int hdr_w, hdr_h;
		_sprite_back.setFreeFont(&UbuntuMono_Bold18pt7bAA);
		calc_text_size("Markets", &UbuntuMono_Bold18pt7bAA, &hdr_w, &hdr_h);
		_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
		_sprite_back.setCursor((_w - hdr_w) / 2, HEADER_H / 2 + hdr_h / 2);
		_sprite_back.print("Markets");

		int16_t row_y0 = HEADER_H + 10;
		int16_t row_h = (quotes.empty()) ? 0 : (_h - row_y0 - padding.bottom) / (int16_t)quotes.size();

		int text_w, text_h;

		for (size_t i = 0; i < quotes.size(); i++)
		{
			const StockQuote &q = quotes[i];
			int16_t baseline_y = row_y0 + (int16_t)i * row_h + row_h / 2 + row_char_h / 2;

			_sprite_back.setFreeFont(&UbuntuMono_Bold18pt7bAA);

			_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
			_sprite_back.setCursor(padding.left, baseline_y);
			_sprite_back.print(q.label.c_str());

			if (!q.has_data)
			{
				_sprite_back.setTextColor(dashboard_theme::text_secondary, -1);
				const char *waiting = wifi_controller.is_connected() ? "..." : "NO INTERNET";
				calc_text_size(waiting, &UbuntuMono_Bold18pt7bAA, &text_w, &text_h);
				_sprite_back.setCursor(_w - padding.right - text_w, baseline_y);
				_sprite_back.print(waiting);
				continue;
			}

			char change_buf[12];
			snprintf(change_buf, sizeof(change_buf), "%+.1f%%", q.change_pct);
			calc_text_size(change_buf, &UbuntuMono_Bold18pt7bAA, &text_w, &text_h);
			int16_t change_x = _w - padding.right - text_w;

			char price_buf[16];
			snprintf(price_buf, sizeof(price_buf), "%.2f", q.price);
			calc_text_size(price_buf, &UbuntuMono_Bold18pt7bAA, &text_w, &text_h);
			int16_t price_x = change_x - 34 - text_w;

			_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
			_sprite_back.setCursor(price_x, baseline_y);
			_sprite_back.print(price_buf);

			_sprite_back.setTextColor(q.change_pct >= 0 ? dashboard_theme::positive : dashboard_theme::negative, -1);
			_sprite_back.setCursor(change_x, baseline_y);
			_sprite_back.print(change_buf);
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

	unsigned long dur = millis() - now_ts;
	if (dur > 200)
	{
		String msg = "stocklist redraw body took " + String(dur) + "ms";
		Serial.println(msg);
	}

	return (fade_amount < 32 || was_dirty);
}

bool widgetStockList::process_touch(touch_event_t touch_event)
{
	return false;
}
