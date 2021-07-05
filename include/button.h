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
    // NOTE this must fit into uint32_t, as is the requirement for event post from ISR

    gpio_num_t pin : 8;
    uint32_t press_length_ms : 23; // NOTE 23 bits have range to 0-8388607 ms
#if BUTTON_LONG_PRESS_ENABLE
    bool long_press : 1;
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

typedef struct button_context *button_context_ptr;

esp_err_t button_config(const struct button_config *cfg, button_context_ptr *context_out);

esp_err_t button_remove(button_context_ptr context);

#ifdef __cplusplus
}
#endif
