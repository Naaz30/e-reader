#include "page.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>
#include <string>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "render.h"

static const char *TAG = "page";

static void show_framebuffer(IT8951 &display, const uint8_t *fb)
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

void draw_screen(IT8951 &display, Demo demo, const char *text, size_t len)
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

    if (demo == Demo::Page) {
        const size_t next = render_page(fb, w, h, text, len, 0);
        ESP_LOGI(TAG, "Page 1 covers bytes 0..%u of %u", (unsigned)next, (unsigned)len);
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

    show_framebuffer(display, fb);
    free(fb);
}


void show_splash(IT8951 &display, const char *text, int hold_ms)
{
    const uint16_t w = display.get_width();
    const uint16_t h = display.get_height();
    const size_t fb_len = (size_t)((w + 1) / 2) * h;

    uint8_t *fb = (uint8_t *)heap_caps_malloc(fb_len, MALLOC_CAP_SPIRAM);
    if (!fb) {
        ESP_LOGE(TAG, "out of memory for framebuffer");
        return;
    }
    memset(fb, 0xFF, fb_len);  
    render_splash(fb, w, h, text);
    show_framebuffer(display, fb);
    free(fb);
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
}

static constexpr gpio_num_t PAGE_PIN  = GPIO_NUM_1;
static constexpr gpio_num_t POWER_PIN = GPIO_NUM_2;
static constexpr gpio_num_t MENU_PIN  = GPIO_NUM_3;
static constexpr int BACK_MS  = 700;   
static constexpr int POWER_MS = 1000; 

enum class Event { PageShort, PageLong, MenuShort, Power };

static void setup_button(gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ull << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

static Event wait_event()
{
    for (;;) {
        if (gpio_get_level(POWER_PIN) == 0) {
            int held = 0;
            while (gpio_get_level(POWER_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                held += 10;
                if (held >= POWER_MS) {
                    while (gpio_get_level(POWER_PIN) == 0)
                        vTaskDelay(pdMS_TO_TICKS(10));
                    return Event::Power;
                }
            }
        }
       
        if (gpio_get_level(PAGE_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(30));                   
            if (gpio_get_level(PAGE_PIN) != 0) continue;   
            int held_ms = 30;
            while (gpio_get_level(PAGE_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                held_ms += 10;
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            return held_ms >= BACK_MS ? Event::PageLong : Event::PageShort;
        }
       
        if (gpio_get_level(MENU_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(30));
            if (gpio_get_level(MENU_PIN) != 0) continue;
            while (gpio_get_level(MENU_PIN) == 0)
                vTaskDelay(pdMS_TO_TICKS(10));
            vTaskDelay(pdMS_TO_TICKS(30));
            return Event::MenuShort;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    return h;
}

static void bookmark_key(const Book &b, char *key)
{
    snprintf(key, 16, "bk%08x", (unsigned)fnv1a(b.title));
}

static size_t load_bookmark(const Book &b)
{
    char key[16];
    bookmark_key(b, key);
    nvs_handle_t nvs;
    if (nvs_open("reader", NVS_READONLY, &nvs) != ESP_OK) return 0;  
    uint32_t v = 0;
    if (nvs_get_u32(nvs, key, &v) != ESP_OK) v = 0;
    nvs_close(nvs);
    return v < b.len ? v : 0; 
}

static void save_bookmark(const Book &b, size_t offset)
{
    char key[16];
    bookmark_key(b, key);
    nvs_handle_t nvs;
    if (nvs_open("reader", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u32(nvs, key, (uint32_t)offset);
    nvs_commit(nvs);
    nvs_close(nvs);
}


static void draw_menu(IT8951 &display, uint8_t *fb, size_t fb_len, uint16_t w, uint16_t h,
                      const Book *books, int count, int sel)
{
    std::string t = "Library\n\n";
    for (int i = 0; i < count; i++) {
        if (i == sel) t += "> ";
        t += books[i].title;
        t += "\n\n";
    }
    memset(fb, 0xFF, fb_len);
    render_page(fb, w, h, t.c_str(), t.size(), 0);
    show_framebuffer(display, fb);
}

static int pick_book(IT8951 &display, uint8_t *fb, size_t fb_len, uint16_t w, uint16_t h,
                     const Book *books, int count, int &sel)
{
    for (;;) {
        draw_menu(display, fb, fb_len, w, h, books, count, sel);
        for (;;) {
            const Event e = wait_event();
            if (e == Event::Power) return -1;
            if (e == Event::MenuShort) { sel = (sel + 1) % count; break; }        
            if (e == Event::PageShort || e == Event::PageLong) return sel;  
        }
    }
}

static size_t find_prev_start(uint8_t *fb, uint16_t w, uint16_t h,
                              const Book &book, size_t target)
{
    size_t pos = 0;
    for (;;) {
        const size_t nxt = render_page(fb, w, h, book.text, book.len, pos);
        if (nxt >= target || nxt <= pos) return pos;
        pos = nxt;
    }
}


static bool read_one_book(IT8951 &display, uint8_t *fb, size_t fb_len,
                          uint16_t w, uint16_t h, const Book &book)
{
    std::vector<size_t> starts;
    starts.push_back(load_bookmark(book));
    size_t idx = 0;

    for (;;) {
        memset(fb, 0xFF, fb_len); 
        const size_t next = render_page(fb, w, h, book.text, book.len, starts[idx]);
        ESP_LOGI(TAG, "\"%s\": page at byte %u..%u of %u", book.title,
                 (unsigned)starts[idx], (unsigned)next, (unsigned)book.len);
        show_framebuffer(display, fb);
        save_bookmark(book, starts[idx]);

        const bool last_page = (next >= book.len);
        for (;;) { 
            const Event e = wait_event();
            if (e == Event::Power) return false;
            if (e == Event::MenuShort) return true;
            if (e == Event::PageShort && !last_page) {
                if (idx + 1 == starts.size()) starts.push_back(next); 
                idx++;
                break;
            }
            if (e == Event::PageLong) {
                if (idx > 0) { idx--; break; }
                if (starts[0] > 0) {  
                    ESP_LOGI(TAG, "Finding the previous page...");
                    const size_t prev = find_prev_start(fb, w, h, book, starts[0]);
                    starts.insert(starts.begin(), prev); 
                    break;
                }
            }
        }
    }
}

void run_library(IT8951 &display, const Book *books, int count)
{
    if (count <= 0) {
        ESP_LOGE(TAG, "no books in the library");
        return;
    }

    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    const uint16_t w = display.get_width();
    const uint16_t h = display.get_height();
    const size_t scan_line = (w + 1) / 2;
    const size_t fb_len = scan_line * h;

    uint8_t *fb = (uint8_t *)heap_caps_malloc(fb_len, MALLOC_CAP_SPIRAM);
    if (!fb) {
        ESP_LOGE(TAG, "out of memory for framebuffer");
        return;
    }

    setup_button(PAGE_PIN);
    setup_button(POWER_PIN);
    setup_button(MENU_PIN);

    int sel = 0; 
    for (;;) {
        const int chosen = pick_book(display, fb, fb_len, w, h, books, count, sel);
        if (chosen < 0) break;                                            // power off
        if (!read_one_book(display, fb, fb_len, w, h, books[chosen])) break;
    }

    free(fb);
}