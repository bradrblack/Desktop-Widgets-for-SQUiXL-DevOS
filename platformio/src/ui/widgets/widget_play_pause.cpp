#include "ui/widgets/widget_play_pause.h"
#include "ui/theme_dashboard.h"
#include "audio/audio.h"

void widgetPlayPause::create(int16_t x, int16_t y)
{
	_x = x;
	_y = y;
	_w = 44;
	_h = 44;

	// Generous forgiving margin around the small hit target - default is 0.
	touch_padding = 14;

	// should_refresh() only returns true on a timer (refresh_interval == 0
	// means "never redraw again after the first frame", regardless of
	// is_dirty) - without this the icon drew once at boot and then never
	// updated no matter how many times it was tapped.
	set_refresh_interval(150);
}

bool widgetPlayPause::process_touch(touch_event_t touch_event)
{
	if (touch_event.type == TOUCH_TAP && check_bounds(touch_event.x, touch_event.y))
	{
		// The single-tap dispatch itself is deferred ~80-400ms (the
		// framework waits to rule out a double-tap), which makes the icon
		// feel unresponsive enough that a second, perfectly legitimate tap
		// on this same button often lands within a second or two - toggling
		// straight back off and looking like an unexplained revert. Ignore
		// a repeat tap on this button within a short cooldown of the last
		// one; the audible click below gives more immediate confirmation
		// that the first tap landed, so there's less temptation to tap again.
		if (millis() - last_toggle_at < 700)
			return true;
		last_toggle_at = millis();

		carousel_playing = !carousel_playing;
		// A small speaker's frequency response rolls off badly below
		// ~900Hz, so a low "pause" tone can end up effectively inaudible;
		// keep both tones well above that, and a touch longer (5 = 50ms).
		audio.play_tone(carousel_playing ? 1600 : 1000, 5);
		Serial.printf("PlayPause: tapped at (%d,%d), now %s\n", touch_event.x, touch_event.y, carousel_playing ? "PLAYING" : "PAUSED");

		if (carousel_playing)
		{
			// Backdate the timer so the first advance fires after
			// CAROUSEL_FIRST_ADVANCE_MS rather than the full interval;
			// loop()'s advance block resets it to a normal full-interval
			// wait once that first hop happens.
			carousel_last_advance = millis() - (CAROUSEL_INTERVAL_MS - CAROUSEL_FIRST_ADVANCE_MS);
		}

		return true;
	}

	return false;
}

bool widgetPlayPause::redraw(uint8_t fade_amount, int8_t tab_group)
{
	if (millis() < delay_first_draw)
		return false;

	if (!sprite_created)
	{
		sprite_created = true;
		_sprite_content.create(_w, _h);
		last_drawn_playing = !carousel_playing; // force the first real draw below
	}

	// ui_screen only blits its composited buffer to the physical LCD when a
	// child's redraw() returns true (that return value becomes child_dirty,
	// which gates ui_screen::refresh()'s call to its own redraw()). This
	// widget was always returning false, so the icon's own off-screen
	// buffer was updating correctly on every tap, but the screen was never
	// told to actually show it - it only became visible on the rare
	// occasion something else on the same screen (e.g. the clock digits
	// changing) forced an unrelated full-screen redraw.
	//
	// Separately: the parent screen's own _sprite_content is fully released
	// and recreated blank every time it's navigated away from and back to,
	// even though this widget's own small sprite persists untouched. So
	// "state didn't change" isn't enough to skip redrawing - also redraw
	// whenever the parent's buffer address has changed since we last drew
	// into it, or this icon silently vanishes after a swipe away and back.
	void *parent_buffer = ui_parent->_sprite_content.getBuffer();
	bool parent_buffer_changed = (parent_buffer != last_parent_buffer);
	last_parent_buffer = parent_buffer;

	if (carousel_playing == last_drawn_playing && !parent_buffer_changed)
		return false;
	last_drawn_playing = carousel_playing;

	// Opaque background matching the clock screen's flat background color -
	// composited with no transparency key below so every redraw fully
	// overwrites the previous glyph instead of layering on top of it (a
	// transparent composite here left the old glyph's pixels showing
	// through, since only exact background-colored pixels got skipped).
	_sprite_content.fillRect(0, 0, _w, _h, dashboard_theme::background);

	int16_t cx = _w / 2;
	int16_t cy = _h / 2;

	if (carousel_playing)
	{
		// Pause glyph: two bars
		constexpr int16_t bar_w = 5, bar_h = 16, gap = 6;
		_sprite_content.fillRoundRect(cx - gap / 2 - bar_w, cy - bar_h / 2, bar_w, bar_h, 1, TFT_GREY);
		_sprite_content.fillRoundRect(cx + gap / 2, cy - bar_h / 2, bar_w, bar_h, 1, TFT_GREY);
	}
	else
	{
		// Play glyph: right-pointing triangle
		constexpr int16_t r = 9;
		_sprite_content.fillTriangle(cx - r / 2, cy - r, cx - r / 2, cy + r, cx + r, cy, TFT_GREY);
	}

	ui_parent->_sprite_content.drawSprite(_x, _y, &_sprite_content, 1.0f, -1);

	next_refresh = millis();
	is_dirty = false;

	return true;
}
