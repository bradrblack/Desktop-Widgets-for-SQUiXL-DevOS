#pragma once

#include "ui/ui_window.h"
#include <vector>

struct AgendaEvent
{
		std::string summary;
		std::string date_label; // e.g. "Mon Sep 15"
		std::string time_label; // e.g. "9:00 AM" or "All day"
		long sort_key = 0;		 // YYYYMMDDHHMM as a comparable integer, local-time-ish
};

// Upcoming-events list card, styled to match widgetStockList. Reads its
// source calendar from the web portal's Calendar Settings (a Google
// Calendar "secret address in iCal format", or any other public/secret
// .ics URL) - see settings_async.h Config_widget_calendar.
//
// Simplifications made for a small on-device parser (documented here so
// they're not mistaken for bugs):
//  - Recurring events (RRULE) are shown only at their original DTSTART:
//    recurrence expansion isn't implemented, so a weekly meeting will only
//    ever show its first-ever occurrence.
//  - DTSTART values are read as their raw date/time digits with no
//    timezone conversion, so an event in a different timezone from the
//    calendar's own may display at the wrong local time.
class widgetCalendar : public ui_window
{
	public:
		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

		void process_ics_data(bool success, const String &response);

		// Kicks off the first fetch as soon as WiFi connects at boot, rather
		// than waiting for this widget's screen to become active.
		void prefetch();

		// Drops the cached event list and re-fetches immediately - called
		// when the Calendar Settings group is saved in the web portal, so a
		// changed URL takes effect without requiring a reboot.
		void reload_events();

	private:
		std::vector<AgendaEvent> events;
		psram_string ics_url;

		unsigned long next_update = 0;
		unsigned long fetch_started_at = 0;
		bool is_fetching = false;
		bool should_redraw = false;
		bool has_data = false;

		uint8_t row_char_w = 0;
		uint8_t row_char_h = 0;

		void maybe_fetch();
		void parse_ics(const String &body);
};
