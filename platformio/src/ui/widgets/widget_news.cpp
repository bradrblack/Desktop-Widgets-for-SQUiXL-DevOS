#include "ui/widgets/widget_news.h"

#include "fonts/ubuntu_mono_bold_14pt_aa.h"
#include "fonts/ubuntu_mono_bold_18pt_aa.h"
#include "ui/theme_dashboard.h"

#include <sstream>

using json = nlohmann::json;

namespace
{
	// The Top Stories endpoint returns the whole section on every call - no
	// query param narrows the result count or fields - so this stays gentle.
	constexpr unsigned long REFRESH_INTERVAL_MS = 1800000; // 30 min
	// Until the first fetch ever succeeds, retry much sooner.
	constexpr unsigned long RETRY_INTERVAL_MS = 15000; // 15 sec

	// While parked on this screen (redraw() only ever runs for the current
	// screen's children), rotate to another random headline on this cadence
	// independent of the global carousel's own auto-advance timer/state.
	constexpr unsigned long ROTATE_INTERVAL_MS = 60000; // 60 sec

	// A "1000+ line" NYT home-section response is mostly per-story
	// `multimedia` arrays (many image-format variants per story) that this
	// widget never reads - real-world size is roughly 150-400KB, well under
	// the ~1.4MB calendar export that used to trip the task watchdog. Passing
	// any nonzero max_response_bytes routes the fetch through
	// WifiController's capped/polling read (see wifi_controller.cpp) instead
	// of the blocking http.getString() call that caused that crash - that
	// codepath, not the exact byte count, is what actually keeps this safe.
	// The cap itself is set generously above the expected size so a normal
	// response is never truncated; it only exists to bound the worst case.
	constexpr size_t NEWS_MAX_RESPONSE_BYTES = 750000; // 750KB

	// NYT headlines are UTF-8 and lean heavily on "smart" typographic
	// punctuation (curly quotes, em/en dashes, ellipsis) - this device's
	// bitmap fonts only cover a single-byte range with no UTF-8 awareness,
	// so each byte of a multi-byte sequence gets drawn as its own separate
	// glyph. In particular a UTF-8 apostrophe (U+2019, bytes E2 80 99) drew
	// its middle byte (0x80) as this device's font's Euro sign glyph, since
	// 0x80 is also where the Euro sign sits in the single-byte Windows-1252
	// codepage many such fonts otherwise follow. Map the common cases to a
	// plain ASCII equivalent, and drop any other multi-byte UTF-8 sequence
	// entirely (accented letters, symbols, emoji, ...) rather than let its
	// raw bytes render as further mojibake.
	std::string sanitize_headline_text(const std::string &in)
	{
		std::string out;
		out.reserve(in.size());

		for (size_t i = 0; i < in.size();)
		{
			unsigned char c = (unsigned char)in[i];

			if (c < 0x80)
			{
				out += (char)c;
				i++;
				continue;
			}

			if (c == 0xE2 && i + 2 < in.size())
			{
				unsigned char b1 = (unsigned char)in[i + 1];
				unsigned char b2 = (unsigned char)in[i + 2];
				if (b1 == 0x80)
				{
					if (b2 == 0x98 || b2 == 0x99) // ‘ ’
					{
						out += '\'';
						i += 3;
						continue;
					}
					if (b2 == 0x9C || b2 == 0x9D) // “ ”
					{
						out += '"';
						i += 3;
						continue;
					}
					if (b2 == 0x93 || b2 == 0x94) // – —
					{
						out += '-';
						i += 3;
						continue;
					}
					if (b2 == 0xA6) // …
					{
						out += "...";
						i += 3;
						continue;
					}
				}
			}

			size_t seq_len = 1;
			if ((c & 0xE0) == 0xC0)
				seq_len = 2;
			else if ((c & 0xF0) == 0xE0)
				seq_len = 3;
			else if ((c & 0xF8) == 0xF0)
				seq_len = 4;

			i += seq_len;
		}

		return out;
	}

	// Streaming (SAX) parser that keeps only each story's "title" string,
	// ignoring everything else (byline, abstract, url, multimedia image
	// variants, facets, ...) - see widget_news.h for why that matters here.
	class NewsSax : public json::json_sax_t
	{
		public:
			explicit NewsSax(std::vector<std::string> &out) : titles(out) {}

