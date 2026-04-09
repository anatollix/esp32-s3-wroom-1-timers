#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

static const char *TAG = "Main";

#define FAN_CYCLE_SEC  12   // full cycle period (seconds), 3600 for 1 hour
#define FAN_ON_SEC      4   // fan ON within cycle (seconds), 900 for 15 minutes

#define TIMER_RESOLUTION_HZ  1000000  // 1 MHz — 1 tick = 1 µs
#define SEC_TO_TICKS(s)      ((s) * (uint64_t)TIMER_RESOLUTION_HZ)
#define FAN_ON_TICKS         SEC_TO_TICKS(FAN_ON_SEC)
#define FAN_OFF_TICKS        SEC_TO_TICKS(FAN_CYCLE_SEC - FAN_ON_SEC)

static const gpio_num_t FAN_PIN = GPIO_NUM_4;

static bool fan_running = false;
static gptimer_handle_t fan_timer = NULL;

static void IRAM_ATTR fan_on(void) {
    gpio_set_level(FAN_PIN, 1);
    fan_running = true;
    ESP_DRAM_LOGI(TAG, "Fan ON");
}

static void IRAM_ATTR fan_off(void) {
    gpio_set_level(FAN_PIN, 0);
    fan_running = false;
    ESP_DRAM_LOGI(TAG, "Fan OFF");
}

static bool IRAM_ATTR fan_timer_cb(gptimer_handle_t timer,
                                    const gptimer_alarm_event_data_t *edata,
                                    void *user_ctx) {
    gptimer_alarm_config_t alarm = {};
    alarm.flags.auto_reload_on_alarm = true;

    if (fan_running) {
        fan_off();
        alarm.alarm_count = FAN_OFF_TICKS;
    } else {
        fan_on();
        alarm.alarm_count = FAN_ON_TICKS;
    }

    gptimer_set_alarm_action(timer, &alarm);
    return false;
}

static void fan_gpio_init(void) {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << FAN_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}

static void fan_timer_init(void) {
    // Create GPTimer
    gptimer_config_t timer_cfg = {};
    timer_cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    timer_cfg.direction = GPTIMER_COUNT_UP;
    timer_cfg.resolution_hz = TIMER_RESOLUTION_HZ;
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_cfg, &fan_timer));

    // Register callback
    gptimer_event_callbacks_t cbs = {};
    cbs.on_alarm = fan_timer_cb;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(fan_timer, &cbs, NULL));

    // First alarm fires immediately, callback will turn fan ON
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = 1;
    alarm.flags.auto_reload_on_alarm = false;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(fan_timer, &alarm));

    ESP_ERROR_CHECK(gptimer_enable(fan_timer));
    ESP_ERROR_CHECK(gptimer_start(fan_timer));
}

static void print_banner(void) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    printf("\n========================================\n");
    printf("   ESP32-S3 Timers — Fan Control\n");
    printf("========================================\n");
    printf("Board: ESP32-S3-WROOM-1\n");
    printf("CPU Cores: %d @ %d MHz\n", chip_info.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    printf("Flash: %lu MB\n", flash_size / (1024 * 1024));
    printf("Free Heap: %lu bytes\n", esp_get_free_heap_size());
    printf("Fan cycle: %ds total, %ds ON\n", FAN_CYCLE_SEC, FAN_ON_SEC);
    printf("========================================\n\n");
}

extern "C" void app_main(void) {
    // Wait for USB Serial JTAG to initialize
    vTaskDelay(pdMS_TO_TICKS(3000));

    print_banner();

    fan_gpio_init();
    fan_timer_init();

    // Nothing left to do in the main task
    vTaskSuspend(NULL);
}
