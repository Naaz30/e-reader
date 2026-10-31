#include "font.h"

extern const uint8_t font_start[] asm("_binary_CrimsonText_Regular_ttf_start");

const uint8_t *font_data() { return font_start; }
