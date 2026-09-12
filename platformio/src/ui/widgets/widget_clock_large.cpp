#include "ui/widgets/widget_clock_large.h"

#include "fonts/ubuntu_mono_bold_44pt_aa.h"
#include "peripherals/rtc.h"
#include "ui/theme_dashboard.h"
#include "ui/ui_screen.h"

void widgetClockLarge::create(int16_t center_x, int16_t y)
{
	_c = dashboard_theme::text_primary;

	// Only this canvas is antialiased - setAntialias() defaults off, and
	// this is the only place it's ever enabled, so no other widget's
	// (1-bit) fonts are affected. UbuntuMono_Bold44pt7bAA is specifically
	// an 8-bit-coverage font meant for this mode; drawing it with
	// antialias off (or drawing a normal 1-bit font with it on) would
	// render garbage - see that font's own header comment.
	_sprite_content.setAntialias(true);

	int tw, th;
	calc_text_size("88:88", &UbuntuMono_Bold44pt7bAA, &tw, &th);
	_glyph_w = (uint16_t)tw;
	_glyph_h = (uint16_t)th;

	_w = (int16_t)(_glyph_w + 8);
	_h = (int16_t)(_glyph_h + 8);
	_x = center_x - _w / 2;
	_y = y;

	_sprite_content.create(_w, _h);
}

bool widgetClockLarge::redraw(uint8_t fade_amount, int8_t tab_group)
{
	if (millis() < delay_first_draw)
		return false;

	if (is_busy)
		return false;

	is_busy = true;

	bool changed = false;

	// rtc.did_time_change() consumes a change flag shared with the RTC chip
	// driver's own cached state - it's only true once per real time change,
	// and only for whichever caller happens to check it first. This widget's
	// redraw() doesn't run at all while the clock screen isn't visible (a
	// non-current screen's children never get redrawn), so on returning to
	// it the flag had usually already gone stale/consumed, and the screen
	// kept showing whatever time it last drew - sometimes a full minute
	// stale - until the RTC ticked over again. Comparing the freshly-read
	// string directly instead means this widget always shows the correct
	// time on its very next redraw, regardless of how long it was offscreen.
	std::string full = rtc.get_time_string(false, settings.config.time_24hour).c_str();
	if (full != _time_string)
	{
		_time_string = full;
		changed = true;
	}

	if (changed && !_time_string.empty())
	{
		// The canvas stays sized for the widest possible string ("88:88") so
		// the widget's screen position never shifts, but a shorter string
		// (e.g. single-digit hour in 12hr mode) needs to be re-centered
		// within that fixed-size canvas each time, rather than always
		// starting at the same left edge.
		int actual_w, actual_h;
		calc_text_size(_time_string.c_str(), &UbuntuMono_Bold44pt7bAA, &actual_w, &actual_h);
		int16_t cursor_x = ((int16_t)(_glyph_w + 8) - (int16_t)actual_w) / 2;

		// Rendered natively at the size it's displayed - no intermediate
		// small canvas + upscale step needed now that the font itself is
		// already the target size. setTextColor()'s bg argument is what
		// drawGlyphAA() blends edge pixels against, so it must match the
		// fillRect() below exactly.
		_sprite_content.fillRect(0, 0, _w, _h, dashboard_theme::background);
		_sprite_content.setFreeFont(&UbuntuMono_Bold44pt7bAA);
		_sprite_content.setTextColor(dashboard_theme::text_primary, dashboard_theme::background);
		_sprite_content.setCursor(cursor_x, _glyph_h + 2);
		_sprite_content.print(_time_string.c_str());
	}

	// Blit unconditionally, even when the text itself hasn't changed. The
	// parent screen's own buffer gets freed and recreated blank every time
	// this screen stops being current and becomes current again (its PSRAM
	// isn't held onto in between), so skipping the blit just because the
	// clock string is unchanged left a freshly-blanked parent buffer with
	// nothing ever drawn onto it whenever you returned within the same
	// minute - this widget's own small canvas already has the right content
	// sitting in it either way, so just always re-composite it.
	if (!_time_string.empty())
		ui_parent->_sprite_content.drawSprite(_x, _y, &_sprite_content, 1.0f, -1);
	next_refresh = millis();

	is_dirty = false;
	is_busy = false;

	return changed;
}

bool widgetClockLarge::process_touch(touch_event_t touch_event)
{
	return false;
}
