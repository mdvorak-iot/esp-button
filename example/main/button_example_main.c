#include "button.h"
#include "example_config.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/cdefs.h>

static const char TAG[] = "example";
static void *test = NULL;

void test_cpp();

static void button_handler(void *arg, const struct button_data *data)
{
    assert(arg == test);

#if BUTTON_LONG_PRESS_ENABLE
    bool long_press = data->long_press;
#else
    bool long_press = false;
#endif
    ESP_DRAM_LOGI(TAG, "button %d event %d {long_press=%d, press_length_ms=%d}", data->pin, data->event, long_press, data->press_length_ms);
}

_Noreturn void app_main()
{
    // NOTE these don't affect log output via ESP_DRAM_LOG*
    esp_log_level_set("memory_layout", ESP_LOG_INFO);
    esp_log_level_set("heap_init", ESP_LOG_INFO);
    esp_log_level_set("memspi", ESP_LOG_INFO);
    esp_log_level_set("spi_flash", ESP_LOG_INFO);
    esp_log_level_set("cpu_start", ESP_LOG_INFO);
    esp_log_level_set("intr_alloc", ESP_LOG_DEBUG);

    // Generic init
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // CPP test
    test_cpp();

    // Just for assertion, as a primitive test
    test = malloc(1);

    // Test button
    struct button_config btn_cfg =
    {
        .level = EXAMPLE_BUTTON_LEVEL,
        .internal_pull = EXAMPLE_BUTTON_INTERNAL_PULL,
#if CONFIG_BUTTON_LONG_PRESS_ENABLE
        .long_press_ms = EXAMPLE_BUTTON_LONG_PRESS_MS,
#endif
        .continuous_callback = EXAMPLE_BUTTON_CONTINUOUS_CALLBACK,
        .on_press = button_handler,
        .on_release = button_handler,
        .arg = test,
    };
    ESP_ERROR_CHECK(button_config(EXAMPLE_BUTTON_PIN, &btn_cfg, NULL));

    // Setup complete
    ESP_LOGI(TAG, "started");

    while (true) vTaskDelay(1);
}
