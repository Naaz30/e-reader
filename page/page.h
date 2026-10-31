#pragma once

#include <stddef.h>

#include "it8951.h"

enum class Demo { Bars, Page };

// One-shot: draw the first page (or the gray bars) and return.
void draw_screen(IT8951 &display, Demo demo, const char *text, size_t len);

// Reader loop on the BOOT button (GPIO0): draws page 1, then
//   short press = next page, long press = previous page,
//   very long press = return (main then clears the screen).
void read_book(IT8951 &display, const char *text, size_t len);
