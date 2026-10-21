#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "it8951.h"

static const char *TAG = "main";


static constexpr float VCOM = -2.86f;
static constexpr bool VCOM_CONFIRMED = true;


static constexpr bool SHOW_TEXT = true;
static const char *TEXT = "Hello Oona";
static constexpr int TEXT_SCALE = 20;  

static IT8951 display;


struct Glyph { char ch; uint8_t rows[7]; };
static const Glyph FONT[] = {
    {'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'e', {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}},
    {'l', {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}},
    {'o', {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}},
    {'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
    {'n', {0x00,0x00,0x16,0x19,0x11,0x11,0x11}},
    {'a', {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}},
};

static const Glyph *find_glyph(char c)
{
    for (const auto &g : FONT) {
        if (g.ch == c) return &g;
    }
    return nullptr; 
}


static void set_pixel_black(uint8_t *fb, size_t stride, int x, int y)
{
    uint8_t &b = fb[(size_t)y * stride + x / 2];
    if (x & 1) b &= 0xF0;   
    else       b &= 0x0F;  
}

static void draw_text(uint8_t *fb, uint16_t w, uint16_t h, const char *s,
                      int scale, int x0, int y0)
{
    const int advance = 6 * scale;  
    for (int i = 0; s[i]; i++) {
        const Glyph *g = find_glyph(s[i]);
        if (!g) continue;
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (!(g->rows[row] & (0x10 >> col))) continue;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        const int x = x0 + i * advance + col * scale + dx;
                        const int y = y0 + row * scale + dy;
                        if (x >= 0 && x < w && y >= 0 && y < h)
                            set_pixel_black(fb, (w + 1) / 2, x, y);
                    }
                }
            }
        }
    }
}


static void show_framebuffer(const uint8_t *fb)
{
    const uint16_t w = display.get_width();
    const uint16_t h = display.get_height();
    const size_t scan_line = (w + 1) / 2;

    IT8951Area area = {.x = 0, .y = 0, .w = w, .h = h};
    display.load_image_start(area, display.get_memory_address(),
                             IT8951_ROTATE_0, IT8951_PIXEL_FORMAT_4BPP);

    const size_t buf_len = display.get_buffer_len();
    for (uint16_t y = 0; y < h; y++) {
        const uint8_t *row = fb + (size_t)y * scan_line;
        for (size_t off = 0; off < scan_line; off += buf_len) {
            const size_t n = std::min(buf_len, scan_line - off);
            memcpy(display.get_buffer(), row + off, n);
            display.load_image_flush_buffer(n);
        }
    }
    display.load_image_end();

    display.display_area(area, display.get_memory_address(),
                         IT8951_PIXEL_FORMAT_4BPP, IT8951_DISPLAY_MODE_GC16);
    display.wait_display_ready();
}

static void draw_screen()
{
    const uint16_t w = display.get_width();   
    const uint16_t h = display.get_height(); 
    const size_t scan_line = (w + 1) / 2;
    const size_t fb_len = scan_line * h;

    uint8_t *fb = (uint8_t *)heap_caps_malloc(fb_len, MALLOC_CAP_SPIRAM);
    if (!fb) {
        ESP_LOGE(TAG, "out of memory for framebuffer");
        return;
    }
    memset(fb, 0xFF, fb_len);  

    if (SHOW_TEXT) {
        const int text_w = (int)strlen(TEXT) * 6 * TEXT_SCALE - TEXT_SCALE;
        const int text_h = 7 * TEXT_SCALE;
        int x0 = (w - text_w) / 2;
        int y0 = (h - text_h) / 2;
        x0 &= ~1;  
        draw_text(fb, w, h, TEXT, TEXT_SCALE, x0, y0);
    } else {
        const int bars = 16;
        const int bar_px = w / bars;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const uint8_t c = (uint8_t)std::min(x / bar_px, bars - 1);  
                uint8_t &b = fb[(size_t)y * scan_line + x / 2];
                if (x & 1) b = (b & 0xF0) | c;
                else       b = (b & 0x0F) | (c << 4);
            }
        }
    }

    show_framebuffer(fb);
    free(fb);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting display setup...");
    if (!display.setup(VCOM)) {
        ESP_LOGE(TAG, "display setup failed");
        return;
    }
    ESP_LOGI(TAG, "Panel size: %u x %u",
             (unsigned)display.get_width(), (unsigned)display.get_height());

    if (!VCOM_CONFIRMED) {
        ESP_LOGW(TAG, "VCOM not confirmed - NOT drawing.");
    } else {
        ESP_LOGI(TAG, "Clearing screen...");
        display.clear_screen();
        ESP_LOGI(TAG, "Drawing %s...", SHOW_TEXT ? TEXT : "gray bars");
        // draw_screen();
        ESP_LOGI(TAG, "Done.");
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
