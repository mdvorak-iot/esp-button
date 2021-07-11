#include "button.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <memory.h>

#if CONFIG_BUTTON_ISR_IN_IRAM
#define BUTTON_IRAM_ATTR IRAM_ATTR
#else
#define BUTTON_IRAM_ATTR
#endif

static const char DRAM_ATTR TAG[] = "button";

static portMUX_TYPE button_mux = portMUX_INITIALIZER_UNLOCKED;

struct button_state
{
    bool pressed;
    int64_t press_start;
};

struct button_context
{
    gpio_num_t pin;
    enum button_level level;
#if BUTTON_LONG_PRESS_ENABLE
    uint32_t long_press_ms;
#endif
    button_callback_fn on_press;
    button_callback_fn on_release;
    void *arg;
    esp_timer_handle_t timer;
    volatile struct button_state state;
};

inline static bool BUTTON_IRAM_ATTR is_pressed(const struct button_context *ctx, int level)
{
    assert(ctx);
    return level == ctx->level;
}

#if BUTTON_LONG_PRESS_ENABLE
inline static bool BUTTON_IRAM_ATTR is_long_press(const struct button_context *ctx, int64_t press_length_ms)
{
    assert(ctx);
    return (ctx->long_press_ms > 0) && (press_length_ms >= ctx->long_press_ms);
}
#endif

static void BUTTON_IRAM_ATTR fire_callback(const struct button_context *ctx, const struct button_data *data)
{
    if (data->event == BUTTON_EVENT_PRESSED)
    {
        if (ctx->on_press) ctx->on_press(ctx->arg, data);
    }
    else if (data->event == BUTTON_EVENT_RELEASED)
    {
        if (ctx->on_release) ctx->on_release(ctx->arg, data);
    }
}

static void BUTTON_IRAM_ATTR handle_button(const struct button_context *ctx, const struct button_state *state, int64_t now, enum button_event event)
{
    assert(ctx);

    // Press length
    int64_t press_length_ms = (now - state->press_start) / 1000L; // us to ms

    // Event data
    struct button_data data =
    {
        .event = event,
        .pin = ctx->pin,
        .press_length_ms = press_length_ms,
#if BUTTON_LONG_PRESS_ENABLE
        .long_press = is_long_press(ctx, press_length_ms),
#endif
    };

    // Log
    const char *log_event_str = (event == BUTTON_EVENT_PRESSED) ? DRAM_STR("pressed") : DRAM_STR("released");
#if BUTTON_LONG_PRESS_ENABLE
    ESP_DRAM_LOGI(TAG, "%d %s after %d ms {long=%d}", ctx->pin, log_event_str, data.press_length_ms, data.long_press);
#else
    ESP_DRAM_LOGI(TAG, "%d %s after %d ms", state->pin, log_event_str, data.press_length_ms);
#endif

    // Callback
    fire_callback(ctx, &data);
}

static void button_timer_handler(void *arg)
{
    // Dereference
    struct button_context *ctx = (struct button_context *)arg;
    assert(ctx);

    // Re-enable interrupt
    // NOTE By enabling interrupt here, it is possible interrupt will run during timer handling
    // This will never cause an inconsistent state
    gpio_intr_enable(ctx->pin);
    ESP_LOGD(TAG, "%d intr enabled", ctx->pin);

    // WARNING portEXIT_CRITICAL is always called in both if-else clauses
    portENTER_CRITICAL(&button_mux);

    // Get current state
    int64_t now = esp_timer_get_time();
    int level = gpio_get_level(ctx->pin);

    // Handle
    if (!is_pressed(ctx, level))
    {
        bool released = false;
        struct button_state local_state;

        if (ctx->state.pressed)
        {
            ctx->state.pressed = false;

            // Copy state and handle
            local_state = ctx->state;
            released = true;
        }
        portEXIT_CRITICAL(&button_mux); // NOTE portENTER_CRITICAL before if

        // Check if button wasn't already released and handled
        if (released)
        {
            // Fire released event
            handle_button(ctx, &local_state, now, BUTTON_EVENT_RELEASED);
        }
        else
        {
            ESP_LOGD(TAG, "%d already released (timer)", ctx->pin);
        }
    }
#if BUTTON_LONG_PRESS_ENABLE
    else
    {
        // Copy state safely
        struct button_state local_state = ctx->state;
        portEXIT_CRITICAL(&button_mux); // NOTE portENTER_CRITICAL before if

        if (local_state.pressed)
        {
            int64_t press_length_ms = (now - ctx->state.press_start) / 1000L; // us to ms
            if (is_long_press(ctx, press_length_ms))
            {
                // Fire long-press event
                handle_button(ctx, &local_state, now, BUTTON_EVENT_PRESSED);
            }
            else if (ctx->long_press_ms > BUTTON_DEBOUNCE_MS)
            {
                // Start timer again, that will fire long-press event, even when button is not released yet
                int64_t timeout_ms = ctx->long_press_ms - BUTTON_DEBOUNCE_MS;
                if (esp_timer_start_once(ctx->timer, timeout_ms * 1000) == ESP_OK)
                {
                    ESP_LOGD(TAG, "%d timer started for %lld ms", ctx->pin, timeout_ms);
                }
            }
        }
        else
        {
            ESP_LOGD(TAG, "%d not pressed (timer)", ctx->pin);
        }
    }
#endif
}

