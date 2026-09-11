#pragma once

#include "ui/ui_element.h"

// Large, clean HH:MM clock. Digits are drawn as rounded seven-segment glyphs
// rather than a bitmap font, so it can be sized far larger than the biggest
// embedded font (22pt) while keeping crisp, un-pixelated edges.
class widgetBigClock : public ui_element
{
	public:
		void create(int16_t x, int16_t y, uint16_t color, TEXT_ALIGN alignment, uint16_t digit_height = 150);

		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

	private:
		void draw_digit(int at_x, uint8_t digit);
		void draw_colon(int at_x);

		uint16_t _color = 0;

		uint16_t _digit_h = 0;
		uint16_t _digit_w = 0;
		uint16_t _colon_w = 0;
		uint16_t _seg_t = 0;
		uint16_t _gap = 0;
		uint16_t _inset = 0;

		std::string _time_string = "";
		bool colon_on = true;

		int16_t _adj_x = 0;
		int16_t _adj_y = 0;
};
