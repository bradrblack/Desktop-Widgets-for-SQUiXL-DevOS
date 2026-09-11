#include "ui/widgets/widget_status_pill.h"

#include "ui/theme_dashboard.h"
#include "peripherals/battery.h"
#include "web/wifi_controller.h"
#include <WiFi.h>

namespace
{
	constexpr int16_t WIFI_BAR_W = 4;
	constexpr int16_t WIFI_BAR_GAP = 2;
	constexpr int16_t WIFI_BAR_HEIGHTS[4] = {6, 10, 14, 18};
	constexpr int16_t WIFI_BLOCK_W = 4 * WIFI_BAR_W + 3 * WIFI_BAR_GAP;
	constexpr int16_t WIFI_BLOCK_H = 20;

	constexpr int16_t BODY_W = 40;
	constexpr int16_t BODY_H = 18;
	constexpr int16_t NUB_W = 3;
	constexpr int16_t NUB_H = 8;
	constexpr int16_t INSET = 3;
	constexpr int16_t BATTERY_BLOCK_W = BODY_W + NUB_W;

	constexpr int16_t GAP = 14;

	int rssi_to_bars(int32_t rssi)
	{
		if (rssi >= -50)
			return 4;
		if (rssi >= -60)
			return 3;
		if (rssi >= -70)
			return 2;
		if (rssi >= -80)
			return 1;
		return 0;
	}
}

void widgetStatusPill::create(int16_t right_edge_x, int16_t y)
{
	_w = WIFI_BLOCK_W + GAP + BATTERY_BLOCK_W;
	_h = max(WIFI_BLOCK_H, (int16_t)22);
	_x = right_edge_x - _w;
	_y = y;

	_sprite_content.create(_w, _h);
}

void widgetStatusPill::draw_wifi_bars(int16_t x)
{
	if (!wifi_controller.is_connected())
		return;

	int bars = rssi_to_bars(WiFi.RSSI());

	for (int i = 0; i < 4; i++)
	{
		int16_t bar_h = WIFI_BAR_HEIGHTS[i];
		int16_t bar_x = x + i * (WIFI_BAR_W + WIFI_BAR_GAP);
		int16_t bar_y = WIFI_BLOCK_H - bar_h;
		uint16_t color = (i < bars) ? dashboard_theme::text_primary : dashboard_theme::text_secondary;
		_sprite_content.fillRoundRect(bar_x, bar_y, WIFI_BAR_W, bar_h, 1, color);
	}
}

void widgetStatusPill::draw_battery(int16_t x)
{
	int percent = constrain((int)battery.get_percent(), 0, 100);
	bool charging = squixl.vbus_present();

	int16_t body_y = (_h - BODY_H) / 2;
	_sprite_content.drawRoundRect(x, body_y, BODY_W, BODY_H, 4, dashboard_theme::text_primary);
	_sprite_content.fillRoundRect(x + BODY_W, body_y + (BODY_H - NUB_H) / 2, NUB_W, NUB_H, 1, dashboard_theme::text_primary);

	uint16_t fill_color = charging ? dashboard_theme::accent_teal : (percent <= 10 ? dashboard_theme::negative : (percent <= 20 ? dashboard_theme::accent_amber : dashboard_theme::positive));

	int16_t fill_w = (int16_t)((BODY_W - 2 * INSET) * (percent / 100.0f));
	if (fill_w > 0)
		_sprite_content.fillRoundRect(x + INSET, body_y + INSET, fill_w, BODY_H - 2 * INSET, 2, fill_color);
}

bool widgetStatusPill::redraw(uint8_t fade_amount, int8_t tab_group)
{
	if (millis() < delay_first_draw)
		return false;

	if (is_busy)
		return false;

	is_busy = true;

	_sprite_content.fillRect(0, 0, _w, _h, TFT_MAGENTA);

	draw_wifi_bars(0);
	draw_battery(WIFI_BLOCK_W + GAP);

	ui_parent->_sprite_content.drawSprite(_x, _y, &_sprite_content, 1.0f, TFT_MAGENTA);
	next_refresh = millis();

	is_dirty = false;
	is_busy = false;

	return true;
}

bool widgetStatusPill::process_touch(touch_event_t touch_event)
{
	return false;
}
