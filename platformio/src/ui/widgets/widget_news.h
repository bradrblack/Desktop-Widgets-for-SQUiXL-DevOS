#pragma once

#include "ui/ui_window.h"
#include <vector>

// NYT Top Stories (home section) headline card - see developer.nytimes.com.
// Fetches the whole section every 30 min (the endpoint has no field-filter
// or count-limit query param) but only keeps each story's "title" via a SAX
// parse - see widget_news.cpp's NewsSax for why that keeps this safe despite
// the response being much larger than e.g. the Markets card's. Shows one
// random headline at a time; re-picks whenever this screen becomes current
// (see main.cpp's screen-transition tracking) or on tap.
class widgetNews : public ui_window
{
	public:
		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

		void process_news_data(bool success, const String &response);

		// Kicks off the first fetch as soon as WiFi connects at boot, rather
		// than waiting for this widget's screen to become active.
		void prefetch();

		// Drops the cached headline collection and re-fetches immediately -
		// called when the News Settings group is saved in the web portal, so
		// a newly-added API key takes effect without requiring a reboot.
		void reload_news();

		// Picks a new random headline from the cached collection, different
		// from the one currently shown when more than one is available, and
		// stamps last_pick_at. Called directly by a data refresh
		// (process_news_data()), a deliberate tap (process_touch()), and the
		// 60s idle auto-rotate in redraw(). For the screen-entry trigger, use
		// on_screen_shown() instead (see below).
		void pick_random_headline();

		// Screen-entry version of the above - main.cpp calls this (not
		// pick_random_headline() directly) whenever this becomes the current
		// screen. Debounced against last_pick_at: entering the screen can
		// coincide with a just-finished data refresh (e.g. saving a
		// freshly-entered API key re-fetches immediately, then the user
		// swipes over to check it), which already picked a headline of its
		// own moments earlier - without this, that shows as one headline
		// being visibly swapped for another right after landing on the card.
		void on_screen_shown();

	private:
		std::vector<std::string> headlines;
		std::string current_headline;

		unsigned long next_update = 0;
		unsigned long fetch_started_at = 0;
		unsigned long last_shuffle_at = 0;
		// Shared by every pick_random_headline() caller - both the 3s
		// screen-entry debounce and the 60s idle auto-rotate below key off
		// this same timestamp so they can't fight each other into a rapid
		// double-change.
		unsigned long last_pick_at = 0;
		bool is_fetching = false;
		bool should_redraw = false;
		bool has_data = false;

		uint8_t row_char_w = 0;
		uint8_t row_char_h = 0;

		void maybe_fetch();

		// Splits a headline into lines that each fit within max_width at the
		// given font, breaking only on word boundaries. A member function
		// (not a free helper) purely so it can call the inherited
		// calc_text_size() the same way the rest of this class does.
		std::vector<std::string> wrap_text(const std::string &text, const GFXfont *font, int16_t max_width);
};
