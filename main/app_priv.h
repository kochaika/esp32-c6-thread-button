/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <esp_timer.h>
#include <hal/gpio_types.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#endif

#define BUTTON_GPIO_PIN  ((gpio_num_t)CONFIG_BUTTON_GPIO)
#define BUTTON2_GPIO_PIN ((gpio_num_t)CONFIG_BUTTON2_GPIO)

/* Battery voltage divider ratio (V_battery / V_adc). For two equal resistors
 * this is 2. Adjust if you use an unequal divider. */
#define BATTERY_DIVIDER_RATIO 2

extern uint16_t switch_endpoint_id;
extern uint16_t switch2_endpoint_id;
extern uint16_t power_source_endpoint_id;

typedef void *app_driver_handle_t;

/** Per-button runtime state.
 *
 * Each physical button owns one of these so the two buttons keep fully
 * independent press/multipress/long-press state and their own factory-reset
 * timer. A pointer to it is passed to the button callbacks as user data.
 */
typedef struct {
    uint16_t endpoint_id;                 /* Matter Generic Switch endpoint */
    esp_timer_handle_t factory_reset_timer;
    int press_count;                      /* presses counted in the current multipress */
    bool is_multipress;                   /* a press sequence is in progress */
    bool is_long_press;                   /* current press has crossed the long-press threshold */
} button_ctx_t;

/** Initialize a button on the given GPIO.
 *
 * Registers the switch callbacks against @p ctx, which must outlive the button
 * (e.g. a static/global). The context's endpoint_id should be assigned by the
 * caller once the matching endpoint has been created.
 *
 * @param[in] gpio Active-low button GPIO.
 * @param[in] ctx  Per-button context (zero-initialized by the caller).
 *
 * @return Handle on success.
 * @return NULL in case of failure.
 */
app_driver_handle_t app_driver_button_init(gpio_num_t gpio, button_ctx_t *ctx);

/** Initialize the battery ADC driver.
 *
 * Configures the ADC channel on CONFIG_BATTERY_ADC_GPIO, takes an initial
 * reading, and starts a periodic sampling timer that updates the Power Source
 * cluster's BatPercentRemaining attribute. Must be called after Matter has
 * started and after power_source_endpoint_id has been set.
 *
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t app_driver_battery_init(void);

/** Driver Update
 *
 * This API should be called to update the driver for the attribute being updated.
 * This is usually called from the common `app_attribute_update_cb()`.
 *
 * @param[in] endpoint_id Endpoint ID of the attribute.
 * @param[in] cluster_id Cluster ID of the attribute.
 * @param[in] attribute_id Attribute ID of the attribute.
 * @param[in] val Pointer to `esp_matter_attr_val_t`. Use appropriate elements as per the value type.
 *
 * @return ESP_OK on success.
 * @return error in case of failure.
 */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
