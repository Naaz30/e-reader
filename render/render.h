#pragma once

#include <stddef.h>
#include <stdint.h>

inline constexpr bool PORTRAIT = true;

inline constexpr bool PORTRAIT_FLIP = false;


inline constexpr float FONT_PX = 44.0f;       
inline constexpr float LINE_SPACING = 1.30f;
inline constexpr int MARGIN_X = 80;
inline constexpr int MARGIN_TOP = 100;
inline constexpr int MARGIN_BOTTOM = 100;
inline constexpr int GLYPH_BUF = 128 * 128; 

size_t render_page(uint8_t *fb, uint16_t pw, uint16_t ph,
                   const char *text, size_t len, size_t start);
