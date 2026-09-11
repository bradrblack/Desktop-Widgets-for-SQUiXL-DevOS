#include "ui/widgets/widget_big_clock.h"

#include "peripherals/rtc.h"
#include "ui/ui_screen.h"

namespace
{
	// Segment bits: a(top) b(upper-right) c(lower-right) d(bottom) e(lower-left) f(upper-left) g(middle)
	constexpr uint8_t SEG_A = 0x01, SEG_B = 0x02, SEG_C = 0x04, SEG_D = 0x08, SEG_E = 0x10, SEG_F = 0x20, SEG_G = 0x40;

	constexpr uint8_t DIGIT_SEGMENTS[10] = {
		SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,			// 0
		SEG_B | SEG_C,											// 1
		SEG_A | SEG_B | SEG_G | SEG_E | SEG_D,					// 2
		SEG_A | SEG_B | SEG_G | SEG_C | SEG_D,					// 3
		SEG_F | SEG_G | SEG_B | SEG_C,							// 4
		SEG_A | SEG_F | SEG_G | SEG_C | SEG_D,					// 5
		SEG_A | SEG_F | SEG_G | SEG_E | SEG_C | SEG_D,			// 6
		SEG_A | SEG_B | SEG_C,									// 7
		SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, // 8
		SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,			// 9
	};
}

void widgetBigClock::create(int16_t x, int16_t y, uint16_t color, TEXT_ALIGN alignment, uint16_t digit_height)
{
	_x = x;
	_y = y;
	_c = color;
	_color = color;
	_align = alignment;

	_digit_h = digit_height;
	_digit_w = (uint16_t)(_digit_h * 0.56f);
	_seg_t = (uint16_t)(_digit_h * 0.15f);
	_colon_w = (uint16_t)(_digit_w * 0.34f);
	_gap = (uint16_t)(_digit_h * 0.045f);
	_inset = (uint16_t)(_seg_t * 0.4f);

	_w = _digit_w * 4 + _colon_w + _gap * 4;
	_h = _digit_h;

	if (_align == TEXT_ALIGN::ALIGN_LEFT)
	{
		_adj_x = _x;
	}
	else if (_align == TEXT_ALIGN::ALIGN_CENTER)
	{
		_adj_x = _x - _w / 2;
	}
	else if (_align == TEXT_ALIGN::ALIGN_RIGHT)
	{
		_adj_x = _x - _w;
	}
	_adj_y = _y;

	_sprite_content.create(_w, _h);
}

void widgetBigClock::draw_digit(int at_x, uint8_t digit)
{
	if (digit > 9)
		return;

	uint8_t segs = DIGIT_SEGMENTS[digit];

	int16_t y_top = 0;
	int16_t y_mid_top = (_digit_h - _seg_t) / 2;
	int16_t y_mid_bot = y_mid_top + _seg_t;
	int16_t y_bot = _digit_h - _seg_t;
	int16_t r = _seg_t / 2;

	if (segs & SEG_A)
		_sprite_content.fillRoundRect(at_x + _inset, y_top, _digit_w - 2 * _inset, _seg_t, r, _color);
	if (segs & SEG_G)
		_sprite_content.fillRoundRect(at_x + _inset, y_mid_top, _digit_w - 2 * _inset, _seg_t, r, _color);
	if (segs & SEG_D)
		_sprite_content.fillRoundRect(at_x + _inset, y_bot, _digit_w - 2 * _inset, _seg_t, r, _color);

	int16_t vy_top_start = y_top + _seg_t + _gap;
	int16_t vy_top_end = y_mid_top - _gap;
	int16_t vy_bot_start = y_mid_bot + _gap;
	int16_t vy_bot_end = y_bot - _gap;

	if (segs & SEG_F)
		_sprite_content.fillRoundRect(at_x, vy_top_start, _seg_t, vy_top_end - vy_top_start, r, _color);
	if (segs & SEG_B)
		_sprite_content.fillRoundRect(at_x + _digit_w - _seg_t, vy_top_start, _seg_t, vy_top_end - vy_top_start, r, _color);
	if (segs & SEG_E)
		_sprite_content.fillRoundRect(at_x, vy_bot_start, _seg_t, vy_bot_end - vy_bot_start, r, _color);
	if (segs & SEG_C)
		_sprite_content.fillRoundRect(at_x + _digit_w - _seg_t, vy_bot_start, _seg_t, vy_bot_end - vy_bot_start, r, _color);
}

void widgetBigClock::draw_colon(int at_x)
{
	if (!colon_on)
		return;

	int16_t dot = _seg_t;
	int16_t cx = at_x + (_colon_w - dot) / 2;

	_sprite_content.fillRoundRect(cx, _digit_h / 3 - dot / 2, dot, dot, dot / 2, _color);
	_sprite_content.fillRoundRect(cx, (_digit_h * 2) / 3 - dot / 2, dot, dot, dot / 2, _color);
}

bool widgetBigClock::redraw(uint8_t fade_amount, int8_t tab_group)
{
	if (millis() < delay_first_draw)
		return false;

	if (is_busy)
		return false;

	is_busy = true;

	bool changed = false;

	if (rtc.did_time_change() || !is_setup)
	{
		is_setup = true;
		colon_on = !colon_on;

		std::string new_time = rtc.get_time_string(true, settings.config.time_24hour).c_str();
		if (new_time != _time_string)
			_time_string = new_time;

		changed = true; // redraw every tick so the colon blinks even when the minute hasn't changed
	}

	if (changed)
	{
		_sprite_content.fillRect(0, 0, _w, _h, TFT_MAGENTA);

		int cursor_x = 0;
		for (char c : _time_string)
		{
			if (c == ':')
			{
				draw_colon(cursor_x);
				cursor_x += _colon_w + _gap;
			}
			else if (c >= '0' && c <= '9')
			{
				draw_digit(cursor_x, c - '0');
				cursor_x += _digit_w + _gap;
			}
		}

		ui_parent->_sprite_content.drawSprite(_adj_x, _adj_y, &_sprite_content, 1.0f, TFT_MAGENTA);
		next_refresh = millis();
	}

	is_dirty = false;
	is_busy = false;

	return changed;
}

bool widgetBigClock::process_touch(touch_event_t touch_event)
{
	return false;
}
