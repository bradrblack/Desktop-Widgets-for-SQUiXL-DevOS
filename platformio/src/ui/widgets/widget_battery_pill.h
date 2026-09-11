#pragma once

#include "ui/ui_element.h"

// Small iPhone-style battery pill (rounded outline + nub + proportional fill
// + percent text) meant to float in the top-left corner of the dashboard
// screens. Deliberately minimal - no wifi/IP/voltage debug info like the
// original widgetBattery.
class widgetBatteryPill : public ui_element
{
	public:
		void create(int16_t x, int16_t y);

		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;
};
