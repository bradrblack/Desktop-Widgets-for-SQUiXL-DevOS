#pragma once

#include "ui/ui_window.h"
#include <vector>

struct StockQuote
{
		std::string label;  // friendly display name, e.g. "Apple"
		std::string ticker; // Yahoo Finance symbol, e.g. "AAPL"

		float price = 0.0f;
		float change_pct = 0.0f;
		bool has_data = false;
};

// A single card listing several ticker quotes (symbol / price / % change),
// fetched from Yahoo Finance's free, unauthenticated "spark" endpoint - one
// request returns every symbol at once. Card is sized tall enough to show
// every configured symbol at once; add more to `init_symbols()` and grow the
// card height (or add scrolling) if the list outgrows it.
class widgetStockList : public ui_window
{
	public:
		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

		void process_quote_data(bool success, const String &response);

		// Kicks off the first fetch as soon as WiFi connects at boot, rather
		// than waiting for this widget's screen to become active - otherwise
		// the Markets card sits empty until the user happens to swipe to it.
		void prefetch();

		// Drops the cached symbol list so the next maybe_fetch() rebuilds it
		// from current settings and re-fetches immediately, instead of
		// requiring a reboot to pick up a config change saved in the portal.
		void reload_symbols();

	protected:
		// This card fully overrides redraw() and only ever draws into
		// ui_parent->_sprite_content (the screen's) - its own inherited
		// _sprite_content is never touched, so skip allocating it entirely.
		bool needs_own_content_sprite() override { return false; }

	private:
		std::vector<StockQuote> quotes;
		psram_string batch_url;

		unsigned long next_update = 0;
		unsigned long fetch_started_at = 0;
		bool is_fetching = false;
		bool should_redraw = false;
		bool has_data = false;

		uint8_t row_char_w = 0;
		uint8_t row_char_h = 0;

		void init_symbols();
		void maybe_fetch();
};
