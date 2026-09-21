#pragma once

#include "ui/ui_element.h"

// Plain HH:MM clock, rendered natively (fonts/ubuntu_mono_bold_88pt_aa.h)
// rather than upscaling a smaller embedded size - upscaling a bitmap font
// via drawSprite()'s scale factor is a naive pixel-doubling blit, which is
// what made an earlier version look blocky at this size.
class widgetClockLarge : public ui_element
{
	public:
		// center_x/center_y: centered on both axes, same convention -
		// callers shouldn't need to know or care how tall the current font
		// happens to be, and switching font size (as already happened once)
		// shouldn't require re-tuning a top-edge y offset by hand.
		void create(int16_t center_x, int16_t center_y);

		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

	private:
		std::string _time_string = "";

		uint16_t _glyph_w = 0;
		uint16_t _glyph_h = 0;

		uint8_t _rainbow_step = 0;
		bool _last_rainbow = false;
		uint16_t _last_color = 0;
};