			bool ok = true;

			bool null() override { return true; }
			bool boolean(bool) override { return true; }
			bool binary(json::binary_t &) override { return true; }
			bool number_integer(json::number_integer_t) override { return true; }
			bool number_unsigned(json::number_unsigned_t) override { return true; }
			bool number_float(json::number_float_t, const json::string_t &) override { return true; }

			bool string(json::string_t &val) override
			{
				if (in_story && story_depth == 0 && pending_key == "title")
					titles.push_back(sanitize_headline_text(val));
				return true;
			}

			bool start_object(std::size_t) override
			{
				if (in_results_array && !in_story)
					in_story = true;
				else if (in_story)
					story_depth++;
				return true;
			}

			bool end_object() override
			{
				if (in_story)
				{
					if (story_depth > 0)
						story_depth--;
					else
						in_story = false;
				}
				return true;
			}

			bool start_array(std::size_t) override
			{
				if (!in_story && !in_results_array && pending_key == "results")
					in_results_array = true;
				else if (in_story)
					story_array_depth++;
				return true;
			}

			bool end_array() override
			{
				if (in_story && story_array_depth > 0)
					story_array_depth--;
				else if (in_results_array && !in_story)
					in_results_array = false;
				return true;
			}

			bool key(json::string_t &val) override
			{
				pending_key = val;
				return true;
			}

			bool parse_error(std::size_t, const std::string &, const json::exception &ex) override
			{
				Serial.print("NewsCard SAX parse error: ");
				Serial.println(ex.what());
				ok = false;
				return false;
			}

		private:
			std::vector<std::string> &titles;
			bool in_results_array = false;
			bool in_story = false;
			int story_depth = 0;
			int story_array_depth = 0;
			std::string pending_key;
	};
}

std::vector<std::string> widgetNews::wrap_text(const std::string &text, const GFXfont *font, int16_t max_width)
{
	std::vector<std::string> lines;
	std::istringstream words(text);
	std::string word, line;
	int text_w, text_h;

	while (words >> word)
	{
		std::string candidate = line.empty() ? word : line + " " + word;
		calc_text_size(candidate.c_str(), font, &text_w, &text_h);
		if (text_w > max_width && !line.empty())
		{
			lines.push_back(line);
			line = word;
		}
		else
		{
			line = candidate;
		}
	}
	if (!line.empty())
		lines.push_back(line);

	return lines;
}

void widgetNews::maybe_fetch()
{
	// Matches the header font's char metrics (index 3, same font this card
	// draws headlines in) - cached once since get_cached_char_sizes() is
	// looked up from a small fixed table.
	if (row_char_h == 0)
		squixl.get_cached_char_sizes(FONT_SPEC::FONT_WEIGHT_B, 3, &row_char_w, &row_char_h);

	if (!settings.config.news.has_key())
		return;

	// Safety net matching the other cards' - if a queued request's callback
	// never comes back, is_fetching would otherwise block every future retry.
	if (is_fetching && millis() - fetch_started_at > 30000)
	{
		Serial.println("News: fetch appears stuck, resetting is_fetching");
		is_fetching = false;
	}

	unsigned long interval = has_data ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS;

	if (!is_fetching && (next_update == 0 || millis() - next_update > interval))
	{
		if (settings.has_wifi_creds() && !wifi_controller.wifi_blocking_access)
		{
			is_fetching = true;
			fetch_started_at = millis();
			std::string url = "https://api.nytimes.com/svc/topstories/v2/home.json?api-key=" + std::string(settings.config.news.api_key.c_str());
			wifi_controller.add_to_queue(url, [this](bool success, const String &response) { this->process_news_data(success, response); }, NEWS_MAX_RESPONSE_BYTES);
		}
		next_update = millis();
	}
}

void widgetNews::prefetch()
{
	maybe_fetch();
}

void widgetNews::reload_news()
{
	headlines.clear();
	current_headline.clear();
	next_update = 0;
	has_data = false;
	is_fetching = false;
	should_redraw = true;
}

void widgetNews::pick_random_headline()
{
	if (headlines.empty())
		return;

	if (headlines.size() == 1)
	{
		current_headline = headlines[0];
	}
	else
	{
		std::string next;
		do
		{
			next = headlines[random(0, headlines.size())];
		} while (next == current_headline);
		current_headline = next;
	}

	should_redraw = true;
	last_pick_at = millis();
}

