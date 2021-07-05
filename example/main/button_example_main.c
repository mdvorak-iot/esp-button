#include "button.h"
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_log.h>

static const char TAG[] = "example";

#define EXAMPLE_BUTTON_PIN CONFIG_EXAMPLE_BUTTON_PIN
#define EXAMPLE_BUTTON_LEVEL CONFIG_EXAMPLE_BUTTON_LEVEL
#if CONFIG_EXAMPLE_BUTTON_INTERNAL_PULL
#define EXAMPLE_BUTTON_INTERNAL_PULL 1
#else
#define EXAMPLE_BUTTON_INTERNAL_PULL 0
#endif
#define EXAMPLE_BUTTON_LONG_PRESS_MS CONFIG_EXAMPLE_BUTTON_LONG_PRESS_MS

static void button_event_handler(__unused void *arg, __unused esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    struct button_data *data = (struct button_data *)event_data;
    ESP_LOGI(TAG, "button %d event %d {long_press=%d, press_length_ms=%d}", data->pin, event_id, data->long_press, data->press_length_ms);
}

void app_main()
{
    // Generic init
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // Events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(BUTTON_EVENT, ESP_EVENT_ANY_ID, button_event_handler, NULL, NULL));

    // Test button
    struct button_config btn_cfg =
    {
        .pin = (gpio_num_t)EXAMPLE_BUTTON_PIN,
        .level = (enum button_level)EXAMPLE_BUTTON_LEVEL,
#if CONFIG_BUTTON_LONG_PRESS_ENABLE
        .long_press_ms = EXAMPLE_BUTTON_LONG_PRESS_MS,
#endif
        .internal_pull = EXAMPLE_BUTTON_INTERNAL_PULL,
        .event_loop = NULL,
    };
    ESP_ERROR_CHECK(button_config(&btn_cfg, NULL));

    // Setup complete
    ESP_LOGI(TAG, "started");
}
