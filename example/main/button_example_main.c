#include "button.h"
#include <driver/gpio.h>
#include <esp_log.h>

static const char TAG[] = "example";

#define EXAMPLE_BUTTON_PIN CONFIG_EXAMPLE_BUTTON_PIN
#define EXAMPLE_BUTTON_LEVEL CONFIG_EXAMPLE_BUTTON_LEVEL
#if CONFIG_EXAMPLE_BUTTON_INTERNAL_PULL
#define EXAMPLE_BUTTON_INTERNAL_PULL 1
#else
#define EXAMPLE_BUTTON_INTERNAL_PULL 0
#endif

void app_main()
{
    // Generic init
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // Test button
    struct button_config btn_cfg = {
        .pin = EXAMPLE_BUTTON_PIN,
        .level = EXAMPLE_BUTTON_LEVEL,
#if CONFIG_BUTTON_LONG_PRESS_ENABLE
        .long_press_ms = 3000,
#endif
        .internal_pull = EXAMPLE_BUTTON_INTERNAL_PULL,
    };
    ESP_ERROR_CHECK(button_config(&btn_cfg, NULL));

    // Setup complete
    ESP_LOGI(TAG, "started");
}
