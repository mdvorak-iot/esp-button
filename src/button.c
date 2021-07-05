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

static const char DRAM_ATTR TAG[] = "button";

ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);

struct button_context
{
    gpio_num_t pin;
    enum button_level level;
#if BUTTON_LONG_PRESS_ENABLE
    uint32_t long_press_ms;
#endif
    esp_event_loop_handle_t event_loop;
    esp_timer_handle_t timer;
    SemaphoreHandle_t pressed_handle; // NOTE using semaphore here as atomic bool, not directly as a synchronization mechanism
    volatile int64_t press_start;
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

static esp_err_t BUTTON_IRAM_ATTR button_event_isr_post(esp_event_loop_handle_t event_loop, int32_t event_id, struct button_data *event_data)
{
    assert(event_data);

    if (event_loop)
    {
        return esp_event_isr_post_to(event_loop, BUTTON_EVENT, event_id, event_data, sizeof(*event_data), NULL);
    }
    else
    {
        return esp_event_isr_post(BUTTON_EVENT, event_id, event_data, sizeof(*event_data), NULL);
    }
}

static void BUTTON_IRAM_ATTR handle_button(struct button_context *ctx, int64_t now, int32_t event_id)
{
    assert(ctx);
    ESP_DRAM_LOGD(TAG, "handle pin %d {now=%lld, press_start=%lld, event_id=%d}", ctx->pin, now, ctx->press_start, event_id);

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
    const char *log_event_str = (event_id == BUTTON_EVENT_PRESS) ? DRAM_STR("pressed") : DRAM_STR("released");
#if BUTTON_LONG_PRESS_ENABLE
    ESP_DRAM_LOGI(TAG, "%s pin %d after %d ms {long=%d}", log_event_str, ctx->pin, data.press_length_ms, data.long_press);
#else
    ESP_DRAM_LOGI(TAG, "%s pin %d after %d ms", log_event, state->pin, data.press_length_ms);
#endif

    // Queue event
    button_event_isr_post(ctx->event_loop, event_id, &data);
}

static void button_timer_handler(void *arg)
{
    // Dereference
    struct button_context *ctx = (struct button_context *)arg;
    assert(ctx);

    int level = gpio_get_level(ctx->pin);
    int64_t now = esp_timer_get_time();
    int64_t press_length_ms = (now - ctx->press_start) / 1000L; // us to ms

    if (!is_pressed(ctx, level))
    {
        // Check if button wasn't already released and handled
        if (xSemaphoreTake(ctx->pressed_handle, 0) == pdTRUE)
        {
            // Fire released event
            handle_button(ctx, now, BUTTON_EVENT_RELEASED);
        }
        else
        {
            ESP_LOGD(TAG, "failed to take on %d from timer", ctx->pin);
        }
    }
#if BUTTON_LONG_PRESS_ENABLE
    else if (is_long_press(ctx, press_length_ms))
    {
        // Fire long-press event
        handle_button(ctx, now, BUTTON_EVENT_PRESS);
    }
    else if (ctx->long_press_ms > BUTTON_DEBOUNCE_MS)
    {
        // Start timer again, that will fire long-press event, even when button is not released yet
        int64_t timeout_ms = ctx->long_press_ms - BUTTON_DEBOUNCE_MS;
        if (esp_timer_start_once(ctx->timer, timeout_ms * 1000) == ESP_OK)
        {
            ESP_LOGD(TAG, "timer %d started for %lld ms", ctx->pin, timeout_ms);
        }
    }
#endif

    // Re-enable interrupts
    gpio_intr_enable(ctx->pin);
    ESP_LOGD(TAG, "intr %d enabled", ctx->pin);
}

static void BUTTON_IRAM_ATTR button_interrupt_handler(void *arg)
{
    // Dereference
    struct button_context *ctx = (struct button_context *)arg;
    assert(ctx);

    // Current state
    int64_t now = esp_timer_get_time();
    int level = gpio_get_level(ctx->pin);

    // No further interrupts till timer has finished
    gpio_intr_disable(ctx->pin);
    ESP_DRAM_LOGD(TAG, "intr %d disabled", ctx->pin);

    // FreeRTOS semaphore handling helper
    BaseType_t task_woken = 0;

    if (is_pressed(ctx, level))
    {
        // NOTE since this is edge handler, button was just pressed
        if (xSemaphoreGiveFromISR(ctx->pressed_handle, &task_woken) == pdTRUE)
        {
            // Set start
            ctx->press_start = now;

            // Fire pressed event
            handle_button(ctx, now, BUTTON_EVENT_PRESS);
        }
        else
        {
            ESP_DRAM_LOGD(TAG, "failed to give on %d from isr", ctx->pin);
        }
    }
    else
    {
        // NOTE since this is edge handler, button was just released
        if (xSemaphoreTakeFromISR(ctx->pressed_handle, &task_woken) == pdTRUE)
        {
            // Stop the timer
            esp_timer_stop(ctx->timer);

            // Fire released event
            handle_button(ctx, now, BUTTON_EVENT_RELEASED);
        }
        else
        {
            ESP_DRAM_LOGD(TAG, "failed to take on %d from isr", ctx->pin);
        }
    }

    // Start timer that re-enables interrupts
    // NOTE this will fail if timer is already running
    if (esp_timer_start_once(ctx->timer, BUTTON_DEBOUNCE_MS * 1000) == ESP_OK)
    {
        ESP_DRAM_LOGD(TAG, "timer %d started for %d ms", ctx->pin, BUTTON_DEBOUNCE_MS);
    }

    // Yield (as recommended by FreeRTOS specs)
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
        return ESP_ERR_NO_MEM;
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
    if (context->pressed_handle)
    {
        vSemaphoreDelete(context->pressed_handle);
    }

    // Free memory
    free(context);

    // Success
    ESP_LOGI(TAG, "reset button on pin %d", pin);
    return ESP_OK;
}
