#pragma once

#include <cstdint>

// Low-saturation palette shared by the dashboard screen and its widgets.
namespace dashboard_theme
{
	constexpr uint16_t background = 0x0841; // near-black (about RGB 8,8,8)
	constexpr uint16_t card = 0x31C8;		  // slightly lighter muted slate
	constexpr uint16_t header_bg = 0x3A4D;   // muted indigo, for card header bands
	constexpr uint16_t card_track = 0x2A4F;  // gauge track, sits between background and card
	constexpr uint16_t accent_teal = 0x7DD4;  // muted sage teal (gauge fill)
	constexpr uint16_t accent_amber = 0xDD0C; // muted clay amber (highlight)
	constexpr uint16_t text_primary = 0xEF3C;   // warm off-white
	constexpr uint16_t text_secondary = 0x9D15; // muted grey

	// Deliberately more saturated than the rest of the palette - gain/loss
	// needs to read clearly at a glance even inside an otherwise muted UI.
	constexpr uint16_t positive = 0x4ECC; // clean green
	constexpr uint16_t negative = 0xFA27; // clean red
}
