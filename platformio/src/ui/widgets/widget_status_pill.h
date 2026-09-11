#pragma once

#include "ui/ui_element.h"

// iPhone-style status readout: WiFi signal bars + battery pill, right-aligned
// to a given x (matching iOS's status bar order: signal, then battery, ending
// at the screen edge). Meant to float in the top-right of a screen.
class widgetStatusPill : public ui_element
{
	public:
		// right_edge_x: the x-coordinate the whole readout's right edge should land on.
		void create(int16_t right_edge_x, int16_t y);

		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

	private:
		void draw_wifi_bars(int16_t x);
		void draw_battery(int16_t x);
};
