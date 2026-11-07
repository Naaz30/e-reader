#include "font.h"

extern const uint8_t font_start[] asm("_binary_CrimsonText_Regular_ttf_start");
extern const uint8_t script_start[] asm("_binary_Pacifico_Regular_ttf_start");

const uint8_t *font_data() { return font_start; }
const uint8_t *font_script_data() { return script_start; }
