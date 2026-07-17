/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <sdkconfig.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <platform/PlatformManager.h>
#if CHIP_CONFIG_ENABLE_ICD_SERVER
#include <app/icd/server/ICDNotifier.h>
#endif

#include <app_priv.h>
#include <iot_button.h>
#include <button_gpio.h>

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::cluster;

static const char *TAG = "app_driver";

static esp_timer_handle_t factory_reset_timer = NULL;

static int current_number_of_presses_counted = 1;
static bool is_multipress = 0;
static bool is_long_press = false;
static uint8_t idlePosition = 0;

static void notify_icd_network_activity()
{
#if CHIP_CONFIG_ENABLE_ICD_SERVER
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        chip::app::ICDNotifier::GetInstance().NotifyNetworkActivityNotification();
    });
    if (err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "Failed to schedule ICD activity notification: %" CHIP_ERROR_FORMAT, err.Format());
    }
#endif
}

static void driver_set_switch_current_position(uint16_t endpoint_id, uint8_t position)
{
    esp_matter_attr_val_t val = esp_matter_uint8(position);
    esp_err_t err = esp_matter::attribute::update(endpoint_id, Switch::Id, Switch::Attributes::CurrentPosition::Id, &val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Switch CurrentPosition update failed: %s", esp_err_to_name(err));
    }
}

static void factory_reset_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Factory reset triggered (button held for 10s)");
    esp_matter::factory_reset();
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    return err;
}

static void app_driver_button_initial_pressed(void *arg, void *data)
{
    notify_icd_network_activity();
    if (!is_multipress) {
        ESP_LOGI(TAG, "Initial button pressed");
        uint8_t newPosition = 1;
        chip::DeviceLayer::SystemLayer().ScheduleLambda([newPosition]() {
            driver_set_switch_current_position(switch_endpoint_id, newPosition);
            switch_cluster::event::send_initial_press(switch_endpoint_id, newPosition);
        });
        is_multipress = 1;
    }
}

static void app_driver_button_release(void *arg, void *data)
{
    notify_icd_network_activity();
    esp_timer_stop(factory_reset_timer);
    bool was_long_press = is_long_press;
    is_long_press = false;
    uint8_t previousPosition = 1;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([was_long_press, previousPosition]() {
        driver_set_switch_current_position(switch_endpoint_id, idlePosition);
        if (was_long_press) {
            switch_cluster::event::send_long_release(switch_endpoint_id, previousPosition);
        } else {
            switch_cluster::event::send_short_release(switch_endpoint_id, previousPosition);
        }
    });
}

static void app_driver_button_long_pressed(void *arg, void *data)
{
    notify_icd_network_activity();
    ESP_LOGI(TAG, "Long button pressed, starting factory reset timer (9s remaining)");
    is_long_press = true;
    esp_timer_start_once(factory_reset_timer, 9000000);
    uint8_t newPosition = 1;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([newPosition]() {
        driver_set_switch_current_position(switch_endpoint_id, newPosition);
        switch_cluster::event::send_long_press(switch_endpoint_id, newPosition);
    });
}

static void app_driver_button_multipress_ongoing(void *arg, void *data)
{
    notify_icd_network_activity();
    ESP_LOGI(TAG, "Multipress Ongoing");
    uint8_t newPosition = 1;
    current_number_of_presses_counted++;
    uint16_t endpoint_id = switch_endpoint_id;
    uint32_t cluster_id = Switch::Id;
    uint32_t attribute_id = Switch::Attributes::FeatureMap::Id;

    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);

    uint32_t feature_map = val.val.u32;
    uint32_t msm_feature_map = switch_cluster::feature::momentary_switch_multi_press::get_id();
    uint32_t as_feature_map = switch_cluster::feature::action_switch::get_id();
    if (((feature_map & msm_feature_map) == msm_feature_map) && ((feature_map & as_feature_map) != as_feature_map)) {
        chip::DeviceLayer::SystemLayer().ScheduleLambda([newPosition]() {
            driver_set_switch_current_position(switch_endpoint_id, newPosition);
            switch_cluster::event::send_multi_press_ongoing(switch_endpoint_id, newPosition, current_number_of_presses_counted);
        });
    }
}

static void app_driver_button_multipress_complete(void *arg, void *data)
{
    notify_icd_network_activity();
    esp_timer_stop(factory_reset_timer);
    ESP_LOGI(TAG, "Multipress Complete");
    uint8_t previousPosition = 1;
    uint16_t endpoint_id = switch_endpoint_id;
    uint32_t cluster_id = Switch::Id;
    uint32_t attribute_id = Switch::Attributes::MultiPressMax::Id;

    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);
    uint8_t multipress_max = val.val.u8;
    int total_number_of_presses_counted = (current_number_of_presses_counted > multipress_max) ? 0 : current_number_of_presses_counted;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([previousPosition, total_number_of_presses_counted]() {
        driver_set_switch_current_position(switch_endpoint_id, idlePosition);
        switch_cluster::event::send_multi_press_complete(switch_endpoint_id, previousPosition, total_number_of_presses_counted);
        current_number_of_presses_counted = 1;
    });
    is_multipress = 0;
}

app_driver_handle_t app_driver_button_init()
{
    /* Initialize factory reset timer */
    const esp_timer_create_args_t timer_args = {
        .callback = factory_reset_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "factory_reset",
    };
    esp_timer_create(&timer_args, &factory_reset_timer);

    /* Initialize button */
    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
    };
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = BUTTON_GPIO_PIN,
        .active_level = 0,
        .enable_power_save = true,
    };

    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device");
        return NULL;
    }

    iot_button_register_cb(handle, BUTTON_PRESS_DOWN, NULL, app_driver_button_initial_pressed, NULL);
    iot_button_register_cb(handle, BUTTON_PRESS_UP, NULL, app_driver_button_release, NULL);
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, NULL, app_driver_button_long_pressed, NULL);
    iot_button_register_cb(handle, BUTTON_PRESS_REPEAT, NULL, app_driver_button_multipress_ongoing, NULL);
    iot_button_register_cb(handle, BUTTON_PRESS_REPEAT_DONE, NULL, app_driver_button_multipress_complete, NULL);

    return (app_driver_handle_t)handle;
}
