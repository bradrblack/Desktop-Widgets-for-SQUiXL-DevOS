#include "ui/widgets/widget_calendar.h"

#include "ui/theme_dashboard.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace
{
	// Calendars change far less often than stock prices - poll gently.
	constexpr unsigned long REFRESH_INTERVAL_MS = 900000; // 15 min
	// Until the first fetch ever succeeds, retry much sooner.
	constexpr unsigned long RETRY_INTERVAL_MS = 15000; // 15 sec
	constexpr size_t MAX_EVENTS = 4;

	std::string unescape_ics_text(const std::string &in)
	{
		std::string out;
		out.reserve(in.size());
		for (size_t i = 0; i < in.size(); i++)
		{
			if (in[i] == '\\' && i + 1 < in.size())
			{
				char next = in[i + 1];
				if (next == 'n' || next == 'N')
				{
					out += ' ';
					i++;
					continue;
				}
				if (next == ',' || next == ';' || next == '\\')
				{
					out += next;
					i++;
					continue;
				}
			}
			out += in[i];
		}
		return out;
	}

	// value is the raw DTSTART digits, e.g. "20250915" (VALUE=DATE) or
	// "20250915T090000" / "20250915T090000Z". No timezone conversion is
	// done - see widget_calendar.h for why that's an accepted simplification.
	bool fill_event_datetime(const std::string &value, bool is_date_only, AgendaEvent &ev)
	{
		if (value.length() < 8)
			return false;

		int year = atoi(value.substr(0, 4).c_str());
		int month = atoi(value.substr(4, 2).c_str());
		int day = atoi(value.substr(6, 2).c_str());

		int hour = 0, minute = 0;
		bool has_time = (!is_date_only && value.length() >= 13 && value[8] == 'T');
		if (has_time)
		{
			hour = atoi(value.substr(9, 2).c_str());
			minute = atoi(value.substr(11, 2).c_str());
		}

		struct tm t = {};
		t.tm_year = year - 1900;
		t.tm_mon = month - 1;
		t.tm_mday = day;
		t.tm_hour = hour;
		t.tm_min = minute;
		t.tm_isdst = -1;
		if (mktime(&t) == (time_t)-1)
			return false;

		char date_buf[16];
		strftime(date_buf, sizeof(date_buf), "%a %b %d", &t);
		ev.date_label = date_buf;

		if (has_time)
		{
			char time_buf[16];
			strftime(time_buf, sizeof(time_buf), "%I:%M %p", &t);
			std::string tl = time_buf;
			if (!tl.empty() && tl[0] == '0')
				tl.erase(0, 1);
			ev.time_label = tl;
		}
		else
		{
			ev.time_label = "All day";
		}

		ev.sort_key = (long)year * 100000000L + (long)month * 1000000L + (long)day * 10000L + (long)hour * 100L + (long)minute;

		return true;
	}
}

void widgetCalendar::maybe_fetch()
{
	ics_url = settings.config.calendar.ics_url.c_str();

	if (ics_url.empty())
		return;

	// Safety net matching widgetStockList's - if a queued request's callback
	// never comes back, is_fetching would otherwise block every future retry.
	if (is_fetching && millis() - fetch_started_at > 30000)
	{
		Serial.println("Calendar: fetch appears stuck, resetting is_fetching");
		is_fetching = false;
	}

	unsigned long interval = has_data ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS;

	if (!is_fetching && (next_update == 0 || millis() - next_update > interval))
	{
		if (settings.has_wifi_creds() && !wifi_controller.wifi_blocking_access)
		{
			is_fetching = true;
			fetch_started_at = millis();
			wifi_controller.add_to_queue((std::string)ics_url, [this](bool success, const String &response) { this->process_ics_data(success, response); });
		}
		next_update = millis();
	}
}

void widgetCalendar::prefetch()
{
	maybe_fetch();
}

void widgetCalendar::reload_events()
{
	events.clear();
	ics_url.clear();
	next_update = 0;
	has_data = false;
	is_fetching = false;
	should_redraw = true;
}

