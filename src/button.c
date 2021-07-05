#include "button.h"
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_log.h>
#include <memory.h>

#if CONFIG_BUTTON_ISR_IN_IRAM
#define BUTTON_IRAM_ATTR IRAM_ATTR
#else
#define BUTTON_IRAM_ATTR
#endif

static const char TAG[] = "button";

ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);

struct button_state
{
    enum button_level level;
#if BUTTON_LONG_PRESS_ENABLE
    uint32_t long_press_ms;
#endif
    esp_event_loop_handle_t event_loop;
    esp_timer_handle_t timer;
    volatile bool pressed;
    volatile int64_t press_start;
};

static struct button_state *button_states[GPIO_NUM_MAX] = {};

inline static bool IRAM_ATTR is_pressed(const struct button_state *state, int level)
{
    return level == state->level;
}

inline static bool is_long_press(const struct button_state *state, int64_t press_length_ms)
{
#if BUTTON_LONG_PRESS_ENABLE
    return (state->long_press_ms > 0) && (press_length_ms >= state->long_press_ms);
#else
    return false;
#endif
}

static esp_err_t button_event_isr_post(esp_event_loop_handle_t event_loop, int32_t event_id, struct button_data *event_data)
{
    if (event_loop)
    {
        return esp_event_isr_post_to(event_loop, BUTTON_EVENT, event_id, event_data, sizeof(*event_data), NULL);
    }
    else
    {
        return esp_event_isr_post(BUTTON_EVENT, event_id, event_data, sizeof(*event_data), NULL);
    }
}

static void on_press(gpio_num_t pin, struct button_state *state)
{
    // Event data
    struct button_data data =
    {
        .pin = pin,
        .press_length_ms = 0,
#if BUTTON_LONG_PRESS_ENABLE
        .long_press = false,
#endif
    };

    // Log
    // TODO make this debug
    ESP_DRAM_LOGI("button", "pressed pin %d", pin);

    // Queue event
    button_event_isr_post(state->event_loop, BUTTON_EVENT_PRESS, &data);
}

static void on_release(gpio_num_t pin, struct button_state *state, int64_t now)
{
    // Already handled
    if (!state->pressed)
    {
        return;
    }

    // Press length
    int64_t press_length_ms = (now - state->press_start) / 1000L; // us to ms

    // Stop the timer
    esp_timer_stop(state->timer);

    // Event data
    struct button_data data =
    {
        .pin = pin,
        .press_length_ms = press_length_ms,
#if BUTTON_LONG_PRESS_ENABLE
        .long_press = is_long_press(state, press_length_ms),
#endif
    };

    // Log
    // TODO make these debug
#if BUTTON_LONG_PRESS_ENABLE
    ESP_DRAM_LOGI("button", "released pin %d after %d ms (long=%d)", pin, data.press_length_ms, data.long_press);
#else
    ESP_DRAM_LOGI("button", "released pin %d after %d ms", pin, data.press_length_ms);
#endif

    // Queue event
    button_event_isr_post(state->event_loop, BUTTON_EVENT_RELEASED, &data);

    // Reset press start, in case of rare race-condition of the timer and ISR
    state->press_start = now;
    state->pressed = false;
}

static void button_timer_handler(void *arg)
{
    gpio_num_t pin = (size_t)arg; // pin stored directly as the pointer
    struct button_state *state = button_states[pin];

    int level = gpio_get_level(pin);
    int64_t now = esp_timer_get_time();
    int64_t press_length_ms = (now - state->press_start) / 1000L; // us to ms

    if (!is_pressed(state, level) || is_long_press(state, press_length_ms))
    {
        // Button released during debounce interval, or long-press should be reported right away
        on_release(pin, state, now);
    }
#if BUTTON_LONG_PRESS_ENABLE
    else if (state->long_press_ms > BUTTON_DEBOUNCE_MS)
    {
        // Start timer again, that will fire long-press event, even when button is not released yet
        esp_timer_start_once(state->timer, (state->long_press_ms - BUTTON_DEBOUNCE_MS) * 1000);
    }
#endif

    // Re-enable interrupts
    gpio_intr_enable(pin);
}

static void BUTTON_IRAM_ATTR button_interrupt_handler(void *arg)
{
    // Dereference
    gpio_num_t pin = (size_t)arg; // pin stored directly as the pointer
    struct button_state *state = button_states[pin];

    // Current state
    int64_t now = esp_timer_get_time();
    int level = gpio_get_level(pin);

    if (is_pressed(state, level))
    {
        // NOTE since this is edge handler, button was just pressed
        state->pressed = true;
        state->press_start = now;

        // No further interrupts till timer has finished
        gpio_intr_disable(pin);

        // Start timer
        esp_timer_start_once(state->timer, BUTTON_DEBOUNCE_MS * 1000);
    }
    else
    {
        // NOTE since this is edge handler, button was just released

        // Debounce also when releasing
        gpio_intr_disable(pin);

        // Run release logic
        on_release(pin, state, now);

        // Start timer that re-enables interrupts
        esp_timer_start_once(state->timer, BUTTON_DEBOUNCE_MS * 1000);
    }
}

esp_err_t button_config(const struct button_config *cfg)
{
    if (cfg == NULL || cfg->pin < 0 || !GPIO_IS_VALID_GPIO(cfg->pin))
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Configure GPIO
    gpio_config_t gpio_cfg = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = BIT64(cfg->pin),
        .intr_type = GPIO_INTR_ANYEDGE,
        .pull_up_en = (cfg->internal_pull && cfg->level == BUTTON_LEVEL_LOW_ON_PRESS) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (cfg->internal_pull && cfg->level == BUTTON_LEVEL_HIGH_ON_PRESS) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
    };

    esp_err_t err = gpio_config(&gpio_cfg);
    if (err != ESP_OK)
    {
        return err;
    }

    // Initialize internal state
    struct button_state *state = button_states[cfg->pin];

    if (state == NULL)
    {
        state = button_states[cfg->pin] = malloc(sizeof(*state));
        memset(state, 0, sizeof(*state));

        esp_timer_create_args_t timer_cfg = {
            .callback = button_timer_handler,
            .arg = (void *)cfg->pin,
            .name = "button",
        };
        err = esp_timer_create(&timer_cfg, &state->timer);
        if (err != ESP_OK)
        {
            return err;
        }
    }
    state->level = cfg->level;
#if BUTTON_LONG_PRESS_ENABLE
    state->long_press_ms = cfg->long_press_ms;
#endif
    state->event_loop = cfg->event_loop;

    // Register interrupt handler
    err = gpio_isr_handler_add(cfg->pin, button_interrupt_handler, (void *)cfg->pin); // NOTE using pin value directly in the pointer, not as a pointer
    if (err != ESP_OK)
    {
        return err;
    }

    ESP_LOGI(TAG, "configured button on pin %d", cfg->pin);
    return ESP_OK;
}

esp_err_t button_remove(gpio_num_t pin)
{
    if (pin < 0 || !GPIO_IS_VALID_GPIO(pin))
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct button_state *state = button_states[pin];
    button_states[pin] = NULL;

    esp_timer_stop(state->timer);
    esp_timer_delete(state->timer);

    free(state);

    gpio_isr_handler_remove(pin);
    gpio_reset_pin(pin);
    return ESP_OK;
}