static void BUTTON_IRAM_ATTR button_interrupt_handler(void *arg)
{
    // Dereference
    struct button_context *ctx = (struct button_context *)arg;
    assert(ctx);

    // No further interrupts till timer has finished
    gpio_intr_disable(ctx->pin);
    ESP_DRAM_LOGD(TAG, "%d intr disabled", ctx->pin);

    // WARNING portEXIT_CRITICAL_ISR is always called in both if-else clauses
    portENTER_CRITICAL_ISR(&button_mux);

    // Current state
    int64_t now = esp_timer_get_time();
    int level = gpio_get_level(ctx->pin);

    if (is_pressed(ctx, level))
    {
        // NOTE since this is edge handler, button was just pressed
        bool pressed = false;
        struct button_state local_state;

        if (!ctx->state.pressed)
        {
            // Set start
            ctx->state.press_start = now;
            ctx->state.pressed = true;

            // Copy state and handle
            local_state = ctx->state;
            pressed = true;
        }
        portEXIT_CRITICAL_ISR(&button_mux); // NOTE portENTER_CRITICAL_ISR before if

        // Handle outside mutex
        if (pressed)
        {
            // Fire pressed event
            handle_button(ctx, &local_state, now, BUTTON_EVENT_PRESSED);
        }
        else
        {
            ESP_DRAM_LOGD(TAG, "%d already pressed (isr)", ctx->pin);
        }
    }
    else
    {
        // NOTE since this is edge handler, button was just released
        bool released = false;
        struct button_state local_state;

        if (ctx->state.pressed)
        {
            ctx->state.pressed = false;

            // Copy state and handle
            local_state = ctx->state;
            released = true;
        }
        portEXIT_CRITICAL_ISR(&button_mux); // NOTE portENTER_CRITICAL_ISR before if

        // Handle outside mutex
        if (released)
        {
            // Stop the timer
            if (esp_timer_stop(ctx->timer) == ESP_OK)
            {
                ESP_DRAM_LOGD(TAG, "%d timer stopped", ctx->pin, BUTTON_DEBOUNCE_MS);
            }

            // Fire released event
            handle_button(ctx, &local_state, now, BUTTON_EVENT_RELEASED);
        }
        else
        {
            ESP_DRAM_LOGD(TAG, "%d already released (isr)", ctx->pin);
        }
    }

    // Start timer that re-enables interrupts
    // NOTE this will fail if timer is already running
    if (esp_timer_start_once(ctx->timer, BUTTON_DEBOUNCE_MS * 1000) == ESP_OK)
    {
        ESP_DRAM_LOGD(TAG, "%d timer started for %d ms", ctx->pin, BUTTON_DEBOUNCE_MS);
    }
}

esp_err_t button_config(const struct button_config *cfg, button_context_ptr *context_out)
{
    if (cfg == NULL || cfg->pin < 0 || !GPIO_IS_VALID_GPIO(cfg->pin))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cfg->on_press && !cfg->on_release)
    {
        // At least one callback must be specified, otherwise it doesn't make sense
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
    struct button_context *ctx = malloc(sizeof(*ctx));
    if (ctx == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    memset(ctx, 0, sizeof(*ctx));

    esp_timer_create_args_t timer_cfg = {
        .callback = button_timer_handler,
        .arg = ctx,
        .name = TAG,
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&timer_cfg, &ctx->timer);
    if (err != ESP_OK)
    {
        button_remove(ctx);
        return err;
    }

    ctx->pin = cfg->pin;
    ctx->level = cfg->level;
#if BUTTON_LONG_PRESS_ENABLE
    ctx->long_press_ms = cfg->long_press_ms;
#endif
    ctx->on_press = cfg->on_press;
    ctx->on_release = cfg->on_release;
    ctx->arg = cfg->arg;

    // Register interrupt handler
    err = gpio_isr_handler_add(cfg->pin, button_interrupt_handler, ctx);
    if (err != ESP_OK)
    {
        button_remove(ctx);
        return err;
    }

    // Pass out context pointer
    if (context_out != NULL)
    {
        *context_out = ctx;
    }

    ESP_LOGI(TAG, "configured button on pin %d", cfg->pin);
    return ESP_OK;
}

esp_err_t button_remove(button_context_ptr context)
{
    if (context == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // For later use after free
    gpio_num_t pin = context->pin;

    // Reset pin (ignore errors)
    gpio_isr_handler_remove(pin);
    gpio_reset_pin(pin);

    // Remove internal objects
    if (context->timer)
    {
        esp_timer_stop(context->timer);
        esp_timer_delete(context->timer);
    }

    // Free memory
    free(context);

    // Success
    ESP_LOGI(TAG, "reset button on pin %d", pin);
    return ESP_OK;
}
