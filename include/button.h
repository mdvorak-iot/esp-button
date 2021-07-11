#pragma once

#include <esp_err.h>
#include <hal/gpio_types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_DEBOUNCE_MS CONFIG_BUTTON_DEBOUNCE_MS
#define BUTTON_LONG_PRESS_ENABLE CONFIG_BUTTON_LONG_PRESS_ENABLE

enum button_level
{
    BUTTON_LEVEL_LOW_ON_PRESS = 0,
    BUTTON_LEVEL_HIGH_ON_PRESS = 1,
};

enum button_event
{
    BUTTON_EVENT_RELEASED = 0,
    BUTTON_EVENT_PRESSED = 1,
};

struct button_data
{
    union
    {
        enum button_event event;
        int level; // NOTE this assumes button_event have same values
    };
    gpio_num_t pin;
    uint32_t press_length_ms;
#if BUTTON_LONG_PRESS_ENABLE
    bool long_press;
#endif
};

typedef void (*button_callback_fn)(void *arg, const struct button_data *data);

struct button_config
{
    gpio_num_t pin; // TODO move to config arg
    enum button_level level;
#if BUTTON_LONG_PRESS_ENABLE
    uint32_t long_press_ms;
#endif
    bool internal_pull;
    button_callback_fn on_press;
    button_callback_fn on_release;
    void *arg;
};

typedef struct button_context *button_context_ptr;

esp_err_t button_config(const struct button_config *cfg, button_context_ptr *context_out);

esp_err_t button_remove(button_context_ptr context);

#ifdef __cplusplus
}
#endif