void widgetCalendar::parse_ics(const String &body)
{
	std::vector<AgendaEvent> parsed;

	bool in_event = false;
	std::string cur_summary;
	std::string cur_dtstart_value;
	bool cur_is_date_only = false;
	bool have_dtstart = false;

	std::string logical_line;

	auto flush_line = [&]() {
		if (logical_line.empty())
			return;

		if (logical_line.back() == '\r')
			logical_line.pop_back();

		if (logical_line == "BEGIN:VEVENT")
		{
			in_event = true;
			cur_summary.clear();
			cur_dtstart_value.clear();
			cur_is_date_only = false;
			have_dtstart = false;
		}
		else if (logical_line == "END:VEVENT")
		{
			if (in_event && have_dtstart)
			{
				AgendaEvent ev;
				ev.summary = cur_summary.empty() ? "(untitled)" : cur_summary;
				if (fill_event_datetime(cur_dtstart_value, cur_is_date_only, ev))
					parsed.push_back(ev);
			}
			in_event = false;
		}
		else if (in_event)
		{
			size_t colon = logical_line.find(':');
			if (colon != std::string::npos)
			{
				std::string key_and_params = logical_line.substr(0, colon);
				std::string value = logical_line.substr(colon + 1);

				size_t semi = key_and_params.find(';');
				std::string key = (semi == std::string::npos) ? key_and_params : key_and_params.substr(0, semi);

				if (key == "SUMMARY")
				{
					cur_summary = unescape_ics_text(value);
				}
				else if (key == "DTSTART")
				{
					cur_dtstart_value = value;
					cur_is_date_only = key_and_params.find("VALUE=DATE") != std::string::npos;
					have_dtstart = true;
				}
			}
		}

		logical_line.clear();
	};

	int pos = 0;
	int len = body.length();
	while (pos < len)
	{
		int nl = body.indexOf('\n', pos);
		String raw_line = (nl == -1) ? body.substring(pos) : body.substring(pos, nl);
		pos = (nl == -1) ? len : nl + 1;

		// Line folding: a line starting with a space or tab continues the
		// previous line - join it (minus that one leading character)
		// without ending the logical line.
		if (!raw_line.isEmpty() && (raw_line[0] == ' ' || raw_line[0] == '\t'))
		{
			logical_line += std::string(raw_line.c_str() + 1);
			continue;
		}

		flush_line();
		logical_line = raw_line.c_str();
	}
	flush_line();

	time_t now = time(nullptr);
	struct tm now_tm;
	localtime_r(&now, &now_tm);
	long today_key = (long)(now_tm.tm_year + 1900) * 100000000L + (long)(now_tm.tm_mon + 1) * 1000000L + (long)now_tm.tm_mday * 10000L;

	std::vector<AgendaEvent> upcoming;
	for (auto &ev : parsed)
	{
		if (ev.sort_key >= today_key)
			upcoming.push_back(ev);
	}

	std::sort(upcoming.begin(), upcoming.end(), [](const AgendaEvent &a, const AgendaEvent &b) { return a.sort_key < b.sort_key; });

	if (upcoming.size() > MAX_EVENTS)
		upcoming.resize(MAX_EVENTS);

	events = std::move(upcoming);

	squixl.get_cached_char_sizes(FONT_SPEC::FONT_WEIGHT_B, 3, &row_char_w, &row_char_h);
}

void widgetCalendar::process_ics_data(bool success, const String &response)
{
	bool ok = true;
	if (response == "ERROR")
	{
		ok = false;
	}
	else
	{
		parse_ics(response);
	}

	Serial.printf("Calendar: process_ics_data callback ran - ok=%d events=%d response_len=%d\n", ok, (int)events.size(), response.length());

	if (ok)
		has_data = true;

	should_redraw = true;
	is_fetching = false;
	next_update = millis();

	// Required - determines the lifetime of the response String so it
	// survives until after it's been used, matching widgetStockList.
	delete &response;
}

bool widgetCalendar::redraw(uint8_t fade_amount, int8_t tab_group)
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
		calc_text_size("Agenda", UbuntuMono_B[3], &hdr_w, &hdr_h);
		_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
		_sprite_back.setCursor((_w - hdr_w) / 2, HEADER_H / 2 + hdr_h / 2);
		_sprite_back.print("Agenda");

		int16_t row_y0 = HEADER_H + 10;

		if (events.empty())
		{
			_sprite_back.setFreeFont(UbuntuMono_B[3]);
			_sprite_back.setTextColor(dashboard_theme::text_secondary, -1);
			const char *msg;
			if (!settings.config.calendar.ics_url.length())
				msg = "NOT CONFIGURED";
			else if (!has_data)
				msg = wifi_controller.is_connected() ? "..." : "NO INTERNET";
			else
				msg = "NO UPCOMING EVENTS";

			int text_w, text_h;
			calc_text_size(msg, UbuntuMono_B[3], &text_w, &text_h);
			_sprite_back.setCursor((_w - text_w) / 2, row_y0 + row_char_h + 20);
			_sprite_back.print(msg);
		}
		else
		{
			int16_t row_h = (_h - row_y0 - padding.bottom) / (int16_t)events.size();

			for (size_t i = 0; i < events.size(); i++)
			{
				const AgendaEvent &ev = events[i];
				int16_t line1_y = row_y0 + (int16_t)i * row_h + row_char_h;
				int16_t line2_y = line1_y + row_char_h + 8;

				_sprite_back.setFreeFont(UbuntuMono_B[3]);
				_sprite_back.setTextColor(dashboard_theme::accent_amber, -1);
				_sprite_back.setCursor(padding.left, line1_y);
				_sprite_back.printf("%s - %s", ev.date_label.c_str(), ev.time_label.c_str());

				// Truncate the summary with an ellipsis if it's wider than
				// the card, rather than letting it overflow the edge.
				std::string summary = ev.summary;
				int text_w, text_h;
				calc_text_size(summary.c_str(), UbuntuMono_B[3], &text_w, &text_h);
				int16_t max_w = _w - padding.left - padding.right;
				while (text_w > max_w && summary.length() > 1)
				{
					summary.pop_back();
					calc_text_size((summary + "...").c_str(), UbuntuMono_B[3], &text_w, &text_h);
				}
				if (summary.length() < ev.summary.length())
					summary += "...";

				_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
				_sprite_back.setCursor(padding.left, line2_y);
				_sprite_back.print(summary.c_str());
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

bool widgetCalendar::process_touch(touch_event_t touch_event)
{
	return false;
}
