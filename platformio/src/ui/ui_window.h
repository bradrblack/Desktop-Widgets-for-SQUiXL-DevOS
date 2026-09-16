#pragma once

#include "ui/ui_element.h"
#include "fonts/ubuntu_mono_all_r.h"
#include <map>
#include <vector>
#include <string>

class ui_control_base;

class ui_window : public ui_element
{
	public:
		using CallbackFunction = void (*)();

		void create(int16_t pos_x, int16_t pos_y, int16_t width, int16_t height, int16_t color, uint8_t transparecy, uint8_t blur_count, const char *title);

		void set_title_alignment(TEXT_ALIGN align);

		void draw_window_heading();

		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;

		bool process_touch(touch_event_t touch_event) override;

		// Frees/recreates this window's 4 full-size sprites (_sprite_back,
		// _sprite_content, _sprite_mixed, _sprite_clean) on the same
		// show/close events ui_screen already uses to manage its own
		// buffers - see ui_window.cpp for why these were previously left
		// permanently allocated instead.
		void about_to_show_screen() override;
		void about_to_close_screen() override;

	protected:
		// A window that fully overrides redraw() (every current card widget
		// - Markets/Weather/Calendar/News - does) only ever draws into
		// ui_parent->_sprite_content (the SCREEN's, not this window's own),
		// leaving its own _sprite_content sitting permanently allocated and
		// completely unused - one whole extra card-sized buffer per widget,
		// for nothing. Override to return false to skip allocating/
		// recreating it. Anything relying on this class's own default
		// redraw() below (which does draw into its own _sprite_content) must
		// leave this at the default.
		virtual bool needs_own_content_sprite() { return true; }


		int16_t _adj_x;			// alignment adjusted draw pos x
		int16_t _adj_y;			// alignment adjusted draw pos y
		uint8_t padding_l = 10; // left padding for window content
		uint8_t padding_t = 30; // top padding for window content
		uint8_t padding_r = 10; // right padding for window content
		uint8_t padding_b = 10; // bottom padding for window content

		uint16_t _text_width, _text_height;

		const GFXfont *_font = UbuntuMono_R[1];

		bool calculate_text_size(bool forced = false);
};