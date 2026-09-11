#include "web/wifi_ota.h"
#include "settings/settings_async.h"
#include "squixl.h"
#include "fonts/ubuntu_mono_all_b.h"

namespace
{
	constexpr int16_t SCREEN_W = 480;

	constexpr int16_t BAR_W = 360;
	constexpr int16_t BAR_H = 32;
	constexpr int16_t BAR_X = (SCREEN_W - BAR_W) / 2;
	constexpr int16_t BAR_Y = 270;

	constexpr int16_t HEADING_Y = 190;

	constexpr int16_t PERCENT_Y = BAR_Y + BAR_H + 60;
	constexpr int16_t PERCENT_BOX_W = 200;
	constexpr int16_t PERCENT_BOX_X = (SCREEN_W - PERCENT_BOX_W) / 2;
	constexpr int16_t PERCENT_BOX_Y = PERCENT_Y - 40;
	constexpr int16_t PERCENT_BOX_H = 54;

	// Heading and percent share this font/size, per request.
	const GFXfont *STATUS_FONT = UbuntuMono_B[3];

	void draw_centered(const char *text, int16_t baseline_y, uint16_t color)
	{
		int16_t tx, ty;
		uint16_t tw, th;
		squixl.lcd.setFreeFont(STATUS_FONT);
		squixl.lcd.getTextBounds(text, 0, 0, &tx, &ty, &tw, &th);
		squixl.lcd.setTextColor(color, TFT_BLACK);
		squixl.lcd.setCursor((SCREEN_W - (int16_t)tw) / 2, baseline_y);
		squixl.lcd.print(text);
	}

	void draw_bar(int percent)
	{
		squixl.lcd.fillRoundRect(BAR_X, BAR_Y, BAR_W, BAR_H, BAR_H / 2, TFT_GREY);

		int16_t fill_w = (int16_t)(BAR_W * constrain(percent, 0, 100) / 100.0f);
		if (fill_w > BAR_H)
			squixl.lcd.fillRoundRect(BAR_X, BAR_Y, fill_w, BAR_H, BAR_H / 2, TFT_GREEN);
	}

	void draw_percent(int percent)
	{
		// Clear a fixed box before redrawing so "9%" -> "100%" never leaves
		// ghost pixels from the wider/narrower previous string.
		squixl.lcd.fillRect(PERCENT_BOX_X, PERCENT_BOX_Y, PERCENT_BOX_W, PERCENT_BOX_H, TFT_BLACK);

		char buf[6];
		snprintf(buf, sizeof(buf), "%d%%", constrain(percent, 0, 100));
		draw_centered(buf, PERCENT_Y, TFT_WHITE);
	}

	// Draws the parts of the screen that don't change during the transfer.
	// Called once (onStart/onError) - onProgress only touches the bar/percent
	// afterwards, so the heading never gets flicker-redrawn on every tick.
	void draw_ota_static(const char *heading, uint16_t heading_color, int percent)
	{
		squixl.lcd.fillScreen(TFT_BLACK);
		draw_centered(heading, HEADING_Y, heading_color);
		draw_bar(percent);
		draw_percent(percent);
		squixl.lcd.force_cache_write();
	}

	void update_ota_progress(int percent)
	{
		draw_bar(percent);
		draw_percent(percent);
		squixl.lcd.force_cache_write();
	}
}

void start_ota()
{
	ArduinoOTA.setHostname(settings.config.mdns_name.c_str());
	ArduinoOTA
		.onStart([]() {
			is_ota_updating = true;

			String type;
			if (ArduinoOTA.getCommand() == U_FLASH)
				type = "sketch";
			else // U_SPIFFS
				type = "filesystem";

			// NOTE: if updating SPIFFS this would be the place to unmount SPIFFS
			// using SPIFFS.end()
			Serial.println("Start updating " + type);

			// Full brightness for the duration of the update - the normal idle
			// dimmer never gets a chance to run while ArduinoOTA blocks the loop,
			// so whatever brightness was set before the update just persists otherwise.
			squixl.set_backlight_level(100.0f);

			draw_ota_static("UPDATING FIRMWARE", TFT_WHITE, 0);
		})
		.onEnd([]() {
			Serial.println("\nEnd");
			draw_ota_static("REBOOTING...", TFT_WHITE, 100);
			is_ota_updating = false;
		})
		.onProgress([](unsigned int progress, unsigned int total) {
			static int last_percent = -1;
			int percent = total > 0 ? (int)((progress * 100) / total) : 0;

			// Only redraw when the displayed percentage actually changes -
			// onProgress fires once per chunk (can be 1000+ times for a
			// multi-MB image) and each redraw+flush costs real time that
			// would otherwise slow the transfer down.
			if (percent != last_percent)
			{
				last_percent = percent;
				update_ota_progress(percent);
			}
		})
		.onError([](ota_error_t error) {
			Serial.printf("Error[%u]: ", error);
			const char *msg = "UPDATE FAILED";
			if (error == OTA_AUTH_ERROR)
				msg = "AUTH FAILED";
			else if (error == OTA_BEGIN_ERROR)
				msg = "BEGIN FAILED";
			else if (error == OTA_CONNECT_ERROR)
				msg = "CONNECT FAILED";
			else if (error == OTA_RECEIVE_ERROR)
				msg = "RECEIVE FAILED";
			else if (error == OTA_END_ERROR)
				msg = "END FAILED";
			Serial.println(msg);

			draw_ota_static(msg, TFT_RED, 0);
			is_ota_updating = false;
		});

	ArduinoOTA.begin();
}
