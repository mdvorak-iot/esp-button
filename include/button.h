#pragma once

#include <esp_err.h>
#include <hal/gpio_types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BUTTON_DEBOUNCE_MS
/**
 * @brief Button debounce interval, in ms.
 */
#define BUTTON_DEBOUNCE_MS CONFIG_BUTTON_DEBOUNCE_MS
#endif
#ifndef BUTTON_LONG_PRESS_ENABLE
/**
 * @brief Flag whether long-press code should be included.
 *
 * Note that long press still have to be enabled for each button by setting the delay.
 */
#define BUTTON_LONG_PRESS_ENABLE CONFIG_BUTTON_LONG_PRESS_ENABLE
#endif

/**
 * @brief Selects logic level of pressed button.
 */
enum button_level
{
    /**
     * @brief GPIO logic level is low on button press, high when released.
     */
    BUTTON_LEVEL_LOW_ON_PRESS = 0,
    /**
     * @brief GPIO logic level is high on button press, low when released.
     */
    BUTTON_LEVEL_HIGH_ON_PRESS = 1,
};

/**
 * @brief Type of the button event.
 */
enum button_event
{
    /**
     * @brief Button was released.
     */
    BUTTON_EVENT_RELEASED = 0,
    /**
     * @brief Button is currently being pressed.
     */
    BUTTON_EVENT_PRESSED = 1,
};

/**
 * @brief Button event data.
 */
struct button_data
{
    /**
     * @brief Button event type.
     * Can be used to share callback function between pressed and released events.
     */
    enum button_event event;
    /**
     * @brief Button GPIO. Can be used to distinguish different buttons in one callback function.
     */
    gpio_num_t pin;
    /**
     * @brief Time since button was pressed. For initial BUTTON_EVENT_PRESSED, it is always 0.
     */
    uint32_t press_length_ms;
#if BUTTON_LONG_PRESS_ENABLE
    /**
     * @brief Set to true if press_length_ms is higher then configured long_press interval.
     */
    bool long_press;
#endif
};

/**
 * @brief Button event callback function.
 *
 * @param arg Custom argument set during button config.
 * @param data Button event data. Never NULL.
 */
typedef void (*button_callback_fn)(void *arg, const struct button_data *data);

/**
 * @brief Button configuration. At least one callback must be set.
 */
struct button_config
{
    /**
     * @brief Logic level of the pressed button.
     */
    enum button_level level;
    /**
     * @brief Enable internal button pull-up or pull-down resistor.
     *
     * When set true, its function depends on level config.
     * For BUTTON_LEVEL_LOW_ON_PRESS it will enable pull-up,
     * for BUTTON_LEVEL_HIGH_ON_PRESS it will enable pull-down.
     */
    bool internal_pull;
#if BUTTON_LONG_PRESS_ENABLE
    /**
     * @brief Long-press interval. Set to 0 to disable for this button.
     *
     * Long-press event will be fired after specified interval
     * as BUTTON_EVENT_PRESSED with long_press flag set to true.
     */
    uint32_t long_press_ms;
#endif
    /**
     * @brief Fire BUTTON_EVENT_PRESSED event every BUTTON_DEBOUNCE_MS interval,
     * until button is released.
     */
    bool continuous_callback;
    /**
     * @brief Button pressed callback. This is should be called once
     * after button is pressed, and possibly again for long-press,
     * unless continuous_callback is set to true.
     */
    button_callback_fn on_press;
    /**
     * @brief Button released callback. This is called exactly once per button press.
     */
    button_callback_fn on_release;
    /**
     * @brief Custom argument for callback functions. Can be NULL.
     */
    void *arg;
};

/**
 * @brief Configured button context.
 */
typedef struct button_context *button_context_ptr;

/**
 * @brief Initializes button with given config.
 *
 * Button must have at least one callback function set.
 *
 * @param pin Button GPIO.
 * @param cfg Button configuration. Mandatory.
 * @param ctx_out Output argument, it will be set to context used to further button manipulation.
 *                Can be NULL if not needed.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t button_config(gpio_num_t pin, const struct button_config *cfg, button_context_ptr *ctx_out);

/**
 * @brief De-initializes previously configured button.
 *
 * @param ctx Button context. Mandatory.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t button_remove(button_context_ptr ctx);

/**
 * @brief Suspends button callbacks until button_resume is called.
 *
 * If button is currently pressed and it is released while button is suspended,
 * BUTTON_EVENT_RELEASED should be called after button is re-enabled.
 *
 * See button_suspend_for for additional information.
 *
 * @param ctx Button context. Mandatory.
 * @return ESP_OK on success, error otherwise.
 * @see button_suspend_for
 */
esp_err_t button_suspend(button_context_ptr ctx);

/**
 * @brief Suspends button callbacks until timeout passes, or the button_resume is called.
 *
 * If button is currently pressed and it is released while button is suspended,
 * BUTTON_EVENT_RELEASED should be called after button is re-enabled.
 *
 * Note that this might in rare cases fail if button is pressed during execution of this function.
 * There are few other unlikely race-conditions, which might cause a press being skipped after
 * resume, or press being called twice. Normally you should not encounter those, file an issue if you do.
 *
 * @param ctx Button context. Mandatory.
 * @param timeout_ms Interval for which button should be suspended. It will be automatically re-enabled
 *                   afterwards. Set to -1 for infinite suspend.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t button_suspend_for(button_context_ptr ctx, int32_t timeout_ms);

/**
 * @brief Resumes suspended button.
 *
 * This might restart debounce timer if called on already enabled button.
 *
 * @param ctx Button context. Mandatory.
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t button_resume(button_context_ptr ctx);

#ifdef __cplusplus
}
#endif
