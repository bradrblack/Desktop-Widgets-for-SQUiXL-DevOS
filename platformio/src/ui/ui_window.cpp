#include "ui/ui_window.h"

static std::vector<ui_window *> windows;

void ui_window::create(int16_t pos_x, int16_t pos_y, int16_t width, int16_t height, int16_t color, uint8_t transparency, uint8_t blur_count, const char *title)
{
	_x = pos_x;
	_y = pos_y;
	_w = width;
	_h = height;
	_c = color;
	_t = min(transparency, (uint8_t)32); // 0-32?
	_b = min(blur_count, (uint8_t)100);

	_title = title;
	_align = TEXT_ALIGN::ALIGN_LEFT;

	_font = UbuntuMono_R[1];

	_sprite_back.create(_w, _h);
	if (needs_own_content_sprite())
		_sprite_content.create(_w, _h);
	_sprite_mixed.create(_w, _h);

	// Create the sprite to hold the clean background of the window
	// read whatever is on the screen alrady at x,y,w,h and store it in the sprite
	// we will draw over that with whatever is on the screen
	_sprite_clean.create(_w, _h);
	squixl.lcd.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_clean.getBuffer());

	calculate_text_size(true);

	windows.push_back(this);

	is_dirty = true;
	is_dirty_hard = true;
}

void ui_window::draw_window_heading()
{
	// Draw the window background and title bar
	_sprite_content.fillScreen(TFT_MAGENTA);
	_sprite_content.fillRoundRect(0, 0, _w, _h, 7, _c); // white will be our mask
	squixl.lcd.blendSprite(&_sprite_content, &_sprite_back, &_sprite_back, _t, TFT_MAGENTA);
	_sprite_content.fillScreen(TFT_MAGENTA);
	_sprite_content.fillRoundRect(0, 0, _w, 24, 7, _c); // white will be our mask
	squixl.lcd.blendSprite(&_sprite_content, &_sprite_back, &_sprite_back, min(_t * 2, 16), TFT_MAGENTA);

	// Draw the window title left jusified
	_sprite_back.setTextColor(TFT_WHITE, -1);
	_sprite_back.setFreeFont(UbuntuMono_R[1]);
	_sprite_back.setCursor(padding.left, 27 - ((24 - _text_height) / 2));
	_sprite_back.print(_title.c_str());
}

bool ui_window::redraw(uint8_t fade_amount, int8_t tab_group)
{
	if (is_dirty_hard)
	{
		squixl.lcd.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_clean.getBuffer());
		is_dirty_hard = false;
	}

	if (is_dirty || fade_amount == 0)
	{
		squixl.lcd.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_back.getBuffer());
		squixl.lcd.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_content.getBuffer());

		squixl.lcd.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_clean.getBuffer());

		// _sprite_content.fillScreen(TFT_BLACK);
		_sprite_content.fillRoundRect(0, 0, _w, _h, 7, _c); // white will be our mask

		squixl.lcd.blendSprite(&_sprite_content, &_sprite_back, &_sprite_back, _t);

		_sprite_back.setTextColor(TFT_WHITE, -1);
		_sprite_back.setFreeFont(_font);
		_sprite_back.setCursor(_w / 2 - _text_width / 2, _text_height + 10);

		_sprite_back.print(_title.c_str());

		for (int w = 0; w < ui_children.size(); w++)
		{
			ui_children[w]->set_dirty(true);
			ui_children[w]->redraw(32);
		}
	}

	if (fade_amount < 32)
	{
		squixl.lcd.blendSprite(&_sprite_back, &_sprite_clean, &_sprite_mixed, fade_amount);
		squixl.lcd.drawSprite(_x, _y, &_sprite_mixed, 1.0f, 0x0);
	}
	else
	{
		// squixl.lcd.blendSprite(&_sprite_back, &_sprite_clean, &_sprite_mixed, 32);
		squixl.lcd.drawSprite(_x, _y, &_sprite_mixed, 1.0f, 0x0);
		next_refresh = millis() + refresh_interval;
	}

	is_dirty = false;

	return true;
}

// These 4 sprites are the same size class as a screen's own full-size
// buffers (a card is typically 440x440 against the screen's 480x480), but
// unlike a screen's buffers - which are deliberately freed/recreated on
// exactly these events, specifically to keep PSRAM available for whichever
// screens actually need a full buffer right now (see
// ui_screen::create_buffers()/clear_buffers()) - a window's sprites were
// only ever allocated once in create() and never released again. With
// several persistent card widgets (Markets/Weather/Calendar/News) each
// holding 4 such buffers for their entire lifetime regardless of whether
// their screen is ever visible, that leaves too little contiguous PSRAM for
// two neighbouring screens' buffers to coexist during a swipe - observed as
// "create_buffers: FAILED to allocate _sprite_back (480x480)" with the
// largest free PSRAM chunk stuck just under the ~460800 bytes needed.
// Matching the screen's own discipline here fixes that at the source rather
// than trying to shrink any one widget's footprint.
void ui_window::about_to_close_screen()
{
	_sprite_back.release();
	_sprite_content.release();
	_sprite_mixed.release();
	_sprite_clean.release();

	for (int w = 0; w < ui_children.size(); w++)
		ui_children[w]->about_to_close_screen();
}

