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

// TODO rename to context
struct button_context
{
    gpio_num_t pin;
    enum button_level level;
#if BUTTON_LONG_PRESS_ENABLE
    uint32_t long_press_ms;
#endif
    esp_event_loop_handle_t event_loop;
    esp_timer_handle_t timer;
    SemaphoreHandle_t pressed_handle;
    volatile int64_t press_start;
};

inline static bool is_pressed(const struct button_context *ctx, int level)
{
    return level == ctx->level;
}

inline static bool is_long_press(const struct button_context *ctx, int64_t press_length_ms)
{
#if BUTTON_LONG_PRESS_ENABLE
    return (ctx->long_press_ms > 0) && (press_length_ms >= ctx->long_press_ms);
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

static void handle_button(struct button_context *ctx, int64_t now, int32_t event_id)
{
    // Press length
    int64_t press_length_ms = (now - ctx->press_start) / 1000L; // us to ms

    // Event data
    struct button_data data =
    {
        .pin = ctx->pin,
        .press_length_ms = press_length_ms,
#if BUTTON_LONG_PRESS_ENABLE
        .long_press = is_long_press(ctx, press_length_ms),
#endif
    };

    // Log
    // TODO make these debug
#if BUTTON_LONG_PRESS_ENABLE
    ESP_DRAM_LOGI("button", "%s pin %d after %d ms (long=%d)", event_id == BUTTON_EVENT_PRESS ? "pressed" : "released", ctx->pin, data.press_length_ms, data.long_press);
#else
    ESP_DRAM_LOGI("button", "%s pin %d after %d ms", event_id == BUTTON_EVENT_PRESS ? "pressed" : "released", state->pin, data.press_length_ms);
#endif

    // Queue event
    button_event_isr_post(ctx->event_loop, event_id, &data);
}

static void button_timer_handler(void *arg)
{
    // Dereference
    struct button_context *ctx = (struct button_context *)arg;

    int level = gpio_get_level(ctx->pin);
    int64_t now = esp_timer_get_time();
    int64_t press_length_ms = (now - ctx->press_start) / 1000L; // us to ms

    if (!is_pressed(ctx, level))
    {
        // Check if button wasn't already released and handled
        if (xSemaphoreGive(ctx->pressed_handle) == pdTRUE)
        {
            // Fire released event
            handle_button(ctx, now, BUTTON_EVENT_RELEASED);
        }
    }
    else if (is_long_press(ctx, press_length_ms))
    {
        // Fire long-press event
        handle_button(ctx, now, BUTTON_EVENT_PRESS);
    }
#if BUTTON_LONG_PRESS_ENABLE
    else if (ctx->long_press_ms > BUTTON_DEBOUNCE_MS)
    {
        // Start timer again, that will fire long-press event, even when button is not released yet
        esp_timer_start_once(ctx->timer, (ctx->long_press_ms - BUTTON_DEBOUNCE_MS) * 1000);
    }
#endif

    // Re-enable interrupts
    gpio_intr_enable(ctx->pin);
}

static void BUTTON_IRAM_ATTR button_interrupt_handler(void *arg)
{
    // Dereference
    struct button_context *ctx = (struct button_context *)arg;

    // Current state
    int64_t now = esp_timer_get_time();
    int level = gpio_get_level(ctx->pin);

    // No further interrupts till timer has finished
    gpio_intr_disable(ctx->pin);

    // FreeRTOS semaphore handling helper
    BaseType_t task_woken = 0;

    if (is_pressed(ctx, level))
    {
        // NOTE since this is edge handler, button was just pressed
        if (xSemaphoreTakeFromISR(ctx->pressed_handle, &task_woken) == pdTRUE)
        {
            // Set start
            ctx->press_start = now;

            // Fire pressed event
            handle_button(ctx, now, BUTTON_EVENT_PRESS);
        }
    }
    else
    {
        // NOTE since this is edge handler, button was just released
        if (xSemaphoreGiveFromISR(ctx->pressed_handle, &task_woken) == pdTRUE)
        {
            // Stop the timer
            esp_timer_stop(ctx->timer);

            // Fire released event
            handle_button(ctx, now, BUTTON_EVENT_RELEASED);
        }
    }

    // Start timer that re-enables interrupts
    // NOTE this will fail if timer is already running
    esp_timer_start_once(ctx->timer, BUTTON_DEBOUNCE_MS * 1000);

    // Wake task
    portYIELD_FROM_ISR(task_woken);
}

esp_err_t button_config(const struct button_config *cfg, button_context *context_out)
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
    ctx->event_loop = cfg->event_loop;
    ctx->pressed_handle = xSemaphoreCreateBinary();

    if (ctx->pressed_handle == NULL)
    {
        button_remove(ctx);
        return ESP_ERR_NOT_SUPPORTED;
    }

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

esp_err_t button_remove(button_context context)
{
    if (context == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Reset pin (ignore errors)
    gpio_isr_handler_remove(context->pin);
    gpio_reset_pin(context->pin);

    // Remove internal objects
    if (context->timer)
    {
        esp_timer_stop(context->timer);
        esp_timer_delete(context->timer);
    }
    if (context->pressed_handle)
    {
        vSemaphoreDelete(context->pressed_handle);
    }

    // Free memory
    free(context);

    // Success
    return ESP_OK;
}
