#pragma once

#include <esp_err.h>
#include <esp_event_base.h>
#include <hal/gpio_types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_DEBOUNCE_MS CONFIG_BUTTON_DEBOUNCE_MS
#define BUTTON_LONG_PRESS_ENABLE CONFIG_BUTTON_LONG_PRESS_ENABLE

ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);

enum button_event
{
    BUTTON_EVENT_PRESS,
    BUTTON_EVENT_RELEASED,
};

enum button_level
{
    BUTTON_LEVEL_LOW_ON_PRESS = 0,
    BUTTON_LEVEL_HIGH_ON_PRESS = 1,
};

struct button_data
{
    gpio_num_t pin;
    uint32_t press_length_ms;
#if BUTTON_LONG_PRESS_ENABLE
    bool long_press;
#endif
};

struct button_config
{
    gpio_num_t pin;
    enum button_level level;
#if BUTTON_LONG_PRESS_ENABLE
    uint32_t long_press_ms;
#endif
    bool internal_pull;
    esp_event_loop_handle_t event_loop;
};

esp_err_t button_config(const struct button_config *cfg);

esp_err_t button_remove(gpio_num_t pin);

#ifdef __cplusplus
}
#endif
