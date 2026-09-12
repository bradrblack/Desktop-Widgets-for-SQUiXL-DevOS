#pragma once

#include "ui/ui_element.h"

// Plain HH:MM clock, rendered natively at 44pt (fonts/ubuntu_mono_bold_44pt.h)
// rather than upscaling the largest embedded size (22pt Bold) - upscaling a
// bitmap font via drawSprite()'s scale factor is a naive pixel-doubling
// blit, which is what made the previous version look blocky at this size.
class widgetClockLarge : public ui_element
{
	public:
		void create(int16_t center_x, int16_t y);

		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

	private:
		std::string _time_string = "";

		uint16_t _glyph_w = 0;
		uint16_t _glyph_h = 0;
};
