#include "page.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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



// One button does everything for now: the BOOT button (GPIO0).
//   short press      (< BACK_MS)            = next page
//   long press       (BACK_MS .. QUIT_MS)   = previous page
//   very long press  (>= QUIT_MS)           = quit (the screen is cleared)
// Later, point NEXT_PIN at GPIO1 once the real button is wired.
static constexpr gpio_num_t NEXT_PIN = GPIO_NUM_0;
static constexpr int BACK_MS = 700;
static constexpr int QUIT_MS = 2500;

enum class Press { Next, Prev, Quit };

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

static Press wait_for_press(bool can_next, bool can_prev)
{
    for (;;) {
        if (gpio_get_level(NEXT_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(30));                 
            if (gpio_get_level(NEXT_PIN) != 0) continue;   
            int held_ms = 30;
            while (gpio_get_level(NEXT_PIN) == 0) {        
                vTaskDelay(pdMS_TO_TICKS(10));
                held_ms += 10;
            }
            vTaskDelay(pdMS_TO_TICKS(30));

            if (held_ms >= QUIT_MS) return Press::Quit;
            if (held_ms >= BACK_MS) {
                if (can_prev) return Press::Prev;
            } else {
                if (can_next) return Press::Next;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void read_book(IT8951 &display, const char *text, size_t len)
{
    const uint16_t w = display.get_width();
    const uint16_t h = display.get_height();
    const size_t scan_line = (w + 1) / 2;
    const size_t fb_len = scan_line * h;

    // One framebuffer, reused for every page.
    uint8_t *fb = (uint8_t *)heap_caps_malloc(fb_len, MALLOC_CAP_SPIRAM);
    if (!fb) {
        ESP_LOGE(TAG, "out of memory for framebuffer");
        return;
    }

    setup_button(NEXT_PIN);

    std::vector<size_t> starts;
    starts.push_back(0);
    size_t idx = 0; 

    for (;;) {
        memset(fb, 0xFF, fb_len);  // blank page
        const size_t next = render_page(fb, w, h, text, len, starts[idx]);
        ESP_LOGI(TAG, "Page %u: bytes %u..%u of %u", (unsigned)(idx + 1),
                 (unsigned)starts[idx], (unsigned)next, (unsigned)len);
        show_framebuffer(display, fb);

        const bool last_page = (next >= len);
        ESP_LOGI(TAG, "Short press = next, long = back, very long = quit%s",
                 last_page ? "  (end of book)" : "");

        const Press p = wait_for_press(!last_page, idx > 0);
        if (p == Press::Quit) {
            ESP_LOGI(TAG, "Very long press: quitting.");
            break;
        }
        if (p == Press::Next) {
            if (idx + 1 == starts.size()) starts.push_back(next);  
            idx++;
        } else { 
            idx--;
        }
    }

    free(fb);
}
