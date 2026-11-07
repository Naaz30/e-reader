#pragma once

#include <stddef.h>

#include "it8951.h"

enum class Demo { Bars, Page };

void draw_screen(IT8951 &display, Demo demo, const char *text, size_t len);

void show_splash(IT8951 &display, const char *text, int hold_ms);

struct Book {
    const char *title;   
    const char *text;   
    size_t len; 
};

void run_library(IT8951 &display, const Book *books, int count);
