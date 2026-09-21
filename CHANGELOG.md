# Changelog

Notable changes in this fork, grouped by feature rather than by commit.

## Dashboard & Carousel

- Added three new full-screen cards beyond the stock shipping firmware: **Markets**, **Weather**, and **Calendar**, plus the original **Clock** screen, forming a swipe loop: Clock → Markets → Weather → Calendar → Clock → ...
- Added a play/pause control on the Clock screen. Tapping it auto-advances through the loop on a timer (first hop after ~2s so it's obvious it's running, then every 60s), using the same slide-transition animation a manual swipe uses (`ui_screen::animate_transition()`) rather than a hard screen-cut. Any touch elsewhere pauses it again.
- Added a `squixl-ota` PlatformIO environment (`pio run -e squixl-ota -t upload`) for pushing firmware over WiFi via `espota` once the device has been flashed once over USB.

## Clock

- The big clock digits are now drawn as a rainbow gradient by default: one hue sweep across the whole clock (no repeated colours), shifting a little every minute. `UM_GFX_Canvas::setRainbow()` colours antialiased glyph pixels from a 256-entry hue table inside `drawGlyphAA`. Pure magenta (`0xF81F`) is nudged off, since the screens use it as their transparent colour key.
- New "Clock Settings" group in the web portal: a SOLID/RAINBOW toggle and a full colour picker for the solid colour used when the rainbow is off.

## Markets Card

- Up to 6 ticker/label pairs, configurable from the web portal's "Markets Settings" group, live-reloading without a reboot when saved.
- Fetches quotes from Yahoo Finance's unauthenticated "spark" endpoint.

## Calendar Card ("Agenda")

- Shows the next 6 upcoming events from a calendar's `.ics` feed, configured via the web portal's "Calendar Settings" group (also live-reloading on save).
- Ships with a small on-device ICS parser (`widget_calendar.cpp`) with two accepted simplifications: recurring events show only at their original `DTSTART` (no `RRULE` expansion), and times are read as-is with no timezone conversion.
- The fetch is capped at 48KB and independently bounded by a 10-second wall-clock deadline (`WifiController::http_request()`'s new `max_response_bytes` parameter) - a full calendar export can run into the megabytes and isn't ordered chronologically, which both caused a real production issue (see below) and doesn't reliably surface only the *next* few events anyway.
- **Recommended source**: rather than pointing this at your main calendar's own secret `.ics` URL (which can carry years of history), deploy the small Google Apps Script in `platformio/docs/google_apps_script_calendar_feed.js` as a Web App. It reads your calendar server-side and returns only the next few events as a tiny, pre-filtered feed - keeping your real calendar private and avoiding the size/ordering problems entirely. See that file for setup instructions.
- HTTP requests now follow redirects (`HTTPC_FORCE_FOLLOW_REDIRECTS`), which the Apps Script Web App source requires (it responds via a 302 to a `script.googleusercontent.com` URL).

### Incident: boot loop from an oversized calendar fetch

An early version fetched a calendar's full `.ics` export directly, which was 1.4MB - large enough that `HTTPClient::getString()` blocked inside a TLS socket read (`NetworkClientSecure::read()` → `mbedtls_ssl_read()`) long enough to trip the task watchdog and reboot the device. Because the fetch runs automatically at boot once WiFi connects, this repeated on every reboot. Fixed by adding a capped, poll-based read path (only ever reading what `Stream::available()` already reports as buffered, never a blocking read for more) and recommending the Apps Script feed above instead of a raw calendar export.

## Framework fixes made along the way

These affect any future widget/screen work, not just the features above (also recorded in this session's saved memory for future SQUiXL work):

- **A widget's `redraw()` must return `true` when it changes visible content.** A screen only blits itself to the physical LCD when a child's `redraw()` returns `true` - returning `false` unconditionally (an easy mistake to make) leaves the widget's own buffer updated but invisible until something else forces a full-screen repaint.
- **`should_refresh()` never fires again after the first frame unless `set_refresh_interval()` is set to a nonzero value**, regardless of `is_dirty`.
- **`squixl.process_touch_full()`'s return value doesn't mean "a fresh touch happened."** It's `true` for a touch's entire lifecycle (down, held, and the ~80-400ms deferred single-tap-vs-double-tap window after release), not just its start. Added public `squixl.is_touch_down()` and `squixl.get_currently_selected()` accessors so callers can watch the down-edge and identify what was touched instead.
- **A screen's buffers are fully released and recreated blank every time it's navigated away from and back to.** A widget that skips redrawing when its own state hasn't changed needs an independent, reliable signal for "my parent's canvas was just wiped" - comparing the parent's buffer pointer isn't reliable (PSRAM commonly hands back the same address for a same-size free-then-realloc); tracking the actual screen-transition event in `main.cpp`'s `loop()` is.
- **`set_current_screen()` alone doesn't guarantee a repaint** if the destination screen's children have no new data to show since their last visit. Added `ui_screen::animate_transition()`, which plays the real swipe animation and handles the forced repaint as part of it.
- Bounded the weather card's icon cache, which never evicted old entries and could grow to ~90KB over the device's uptime - a contributor to PSRAM fragmentation tight enough to fail a full-screen (~460KB) buffer allocation during a swipe.