void widgetNews::process_news_data(bool success, const String &response)
{
	bool ok = true;
	std::vector<std::string> new_headlines;

	if (response == "ERROR")
	{
		ok = false;
	}
	else
	{
		try
		{
			unsigned long parse_start = millis();

			NewsSax handler(new_headlines);
			bool parsed = json::sax_parse(response, &handler);

			Serial.printf("News: SAX parse took %lums (%d bytes, %d headlines)\n", millis() - parse_start, response.length(), (int)new_headlines.size());

			ok = parsed && handler.ok;
		}
		catch (json::exception &e)
		{
			Serial.println("News SAX parse threw:");
			Serial.println(e.what());
			ok = false;
			new_headlines.clear();
		}
	}

	if (ok && !new_headlines.empty())
	{
		headlines = std::move(new_headlines);
		has_data = true;
		pick_random_headline();
	}
	else
	{
		Serial.println("News: failed to update headlines");
	}

	should_redraw = true;
	is_fetching = false;
	next_update = millis();

	// Required - determines the lifetime of the response String so it
	// survives until after it's been used, matching the other cards.
	delete &response;
}

bool widgetNews::redraw(uint8_t fade_amount, int8_t tab_group)
{
	if (millis() < delay_first_draw)
		return false;

	maybe_fetch();

	// is_dirty_hard is true exactly once per real visit to this screen - set
	// by ui_window::about_to_show_screen() the moment this card's buffers
	// are freshly recreated (both for the live drag-preview mid-swipe and,
	// redundantly but harmlessly, again once the swipe settles - only the
	// first of those two actually recreates anything). That's the one
	// deterministic "the user is now looking at this" signal available
	// before any redraw happens, unlike main.cpp's separate screen-
	// transition check, which only runs after finish_drag() has already
	// driven at least one (sometimes two) redraw() calls of its own -
	// letting the periodic rotate check fire on one of those raced ahead of
	// a dedicated "just arrived" hook and picked its own headline a moment
	// before the dedicated hook's, showing as two different headlines about
	// a second apart. Doing the fresh-arrival pick right here instead, and
	// only falling back to the elapsed-time rotate once truly settled
	// (is_dirty_hard already false), removes that race entirely.
	if (has_data)
	{
		if (is_dirty_hard)
			pick_random_headline();
		else if (millis() - last_pick_at > ROTATE_INTERVAL_MS)
			pick_random_headline();
	}

	bool was_dirty = false;

	if (should_redraw)
	{
		should_redraw = false;
		is_dirty = true;
	}

	if (is_dirty || is_dirty_hard)
	{
		ui_parent->_sprite_back.readImage(_x, _y, _w, _h, (uint16_t *)_sprite_back.getBuffer());
		delay(10);

		// Antialiased text only - see UbuntuMono_Bold18pt7bAA's own header
		// comment for why this can't mix with a 1-bit font on this canvas.
		_sprite_back.setAntialias(true);

		_sprite_clean.fillRect(0, 0, _w, _h, TFT_MAGENTA);
		_sprite_clean.fillRoundRect(0, 0, _w, _h, 14, dashboard_theme::card);
		squixl.lcd.blendSprite(&_sprite_clean, &_sprite_back, &_sprite_back, _t, TFT_MAGENTA);

		constexpr int16_t HEADER_H = 56;
		constexpr int16_t CORNER_R = 14;

		_sprite_back.fillRoundRect(0, 0, _w, HEADER_H, CORNER_R, dashboard_theme::header_bg);
		_sprite_back.fillRect(0, HEADER_H - CORNER_R, _w, CORNER_R, dashboard_theme::header_bg);

		int hdr_w, hdr_h;
		_sprite_back.setFreeFont(&UbuntuMono_Bold18pt7bAA);
		calc_text_size("News", &UbuntuMono_Bold18pt7bAA, &hdr_w, &hdr_h);
		_sprite_back.setTextColor(dashboard_theme::text_primary, -1);
		_sprite_back.setCursor((_w - hdr_w) / 2, HEADER_H / 2 + hdr_h / 2);
		_sprite_back.print("News");

		int16_t content_y0 = HEADER_H;
		int16_t content_y1 = _h - padding.bottom;
		int16_t content_w = _w - padding.left - padding.right;

		if (current_headline.empty())
		{
			const char *msg;
			if (!settings.config.news.has_key())
				msg = "NOT CONFIGURED";
			else if (!has_data)
				msg = wifi_controller.is_connected() ? "..." : "NO INTERNET";
			else
				msg = "NO STORIES";

			int text_w, text_h;
			_sprite_back.setFreeFont(&UbuntuMono_Bold18pt7bAA);
			calc_text_size(msg, &UbuntuMono_Bold18pt7bAA, &text_w, &text_h);
			_sprite_back.setTextColor(dashboard_theme::text_secondary, -1);
			_sprite_back.setCursor((_w - text_w) / 2, content_y0 + (content_y1 - content_y0) / 2);
			_sprite_back.print(msg);
		}
		else
		{
			constexpr int16_t ATTR_GAP = 20;

			std::vector<std::string> lines = wrap_text(current_headline, &UbuntuMono_Bold18pt7bAA, content_w);

			// Bound how many lines can fit above the attribution line -
			// headlines are short enough in practice that this rarely
			// triggers, but a card this size shouldn't ever overflow.
			int16_t avail_h = (content_y1 - content_y0) - row_char_h - ATTR_GAP;
			size_t max_lines = (row_char_h > 0) ? (size_t)(avail_h / (row_char_h + 10)) : lines.size();
			if (max_lines < 1)
				max_lines = 1;

			bool truncated = lines.size() > max_lines;
			if (truncated)
			{
				lines.resize(max_lines);
				std::string &last = lines.back();
				int text_w, text_h;
				calc_text_size((last + "...").c_str(), &UbuntuMono_Bold18pt7bAA, &text_w, &text_h);
				while (text_w > content_w && last.length() > 1)
				{
					last.pop_back();
					calc_text_size((last + "...").c_str(), &UbuntuMono_Bold18pt7bAA, &text_w, &text_h);
				}
				last += "...";
			}

			int16_t line_h = row_char_h + 10;
			int16_t block_h = (int16_t)lines.size() * line_h;
			int16_t block_y0 = content_y0 + (content_y1 - content_y0 - block_h - ATTR_GAP - row_char_h) / 2;

			_sprite_back.setFreeFont(&UbuntuMono_Bold18pt7bAA);
			_sprite_back.setTextColor(dashboard_theme::text_primary, -1);

			int text_w, text_h;
			for (size_t i = 0; i < lines.size(); i++)
			{
				calc_text_size(lines[i].c_str(), &UbuntuMono_Bold18pt7bAA, &text_w, &text_h);
				_sprite_back.setCursor((_w - text_w) / 2, block_y0 + (int16_t)i * line_h + row_char_h);
				_sprite_back.print(lines[i].c_str());
			}

			const char *attribution = "via The New York Times";
			_sprite_back.setFreeFont(&UbuntuMono_Bold14pt7bAA);
			calc_text_size(attribution, &UbuntuMono_Bold14pt7bAA, &text_w, &text_h);
			_sprite_back.setTextColor(dashboard_theme::text_secondary, -1);
			_sprite_back.setCursor((_w - text_w) / 2, block_y0 + block_h + ATTR_GAP + text_h);
			_sprite_back.print(attribution);
		}

		is_dirty_hard = false;
		is_dirty = true;
	}

	squixl.lcd.blendSprite(&_sprite_back, &_sprite_clean, &_sprite_mixed, constrain(fade_amount, 0, 32));
	ui_parent->_sprite_content.drawSprite(_x, _y, &_sprite_mixed, 1.0f, -1);
	next_refresh = millis();

	if (is_dirty && !was_dirty)
		was_dirty = true;

	is_dirty = false;
	is_busy = false;

	return (fade_amount < 32 || was_dirty);
}

bool widgetNews::process_touch(touch_event_t touch_event)
{
	if (touch_event.type == TOUCH_TAP && check_bounds(touch_event.x, touch_event.y))
	{
		// Same rationale as widgetPlayPause's cooldown - the framework's
		// single-tap dispatch is deferred, so a fast double-tap on this card
		// is easy to trigger by accident.
		if (millis() - last_shuffle_at < 700)
			return true;
		last_shuffle_at = millis();

		pick_random_headline();
		return true;
	}

	return false;
}