void ui_window::about_to_show_screen()
{
	// Tracked so the "force a repaint" block below only fires when a buffer
	// actually needed recreating - about_to_show_screen() can be called more
	// than once for the same visit (see setup_draggable_neighbour(true),
	// which calls this eagerly for the live drag preview, and finish_drag()
	// calls it again once the swipe commits), and forcing a full ~130ms
	// repaint on every one of those would reintroduce jank during the drag
	// itself rather than fixing it.
	bool recreated = false;

	if (!_sprite_back.getBuffer())
	{
		_sprite_back.create(_w, _h);
		recreated = true;
	}
	if (needs_own_content_sprite() && !_sprite_content.getBuffer())
	{
		_sprite_content.create(_w, _h);
		recreated = true;
	}
	if (!_sprite_mixed.getBuffer())
	{
		_sprite_mixed.create(_w, _h);
		recreated = true;
	}
	if (!_sprite_clean.getBuffer())
	{
		_sprite_clean.create(_w, _h);
		squixl.lcd.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_clean.getBuffer());
		recreated = true;
	}

	if (recreated)
	{
		// Buffers just came back from being blank/released - every
		// subclass's redraw() (including the card widgets, which override
		// redraw() entirely rather than calling this class's own) gates its
		// repaint on one of these two flags, so this guarantees a full
		// repaint into the freshly-recreated buffers on the very next
		// redraw() rather than assuming stale content is still there.
		is_dirty = true;
		is_dirty_hard = true;

		// is_dirty/is_dirty_hard alone don't get redraw() actually called,
		// though - ui_screen::position_children() only calls a child's
		// redraw() once child->should_refresh() says so, which is a plain
		// next_refresh/refresh_interval timer check with no idea a hard
		// refresh is now overdue. Most of these cards use a multi-second
		// interval (e.g. 2000ms), so without this, a freshly-recreated
		// (blank) card would sit visibly blank/grey for up to that whole
		// interval after every single navigation to it, until its own timer
		// happened to allow the next redraw() - forcing that timer to fire
		// on the very next check instead makes the repaint immediate.
		next_refresh = 0;
	}

	for (int w = 0; w < ui_children.size(); w++)
		ui_children[w]->about_to_show_screen();
}

bool ui_window::process_touch(touch_event_t touch_event)
{
	// Did any of my children recieve this touch event?
	for (int w = 0; w < ui_children.size(); w++)
	{
		if (ui_children[w]->process_touch(touch_event))
		{
			next_update = 0;
			return true;
		}
	}

	/*
		Do any logic here for my own touch requirements
	*/
	return false;
}

void ui_window::set_title_alignment(TEXT_ALIGN new_align)
{
	bool recalculate = (_align != new_align);
	_align = new_align;

	if (recalculate)
	{
		// Work out new X,Y coordinates based on alingment
		if (_align == TEXT_ALIGN::ALIGN_LEFT)
		{
			_adj_x = _x;
			_adj_y = _y;
		}
		else if (_align == TEXT_ALIGN::ALIGN_CENTER)
		{
			_adj_x = _x - _w / 2;
			_adj_y = _y;
		}
		else if (_align == TEXT_ALIGN::ALIGN_RIGHT)
		{
			_adj_x = _x - _w;
			_adj_y = _y;
		}

		is_dirty = true;
	}
}

// Private

bool ui_window::calculate_text_size(bool forced)
{
	int16_t tempx;
	int16_t tempy;
	uint16_t tempw;
	uint16_t temph;

	int _text_pos_x = _w / 2 - _text_width / 2;
	int _text_pos_y = _text_height;

	bool changed = false;

	_sprite_content.getTextBounds(_title.c_str(), _text_pos_x, _text_pos_y, &tempx, &tempy, &tempw, &temph);

	if (tempw != _w || temph != _h || forced)
	{
		changed = true;

		_text_width = tempw;
		_text_height = temph;

		// Work out new X,Y coordinates based on alingment
		if (_align == TEXT_ALIGN::ALIGN_LEFT)
		{
			_adj_x = _x;
			_adj_y = _y;
		}
		else if (_align == TEXT_ALIGN::ALIGN_CENTER)
		{
			_adj_x = _x - _w / 2;
			_adj_y = _y;
		}
		else if (_align == TEXT_ALIGN::ALIGN_RIGHT)
		{
			_adj_x = _x - _w;
			_adj_y = _y;
		}
	}

	return changed;
}
