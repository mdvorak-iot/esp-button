#include "button.h"
#include <driver/gpio.h>
#include <esp_log.h>

static const char TAG[] = "example";

#define EXAMPLE_BUTTON_PIN CONFIG_EXAMPLE_BUTTON_PIN
#define EXAMPLE_BUTTON_MODE CONFIG_EXAMPLE_BUTTON_MODE
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
        .internal_pull = EXAMPLE_BUTTON_INTERNAL_PULL,
        .mode = EXAMPLE_BUTTON_MODE,
        .long_press_ms = 3000,
        .long_press_mode = BUTTON_LONG_PRESS_ON_RELEASE, // TODO in Kconfig as well, after refactor
    };
    ESP_ERROR_CHECK(button_config(&btn_cfg));

    // Setup complete
    ESP_LOGI(TAG, "started");
}