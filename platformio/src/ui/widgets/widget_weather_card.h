#pragma once

#include "ui/ui_window.h"
#include "ui/icons/images/weather/um_ow_01d.h"
#include "ui/icons/images/weather/um_ow_02d.h"
#include "ui/icons/images/weather/um_ow_03d.h"
#include "ui/icons/images/weather/um_ow_04d.h"
#include "ui/icons/images/weather/um_ow_09d.h"
#include "ui/icons/images/weather/um_ow_10d.h"
#include "ui/icons/images/weather/um_ow_11d.h"
#include "ui/icons/images/weather/um_ow_13d.h"
#include "ui/icons/images/weather/um_ow_50d.h"
#include "ui/icons/images/weather/um_ow_01n.h"
#include "ui/icons/images/weather/um_ow_02n.h"

#include <map>
#include <vector>

struct DayForecast
{
		std::string label; // "Today", "Thu", "Fri", ...
		int16_t low = 999;
		int16_t high = -999;
		int precip_pct = 0;
		String icon_name;
		bool has_data = false;
};

// 5-day forecast list card (day / icon / precip% / low / range bar / high),
// styled to match widgetStockList. Reads city/country/API key from settings
// live each call - the on-device Location and OpenWeather settings panels
// already exist for configuring these, no code change needed to repoint it.
class widgetWeatherCard : public ui_window
{
	public:
		bool redraw(uint8_t fade_amount, int8_t tab_group = -1) override;
		bool process_touch(touch_event_t touch_event) override;

		void process_forecast_data(bool success, const String &response);

		// Kicks off the first fetch as soon as WiFi connects at boot, rather
		// than waiting for this widget's screen to become active.
		void prefetch();

	private:
		std::vector<DayForecast> days;

		unsigned long next_update = 0;
		unsigned long fetch_started_at = 0;
		bool is_fetching = false;
		bool has_data = false;
		bool should_redraw = false;

		std::map<String, umgfx::UM_GFX_Canvas> icons;

		std::string build_url();
		void load_icon(const String &name);
		void maybe_fetch();
};
