#pragma once

#include "ui/ui_element.h"

// Plain HH:MM clock using the largest embedded font (22pt Bold) at a modest
// 2x scale - large and legible without the blockiness a bigger scale factor
// would introduce, and it reads as a real typeface rather than the
// mechanical look of drawn segments.
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
		float _scale = 2.0f;
};
