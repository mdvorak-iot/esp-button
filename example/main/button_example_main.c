#include "button.h"
#include "example_config.h"
#include <driver/gpio.h>
#include <esp_log.h>

static const char TAG[] = "example";
static void *test = NULL;

void test_cpp();

static void button_handler(void *arg, const struct button_data *data)
{
    assert(arg == test);
    ESP_DRAM_LOGI(TAG, "button %d event %d {long_press=%d, press_length_ms=%d, level=%d}", data->pin, data->event, data->long_press, data->press_length_ms, data->level);
}

void app_main()
{
    // Generic init
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // CPP test
    test_cpp();

    // Just for assertion, as a primitive test
    test = malloc(1);

    // Test button
    struct button_config btn_cfg =
    {
        .pin = (gpio_num_t)EXAMPLE_BUTTON_PIN,
        .level = (enum button_level)EXAMPLE_BUTTON_LEVEL,
#if CONFIG_BUTTON_LONG_PRESS_ENABLE
        .long_press_ms = EXAMPLE_BUTTON_LONG_PRESS_MS,
#endif
        .internal_pull = EXAMPLE_BUTTON_INTERNAL_PULL,
        .on_press = button_handler,
        .on_release = button_handler,
        .arg = test,
    };
    ESP_ERROR_CHECK(button_config(&btn_cfg, NULL));

    // Setup complete
    ESP_LOGI(TAG, "started");
}
