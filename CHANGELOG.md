# Changelog

Notable changes in this fork, grouped by feature rather than by commit.

## Dashboard & Carousel

- Added four new full-screen cards beyond the stock shipping firmware: **Markets**, **Weather**, **Calendar**, and **News**, plus the original **Clock** screen, forming a swipe loop: Clock → Markets → Weather → Calendar → News → Clock → ...
- Added a play/pause control on the Clock screen that starts/stops carousel auto-advance, using the same slide-transition animation a manual swipe uses (`ui_screen::animate_transition()`) rather than a hard screen-cut. Any touch elsewhere pauses it again.
- Auto-swipe is now **on by default**, with a full dwell on the clock before the first hop. The dwell duration is configurable from the web portal's "Auto-Swipe Settings" group (default 60s, 5-600s range) instead of being hard-coded.
- Tidied the portal's Widgets tab: hid the unused RSS Feed and Expansion Settings groups (not used by this fork) rather than removing them outright, since their option indexes are still relied on elsewhere.
- Added a `squixl-ota` PlatformIO environment (`pio run -e squixl-ota -t upload`) for pushing firmware over WiFi via `espota` once the device has been flashed once over USB.

## Clock

- The big clock digits are now drawn as a rainbow gradient by default: a gentle gradient covering about half the hue wheel across the clock (no repeated colours), shifting a little every minute. `UM_GFX_Canvas::setRainbow()` colours antialiased glyph pixels from a 256-entry hue table inside `drawGlyphAA`. Pure magenta (`0xF81F`) is nudged off, since the screens use it as their transparent colour key.
- New "Clock Settings" group in the web portal: a SOLID/RAINBOW toggle and a full colour picker for the solid colour used when the rainbow is off.
- Digits are rendered natively at 88pt with real antialiasing (`UM_GFX_Canvas::drawGlyphAA()`, blending each glyph's 8-bit coverage into the background colour) instead of the previous approach of drawing a smaller bitmap font and upscaling it 2x, which looked blocky.
- Centered vertically around its configured midpoint instead of being anchored by its top edge, so changing the font size no longer requires re-tuning a manual offset.
- Fixed the 12-hour display showing "0:mm" instead of "12:mm" at midnight - hour 0 wasn't being converted to 12 (only `hour > 12` was handled).

## Antialiased Text on the Dashboard Cards

Markets, Weather, Calendar, and News now render all their text antialiased too, the same way the clock does, instead of the original 1-bit fonts. Since the clock's own AA font only covered the 11 characters it needed (digits and `:`), extending this meant generating full antialiased versions of the two Ubuntu Mono Bold sizes the cards actually use (14pt and 18pt, covering the same 0x20-0xB0 range as the existing 1-bit fonts) plus the small 7pt Regular size Weather uses for precipitation percentages - `ubuntu_mono_bold_14pt_aa.h`, `_18pt_aa.h`, and `ubuntu_mono_regular_7pt_aa.h`.

**How they were generated**, since no font-generation tool or source font file was checked into this repo:

1. Downloaded the exact source TTFs (`UbuntuMono-Bold.ttf`, `UbuntuMono-Regular.ttf`) from Google's official fonts repo - Ubuntu Mono is open-source (Ubuntu Font License), and it's the same family already used everywhere in this firmware.
2. Reverse-engineered the DPI the existing fonts were built at: each existing font's `yAdvance` matches `round(point_size * 141 / 72)` exactly (e.g. 18pt -> 35), confirming 141 DPI - the same number already noted in the clock's own AA font header comment.
3. Wrote a small Python script (`freetype-py`) that replicates the relevant part of Adafruit's `fontconvert.c`, but using FreeType's default antialiased rendering instead of `FT_LOAD_TARGET_MONO`, emitting each glyph's 8-bit coverage bitmap (one byte per pixel, row-major, no bit-packing) plus its `GFXglyph` metrics in the exact header format `UM_GFX_Canvas::drawGlyphAA()` and the existing font files already use.
4. Validated the output before trusting it: the generated fonts' advance widths and glyph dimensions matched the existing 1-bit fonts at the same point size almost exactly (a pixel or two of difference here and there, expected from AA vs. mono hinting) - confirming the source file and DPI math were right before wiring the fonts into any widget.

This cost about 250KB of flash (66.8% to 72.1% used, 1.3MB still free) - more than the ~86KB the three new fonts' raw data would suggest, because each of the four widgets ends up compiling its own private copy of whichever font(s) it references (internal linkage, no dedup across translation units at link time). That turned out to be a pre-existing pattern already true of the original 1-bit fonts (confirmed via `nm` on the build's object files) - this change just made each duplicate ~8x more expensive, since 8-bit coverage data is 8x the size of 1-bit packed data per glyph. Not worth restructuring given the flash headroom left.

