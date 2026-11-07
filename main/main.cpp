#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "it8951.h"
#include "page.h"

static const char *TAG = "main";

static constexpr float VCOM = -2.86f;
static constexpr bool VCOM_CONFIRMED = true;

static constexpr Demo DEMO = Demo::Page;

extern const char book_start[] asm("_binary_sample_txt_start");
extern const char book_end[] asm("_binary_sample_txt_end");
extern const char story_start[] asm("_binary_lighthouse_txt_start");
extern const char story_end[] asm("_binary_lighthouse_txt_end");

static const Book BOOKS[] = {
    {"Sample Story",             book_start,  (size_t)(book_end - book_start) - 1},
    {"The Lighthouse at Marrow Point", story_start, (size_t)(story_end - story_start) - 1},
};

static IT8951 display;
static constexpr gpio_num_t QUIT_PIN = GPIO_NUM_0; 
static constexpr gpio_num_t POWER_PIN = GPIO_NUM_2;
static volatile bool quit_requested = false;
static bool display_ready = false;

static void clear_display_on_exit()
{
    if (!display_ready) return;
    ESP_LOGI(TAG, "Exiting: clearing screen...");
    display.clear_screen();
    display.wait_display_ready();
}

static void run()
{
    ESP_LOGI(TAG, "Starting display setup...");
    if (!display.setup(VCOM)) {
        ESP_LOGE(TAG, "display setup failed");
        return;
    }
    display_ready = true;
    ESP_LOGI(TAG, "Panel size: %u x %u",
             (unsigned)display.get_width(), (unsigned)display.get_height());

    if (!VCOM_CONFIRMED) {
        ESP_LOGW(TAG, "VCOM not confirmed - NOT drawing.");
        return;
    }

    ESP_LOGI(TAG, "Clearing screen...");
    display.clear_screen();
    ESP_LOGI(TAG, "Drawing...");
    const size_t book_len = (size_t)(book_end - book_start) - 1;
    if (DEMO == Demo::Page) {
        show_splash(display, "hey, babe!", 1500);
        run_library(display, BOOKS, (int)(sizeof(BOOKS) / sizeof(BOOKS[0])));
        return;
    }
    draw_screen(display, DEMO, book_start, book_len);
    ESP_LOGI(TAG, "Done.");

    gpio_config_t quit_cfg = {
        .pin_bit_mask = 1ull << QUIT_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&quit_cfg));
    ESP_LOGI(TAG, "Running. Press the BOOT button to quit and clear the screen.");

    while (!quit_requested) {
        if (gpio_get_level(QUIT_PIN) == 0) {          
            vTaskDelay(pdMS_TO_TICKS(50));            
            if (gpio_get_level(QUIT_PIN) == 0) {
                ESP_LOGI(TAG, "BOOT pressed: quitting.");
                quit_requested = true;
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void deep_sleep_until_power_button()
{
    ESP_LOGI(TAG, "Sleeping. Press the power button (GPIO2) to wake.");
    rtc_gpio_init(POWER_PIN);
    rtc_gpio_set_direction(POWER_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(POWER_PIN);
    rtc_gpio_pulldown_dis(POWER_PIN);
    esp_sleep_enable_ext0_wakeup(POWER_PIN, 0);   
    esp_deep_sleep_start();                     
}

extern "C" void app_main(void)
{
    run();
    clear_display_on_exit();
    deep_sleep_until_power_button();
}
