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
#if BUTTON_LONG_PRESS_ENABLE
    bool long_press;
#endif
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

inline static void BUTTON_IRAM_ATTR fire_callback(const struct button_context *ctx, const struct button_data *data)
{
    assert(ctx);
    assert(data);

    if (data->event == BUTTON_EVENT_PRESSED)
    {
        if (ctx->on_press) ctx->on_press(ctx->arg, data);
    }
    else if (data->event == BUTTON_EVENT_RELEASED)
    {
        if (ctx->on_release) ctx->on_release(ctx->arg, data);
    }
}

static void BUTTON_IRAM_ATTR handle_button(const struct button_context *ctx, enum button_event event, int64_t press_length_ms, bool long_press)
{
    assert(ctx);

    // Event data
    struct button_data data =
    {
        .event = event,
        .pin = ctx->pin,
        .press_length_ms = press_length_ms,
#if BUTTON_LONG_PRESS_ENABLE
        .long_press = long_press,
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

    // Get current state
    int64_t now = esp_timer_get_time();
    int level = gpio_get_level(ctx->pin);

    // Handle state change
    portENTER_CRITICAL(&button_mux);

    // Handle release debounce or misfire
    if (!ctx->state.pressed)
    {
        // Exit critical section
        portEXIT_CRITICAL(&button_mux);
        // Stop timer
        esp_timer_stop(ctx->timer);
        // Re-enable interrupt
        gpio_intr_enable(ctx->pin);
        ESP_LOGV(TAG, "%d intr enabled", ctx->pin);
        // Done (already exited from CS)
        return;
    }

    bool released = false;
    int64_t press_length_ms = (now - ctx->state.press_start) / 1000LL; // us to ms

#if BUTTON_LONG_PRESS_ENABLE
    // Is it long press?
    bool fire_long_press = false;
    if (!ctx->state.long_press && ctx->long_press_ms > 0)
    {
        ctx->state.long_press = fire_long_press = (press_length_ms >= ctx->long_press_ms);
    }

    // Store long_press state for use outside CS
    bool state_long_press = ctx->state.long_press;
#else
    bool state_long_press = false;
#endif

    // Released?
    if (!is_pressed(ctx, level))
    {
        ctx->state.pressed = false;
#if BUTTON_LONG_PRESS_ENABLE
        ctx->state.long_press = false;
#endif
        released = true;
    }

    // Exit CS for actual processing
    portEXIT_CRITICAL(&button_mux);

    // Handle
    if (released)
    {
        // Stop timer
        esp_timer_stop(ctx->timer);

        // Fire released event
        handle_button(ctx, BUTTON_EVENT_RELEASED, press_length_ms, state_long_press);

        // Start timer once - it will re-enable interrupt
        esp_err_t err = esp_timer_start_once(ctx->timer, BUTTON_DEBOUNCE_MS * 1000ULL);
        if (err == ESP_OK)
        {
            ESP_DRAM_LOGV(TAG, "%d timer started for %d ms (timer)", ctx->pin, BUTTON_DEBOUNCE_MS);
        }
        else
        {
            ESP_DRAM_LOGV(TAG, "%d timer failed to start: %d (timer)", err);
        }

        // Done
        return;
    }
#if BUTTON_LONG_PRESS_ENABLE
    else if (fire_long_press)
    {
        // Fire long-press event
        handle_button(ctx, BUTTON_EVENT_PRESSED, press_length_ms, true);
    }
#endif

    // Make sure timer is running - this is needed for manual suspend support
    esp_timer_start_periodic(ctx->timer, BUTTON_DEBOUNCE_MS * 1000ULL);
}

static void BUTTON_IRAM_ATTR button_interrupt_handler(void *arg)
{
    // Dereference
    struct button_context *ctx = (struct button_context *)arg;
    assert(ctx);

    // No further interrupts till timer has finished
    gpio_intr_disable(ctx->pin);
    ESP_DRAM_LOGV(TAG, "%d intr disabled", ctx->pin);

    int64_t now = esp_timer_get_time();

    // Sync
    portENTER_CRITICAL_ISR(&button_mux);

    // Button was just pressed
    bool pressed = false;

    if (!ctx->state.pressed)
    {
        // Set start
        ctx->state.press_start = now;
        ctx->state.pressed = true;
        ctx->state.long_press = false;

        // Set flag
        pressed = true;
    }
    portEXIT_CRITICAL_ISR(&button_mux);

    // Handle outside mutex
    if (pressed)
    {
        // Fire pressed event
        handle_button(ctx, BUTTON_EVENT_PRESSED, 0, false);

        // Start timer polling timer
        esp_err_t err = esp_timer_start_periodic(ctx->timer, BUTTON_DEBOUNCE_MS * 1000ULL);
        if (err == ESP_OK)
        {
            ESP_DRAM_LOGV(TAG, "%d timer started for %d ms (intr)", ctx->pin, BUTTON_DEBOUNCE_MS);
        }
        else
        {
            ESP_DRAM_LOGV(TAG, "%d timer failed to start: %d (isr)", err);
        }
    }
    else
    {
        ESP_DRAM_LOGD(TAG, "%d already pressed (isr)", ctx->pin);
    }
}

esp_err_t button_config(gpio_num_t pin, const struct button_config *cfg, button_context_ptr *ctx_out)
{
    if (cfg == NULL || pin < 0 || !GPIO_IS_VALID_GPIO(pin))
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
        .pin_bit_mask = BIT64(pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (cfg->internal_pull && cfg->level == BUTTON_LEVEL_LOW_ON_PRESS) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (cfg->internal_pull && cfg->level == BUTTON_LEVEL_HIGH_ON_PRESS) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = (cfg->level == BUTTON_LEVEL_LOW_ON_PRESS) ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE,
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

    ctx->pin = pin;
    ctx->level = cfg->level;
#if BUTTON_LONG_PRESS_ENABLE
    ctx->long_press_ms = cfg->long_press_ms;
#endif
    ctx->on_press = cfg->on_press;
    ctx->on_release = cfg->on_release;
    ctx->arg = cfg->arg;

    // Register interrupt handler
    err = gpio_isr_handler_add(pin, button_interrupt_handler, ctx);
    if (err != ESP_OK)
    {
        button_remove(ctx);
        return err;
    }

    // Pass out context pointer
    if (ctx_out != NULL)
    {
        *ctx_out = ctx;
    }

    ESP_LOGI(TAG, "configured button on pin %d", pin);
    return ESP_OK;
}

esp_err_t button_remove(button_context_ptr ctx)
{
    if (ctx == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // For later use after free
    gpio_num_t pin = ctx->pin;

    // Reset pin (ignore errors)
    gpio_isr_handler_remove(pin);
    gpio_reset_pin(pin);

    // Remove internal objects
    if (ctx->timer)
    {
        esp_timer_stop(ctx->timer);
        esp_timer_delete(ctx->timer);
    }

    // Free memory
    free(ctx);

    // Success
    ESP_LOGI(TAG, "reset button on pin %d", pin);
    return ESP_OK;
}

inline esp_err_t button_suspend(button_context_ptr ctx)
{
    return button_suspend_for(ctx, -1);
}

esp_err_t button_suspend_for(button_context_ptr ctx, int32_t timeout_ms)
{
    if (ctx == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // No further interrupts till timer has finished
    gpio_intr_disable(ctx->pin);
    ESP_DRAM_LOGV(TAG, "%d intr disabled", ctx->pin);

    // Stop timer and start it again
    // NOTE these two calls have possible race-condition with timer and ISR
    esp_timer_stop(ctx->timer);

    if (timeout_ms >= 0)
    {
        // This will eventually re-enable interrupt, and also fire any pending events
        // NOTE if this fails, suspend have failed due to race-condition
        return esp_timer_start_once(ctx->timer, timeout_ms * 1000ULL);
    }
    else
    {
        // Disabled
        return ESP_OK;
    }
}

esp_err_t button_resume(button_context_ptr ctx)
{
    if (ctx == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Stop timer and start it again
    // NOTE these two calls have possible race-condition with timer and ISR
    esp_timer_stop(ctx->timer);

    // Start normal timer - it will get stopped in timer handler, if needed
    return esp_timer_start_periodic(ctx->timer, BUTTON_DEBOUNCE_MS * 1000ULL);
}