## Theme

- The shared dashboard background is now near-black (`dashboard_theme::background`, about RGB 8,8,8) instead of muted slate.

## Markets Card

- Up to 6 ticker/label pairs, configurable from the web portal's "Markets Settings" group, live-reloading without a reboot when saved.
- Fetches quotes from Yahoo Finance's unauthenticated "spark" endpoint.

## Weather Card

- 5-day forecast list styled to match the Markets card, reading city/country/API key live from the existing Location and OpenWeather settings.
- Fixed forecast entries being bucketed by OpenWeather's raw UTC date instead of the location's local date: a forecast fetched late enough in the UTC day had its first entries already dated UTC-tomorrow, which got unconditionally labeled "Today" and swallowed what should have been its own card - making the following day look like it arrived a weekday early. Entries are now shifted by the location's configured UTC offset before bucketing, and a bucket is only labeled "Today" if its resulting local date actually matches the RTC's current date; the "representative icon" pick for each day was fixed the same way (local noon instead of UTC noon).

## Calendar Card ("Agenda")

- Shows the next 6 upcoming events from a calendar's `.ics` feed, configured via the web portal's "Calendar Settings" group (also live-reloading on save).
- Ships with a small on-device ICS parser (`widget_calendar.cpp`) with two accepted simplifications: recurring events show only at their original `DTSTART` (no `RRULE` expansion), and times are read as-is with no timezone conversion.
- The fetch is capped at 48KB and independently bounded by a 10-second wall-clock deadline (`WifiController::http_request()`'s new `max_response_bytes` parameter) - a full calendar export can run into the megabytes and isn't ordered chronologically, which both caused a real production issue (see below) and doesn't reliably surface only the *next* few events anyway.
- **Recommended source**: rather than pointing this at your main calendar's own secret `.ics` URL (which can carry years of history), deploy the small Google Apps Script in `platformio/docs/google_apps_script_calendar_feed.js` as a Web App. It reads your calendar server-side and returns only the next few events as a tiny, pre-filtered feed - keeping your real calendar private and avoiding the size/ordering problems entirely. See that file for setup instructions.
- HTTP requests now follow redirects (`HTTPC_FORCE_FOLLOW_REDIRECTS`), which the Apps Script Web App source requires (it responds via a 302 to a `script.googleusercontent.com` URL).

### Incident: boot loop from an oversized calendar fetch

An early version fetched a calendar's full `.ics` export directly, which was 1.4MB - large enough that `HTTPClient::getString()` blocked inside a TLS socket read (`NetworkClientSecure::read()` → `mbedtls_ssl_read()`) long enough to trip the task watchdog and reboot the device. Because the fetch runs automatically at boot once WiFi connects, this repeated on every reboot. Fixed by adding a capped, poll-based read path (only ever reading what `Stream::available()` already reports as buffered, never a blocking read for more) and recommending the Apps Script feed above instead of a raw calendar export.

## News Card

- Shows one random headline at a time from the NYT Top Stories API's "home" section, word-wrapped and centered in the Markets card's font. Configured via the web portal's "News Settings" group (an NYT API key field from developer.nytimes.com, live-reloading on save).
- The story collection refreshes every 30 minutes. The endpoint has no field-filter or count-limit query param, so the full response (mostly per-story image-variant arrays this widget never reads) is fetched every time; a SAX parser (same technique as the Weather card) keeps only each story's `title` string in memory, and the fetch is capped at 750KB and routed through the same polling-read path Calendar uses, to avoid a repeat of the boot-loop incident described above.
- Tapping the card shuffles to a new random headline immediately (same 700ms double-tap cooldown pattern as the play/pause control). It also re-picks automatically every 60s while left idle on-screen, and once on every fresh visit to the screen.
- NYT's UTF-8 "smart" punctuation (curly quotes, em/en dashes, ellipses) is sanitized to plain ASCII before display - this device's fonts draw one byte per glyph with no UTF-8 awareness, so left as-is, a curly apostrophe's middle byte rendered as a Euro sign instead.

## PSRAM fragmentation: freeing widget buffers on navigation

Adding News as a fourth permanently-resident card widget exposed a pre-existing architectural gap: every `ui_window`-based card (Markets, Weather, Calendar, News) allocated its 4 full-size sprite buffers once and never released them for the rest of the widget's lifetime, whether or not its screen was ever shown. With four such widgets resident, there was too little contiguous PSRAM left for two neighbouring screens' own buffers to coexist during a swipe - confirmed via serial log: `FAILED to allocate _sprite_back (480x480) - largest PSRAM chunk free=458740`, just under the ~460800 bytes needed, causing blank/grey screens and laggy swiping on effectively every navigation (not just to News).

`ui_screen` already freed/recreated its own buffers on exactly this kind of event and already cascaded `about_to_show_screen()`/`about_to_close_screen()` to every child widget on every navigation - but `ui_window` had never overridden either (both were silent no-op stubs inherited from `ui_element`), so those calls fired on every widget and did nothing. Giving `ui_window` real implementations (release all 4 sprites on close; recreate them and force a full repaint on show) fixed the fragmentation without changing any call sites, chased through a few follow-on side effects along the way:

- A freshly-recreated buffer alone didn't get `redraw()` called soon enough - `should_refresh()`'s plain timer check had no idea a hard refresh was overdue, leaving cards sitting blank/grey for up to their full refresh interval (~2s) after every navigation. Fixed by resetting `next_refresh` to 0 when a buffer is recreated.
- The live drag-preview (which force-redraws the incoming screen immediately for the sliding-in animation) ran before a card's buffers had been recreated, showing grey until the swipe fully committed. Fixed by calling `about_to_show_screen()` up front in `setup_draggable_neighbour(true)` too, with matching `about_to_close_screen()` cleanup on both committed and cancelled drags so an aborted swipe doesn't leak a neighbour's buffers.
- That fix then kept both the outgoing and incoming screen's widgets fully buffered at once during every drag, which doubled peak PSRAM pressure enough to fail the screen's own drag-visual buffer instead (`_sprite_drag`, serial log: largest free chunk down to 311284 bytes) - swipes from Markets to Weather would occasionally stick with no motion. Root cause: all four card widgets fully override `redraw()` and only ever draw into the *screen's* `_sprite_content`, never their own individually-inherited one - a genuinely dead ~387KB per widget (~1.5MB across all four). Added `ui_window::needs_own_content_sprite()` (defaults `true`, preserving existing behaviour for anything relying on `ui_window`'s own default `redraw()`, e.g. `widget_openweather`) and overrode it to return `false` on the four card widgets, so that buffer is never allocated for them at all.
- Two headline-picking triggers (a data refresh and screen-entry) could land within a second of each other, showing as one headline flickering to another. Moved the "fresh arrival" pick to key off `is_dirty_hard` (set the instant a card's buffers are freshly recreated, before any `redraw()` runs) instead of a separate `on_screen_shown()` hook called later in `loop()`, which could race behind the drag-preview's own forced redraws.

## Framework fixes made along the way

These affect any future widget/screen work, not just the features above (also recorded in this session's saved memory for future SQUiXL work):

- **A widget's `redraw()` must return `true` when it changes visible content.** A screen only blits itself to the physical LCD when a child's `redraw()` returns `true` - returning `false` unconditionally (an easy mistake to make) leaves the widget's own buffer updated but invisible until something else forces a full-screen repaint.
- **`should_refresh()` never fires again after the first frame unless `set_refresh_interval()` is set to a nonzero value**, regardless of `is_dirty`.
- **`squixl.process_touch_full()`'s return value doesn't mean "a fresh touch happened."** It's `true` for a touch's entire lifecycle (down, held, and the ~80-400ms deferred single-tap-vs-double-tap window after release), not just its start. Added public `squixl.is_touch_down()` and `squixl.get_currently_selected()` accessors so callers can watch the down-edge and identify what was touched instead.
- **A screen's buffers are fully released and recreated blank every time it's navigated away from and back to.** A widget that skips redrawing when its own state hasn't changed needs an independent, reliable signal for "my parent's canvas was just wiped" - comparing the parent's buffer pointer isn't reliable (PSRAM commonly hands back the same address for a same-size free-then-realloc); tracking the actual screen-transition event in `main.cpp`'s `loop()` is.
- **`set_current_screen()` alone doesn't guarantee a repaint** if the destination screen's children have no new data to show since their last visit. Added `ui_screen::animate_transition()`, which plays the real swipe animation and handles the forced repaint as part of it.
- Bounded the weather card's icon cache, which never evicted old entries and could grow to ~90KB over the device's uptime - a contributor to PSRAM fragmentation tight enough to fail a full-screen (~460KB) buffer allocation during a swipe.
