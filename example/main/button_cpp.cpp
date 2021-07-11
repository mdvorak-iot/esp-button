#include "example_config.h"
#include <button.h>

// Test that header is compatible with CPP source code

static void button_handler(__unused void *arg, __unused const struct button_data *data)
{
}

extern "C" void test_cpp()
{
    struct button_config cfg = {
        .level = BUTTON_LEVEL_LOW_ON_PRESS,
        .long_press_ms = 0,
        .internal_pull = false,
        .on_press = nullptr,
        .on_release = button_handler,
        .arg = nullptr,
    };
    button_context_ptr ctx = nullptr;
    ESP_ERROR_CHECK(button_config((gpio_num_t)EXAMPLE_BUTTON_PIN, &cfg, &ctx));
    assert(ctx);

    ESP_ERROR_CHECK(button_remove(ctx));
}
