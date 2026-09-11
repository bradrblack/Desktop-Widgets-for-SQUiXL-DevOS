#include "ui/widgets/widget_battery_pill.h"

#include "ui/theme_dashboard.h"
#include "peripherals/battery.h"

namespace
{
	constexpr int16_t BODY_W = 40;
	constexpr int16_t BODY_H = 18;
	constexpr int16_t NUB_W = 3;
	constexpr int16_t NUB_H = 8;
	constexpr int16_t INSET = 3;
}

void widgetBatteryPill::create(int16_t x, int16_t y)
{
	_x = x;
	_y = y;
	_w = BODY_W + NUB_W + 44; // room for the "100%" label
	_h = 22;

	_sprite_content.create(_w, _h);
}

bool widgetBatteryPill::redraw(uint8_t fade_amount, int8_t tab_group)
{
	if (millis() < delay_first_draw)
		return false;

	if (is_busy)
		return false;

	is_busy = true;

	int percent = constrain((int)battery.get_percent(), 0, 100);
	bool charging = squixl.vbus_present();

	_sprite_content.fillRect(0, 0, _w, _h, TFT_MAGENTA);

	int16_t body_y = (_h - BODY_H) / 2;
	_sprite_content.drawRoundRect(0, body_y, BODY_W, BODY_H, 4, dashboard_theme::text_primary);
	_sprite_content.fillRoundRect(BODY_W, body_y + (BODY_H - NUB_H) / 2, NUB_W, NUB_H, 1, dashboard_theme::text_primary);

	uint16_t fill_color = charging ? dashboard_theme::accent_teal : (percent <= 10 ? dashboard_theme::negative : (percent <= 20 ? dashboard_theme::accent_amber : dashboard_theme::positive));

	int16_t fill_w = (int16_t)((BODY_W - 2 * INSET) * (percent / 100.0f));
	if (fill_w > 0)
		_sprite_content.fillRoundRect(INSET, body_y + INSET, fill_w, BODY_H - 2 * INSET, 2, fill_color);

	char buf[6];
	snprintf(buf, sizeof(buf), "%d%%", percent);
	_sprite_content.setFreeFont(UbuntuMono_R[1]);
	_sprite_content.setTextColor(dashboard_theme::text_primary, TFT_MAGENTA);
	_sprite_content.setCursor(BODY_W + NUB_W + 8, _h - 5);
	_sprite_content.print(buf);

	ui_parent->_sprite_content.drawSprite(_x, _y, &_sprite_content, 1.0f, TFT_MAGENTA);
	next_refresh = millis();

	is_dirty = false;
	is_busy = false;

	return true;
}

bool widgetBatteryPill::process_touch(touch_event_t touch_event)
{
	return false;
}
