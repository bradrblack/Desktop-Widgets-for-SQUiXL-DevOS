#pragma once

#include "ui/ui_element.h"

// Carousel play/pause toggle, lives on the clock screen. Tapping it flips
// `carousel_playing`, which main.cpp's loop() uses to auto-advance through
// carousel_screens. Any other touch anywhere pauses it again - main.cpp
// detects that by comparing squixl.get_currently_selected() against this
// widget on a fresh touch-down, so this toggle itself never needs to guard
// against self-cancelling.
extern bool carousel_playing;
extern unsigned long carousel_last_advance;
extern unsigned long CAROUSEL_INTERVAL_MS;
extern unsigned long CAROUSEL_FIRST_ADVANCE_MS;

class widgetPlayPause : public ui_element
{
	public:
		void create(int16_t x, int16_t y);

		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

		// Forces the next redraw() to actually draw and report a change,
		// even if carousel_playing hasn't changed since the last draw.
		// Needed because the parent screen's canvas is fully wiped and
		// recreated every time the screen is navigated away from and back
		// to - see the call site in main.cpp's loop() for why this can't
		// just be inferred from the parent's buffer pointer changing.
		void force_redraw() { last_drawn_playing = !carousel_playing; }

	private:
		bool sprite_created = false;
		unsigned long last_toggle_at = 0;
		bool last_drawn_playing = false;
};
