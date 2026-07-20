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

#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

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

/* Switch "released"/idle current position reported to the Switch cluster. */
static const uint8_t k_idle_position = 0;
/* Switch "pressed" current position. */
static const uint8_t k_pressed_position = 1;

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

/* ------------------------------------------------------------------------- */
/* Button callbacks — all state lives in the per-button context passed as     */
/* the callback's user data, so the two buttons never share state.            */
/* ------------------------------------------------------------------------- */

static void app_driver_button_initial_pressed(void *arg, void *data)
{
    button_ctx_t *ctx = (button_ctx_t *)data;
    notify_icd_network_activity();
    if (!ctx->is_multipress) {
        ESP_LOGI(TAG, "[ep %u] Initial button pressed", ctx->endpoint_id);
        uint16_t endpoint_id = ctx->endpoint_id;
        chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id]() {
            driver_set_switch_current_position(endpoint_id, k_pressed_position);
            switch_cluster::event::send_initial_press(endpoint_id, k_pressed_position);
        });
        ctx->is_multipress = true;
    }
}

static void app_driver_button_release(void *arg, void *data)
{
    button_ctx_t *ctx = (button_ctx_t *)data;
    notify_icd_network_activity();
    esp_timer_stop(ctx->factory_reset_timer);
    bool was_long_press = ctx->is_long_press;
    ctx->is_long_press = false;
    uint16_t endpoint_id = ctx->endpoint_id;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, was_long_press]() {
        driver_set_switch_current_position(endpoint_id, k_idle_position);
        if (was_long_press) {
            switch_cluster::event::send_long_release(endpoint_id, k_pressed_position);
        } else {
            switch_cluster::event::send_short_release(endpoint_id, k_pressed_position);
        }
    });
}

static void app_driver_button_long_pressed(void *arg, void *data)
{
    button_ctx_t *ctx = (button_ctx_t *)data;
    notify_icd_network_activity();
    ESP_LOGI(TAG, "[ep %u] Long button pressed, starting factory reset timer (9s remaining)", ctx->endpoint_id);
    ctx->is_long_press = true;
    esp_timer_start_once(ctx->factory_reset_timer, 9000000);
    uint16_t endpoint_id = ctx->endpoint_id;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id]() {
        driver_set_switch_current_position(endpoint_id, k_pressed_position);
        switch_cluster::event::send_long_press(endpoint_id, k_pressed_position);
    });
}

static void app_driver_button_multipress_ongoing(void *arg, void *data)
{
    button_ctx_t *ctx = (button_ctx_t *)data;
    notify_icd_network_activity();
    ESP_LOGI(TAG, "[ep %u] Multipress Ongoing", ctx->endpoint_id);
    ctx->press_count++;
    uint16_t endpoint_id = ctx->endpoint_id;

    attribute_t *attribute = attribute::get(endpoint_id, Switch::Id, Switch::Attributes::FeatureMap::Id);
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);

    uint32_t feature_map = val.val.u32;
    uint32_t msm_feature_map = switch_cluster::feature::momentary_switch_multi_press::get_id();
    uint32_t as_feature_map = switch_cluster::feature::action_switch::get_id();
    if (((feature_map & msm_feature_map) == msm_feature_map) && ((feature_map & as_feature_map) != as_feature_map)) {
        int press_count = ctx->press_count;
        chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, press_count]() {
            driver_set_switch_current_position(endpoint_id, k_pressed_position);
            switch_cluster::event::send_multi_press_ongoing(endpoint_id, k_pressed_position, press_count);
        });
    }
}

static void app_driver_button_multipress_complete(void *arg, void *data)
{
    button_ctx_t *ctx = (button_ctx_t *)data;
    notify_icd_network_activity();
    esp_timer_stop(ctx->factory_reset_timer);
    ESP_LOGI(TAG, "[ep %u] Multipress Complete", ctx->endpoint_id);
    uint16_t endpoint_id = ctx->endpoint_id;

    attribute_t *attribute = attribute::get(endpoint_id, Switch::Id, Switch::Attributes::MultiPressMax::Id);
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);
    uint8_t multipress_max = val.val.u8;
    int total_number_of_presses_counted = (ctx->press_count > multipress_max) ? 0 : ctx->press_count;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, total_number_of_presses_counted]() {
        driver_set_switch_current_position(endpoint_id, k_idle_position);
        switch_cluster::event::send_multi_press_complete(endpoint_id, k_pressed_position, total_number_of_presses_counted);
    });
    ctx->press_count = 1;
    ctx->is_multipress = false;
}

app_driver_handle_t app_driver_button_init(gpio_num_t gpio, button_ctx_t *ctx)
{
    /* First press of a multipress sequence counts as 1. */
    ctx->press_count = 1;

    /* Per-button factory reset timer (started on long press, so either button
     * held for the full 10s independently triggers a factory reset). */
    const esp_timer_create_args_t timer_args = {
        .callback = factory_reset_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "factory_reset",
    };
    esp_timer_create(&timer_args, &ctx->factory_reset_timer);

    /* Initialize button */
    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
    };
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = gpio,
        .active_level = 0,
        .enable_power_save = true,
    };

    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device on GPIO %d", (int)gpio);
        return NULL;
    }

    iot_button_register_cb(handle, BUTTON_PRESS_DOWN, NULL, app_driver_button_initial_pressed, ctx);
    iot_button_register_cb(handle, BUTTON_PRESS_UP, NULL, app_driver_button_release, ctx);
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, NULL, app_driver_button_long_pressed, ctx);
    iot_button_register_cb(handle, BUTTON_PRESS_REPEAT, NULL, app_driver_button_multipress_ongoing, ctx);
    iot_button_register_cb(handle, BUTTON_PRESS_REPEAT_DONE, NULL, app_driver_button_multipress_complete, ctx);

    return (app_driver_handle_t)handle;
}

/* ------------------------------------------------------------------------- */
/* Battery reading — periodic ADC sample of the divider on CONFIG_BATTERY_    */
/* ADC_GPIO, mapped to a Li-Po state-of-charge percentage and pushed to the   */
/* Power Source cluster.                                                      */
/* ------------------------------------------------------------------------- */

/* Sample every 30 minutes: battery voltage changes slowly and every report
 * that forces the LIT ICD radio awake costs energy. */
#define BATTERY_SAMPLE_INTERVAL_US (30ULL * 60 * 1000 * 1000)
/* Only report when the percentage moves by at least this much. */
#define BATTERY_REPORT_DELTA_PCT 3
/* Number of raw ADC reads averaged per sample to suppress noise. */
#define BATTERY_ADC_SAMPLES 16

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_adc_cali_enabled = false;
static adc_unit_t s_batt_unit;
static adc_channel_t s_batt_channel;
static esp_timer_handle_t s_battery_timer = NULL;
static int s_last_reported_pct = -1;

/* Resting Li-Po (single cell) open-circuit voltage vs. state of charge.
 * Descending by voltage; linearly interpolated between points. Approximate —
 * tune for your cell/load. */
typedef struct {
    int mv;
    uint8_t pct;
} batt_point_t;

static const batt_point_t k_lipo_curve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80},
    {3980, 75},  {3950, 70}, {3910, 65}, {3870, 60}, {3850, 55},
    {3840, 50},  {3820, 45}, {3800, 40}, {3790, 35}, {3770, 30},
    {3750, 25},  {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},
    {3500, 0},
};

static uint8_t battery_mv_to_percent(int mv)
{
    const size_t n = sizeof(k_lipo_curve) / sizeof(k_lipo_curve[0]);
    if (mv >= k_lipo_curve[0].mv) {
        return 100;
    }
    if (mv <= k_lipo_curve[n - 1].mv) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if (mv >= k_lipo_curve[i].mv) {
            const batt_point_t *hi = &k_lipo_curve[i - 1];
            const batt_point_t *lo = &k_lipo_curve[i];
            int span_mv = hi->mv - lo->mv;
            int span_pct = hi->pct - lo->pct;
            return (uint8_t)(lo->pct + ((mv - lo->mv) * span_pct + span_mv / 2) / span_mv);
        }
    }
    return 0;
}

static void battery_report_percent(uint8_t percent)
{
    /* BatPercentRemaining is in half-percent units (0-200). */
    uint8_t half_percent = (uint8_t)(percent * 2);
    uint16_t endpoint_id = power_source_endpoint_id;
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, half_percent]() {
        esp_matter_attr_val_t val = esp_matter_nullable_uint8(nullable<uint8_t>(half_percent));
        esp_err_t err = esp_matter::attribute::update(endpoint_id, PowerSource::Id,
                                                      PowerSource::Attributes::BatPercentRemaining::Id, &val);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BatPercentRemaining update failed: %s", esp_err_to_name(err));
        }
    });
}

static void battery_sample_and_report(void *arg)
{
    if (!s_adc_handle) {
        return;
    }

    /* Average several raw reads to suppress ADC noise (matters with the
     * high-impedance 1 MΩ divider). */
    int raw_sum = 0;
    int samples = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        int raw_i = 0;
        if (adc_oneshot_read(s_adc_handle, s_batt_channel, &raw_i) == ESP_OK) {
            raw_sum += raw_i;
            samples++;
        }
    }
    if (samples == 0) {
        ESP_LOGW(TAG, "ADC read failed for all %d samples", BATTERY_ADC_SAMPLES);
        return;
    }
    int raw = raw_sum / samples;

    int node_mv = 0;
    if (s_adc_cali_enabled) {
        adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &node_mv);
    } else {
        /* Rough fallback: 12-bit range, ~3100 mV full scale at 12 dB atten. */
        node_mv = raw * 3100 / 4095;
    }
    int battery_mv = node_mv * BATTERY_DIVIDER_RATIO;
    uint8_t percent = battery_mv_to_percent(battery_mv);

    ESP_LOGI(TAG, "Battery: raw=%d, node=%d mV, battery=%d mV -> %u%%", raw, node_mv, battery_mv, percent);

    if (s_last_reported_pct < 0 || abs((int)percent - s_last_reported_pct) >= BATTERY_REPORT_DELTA_PCT) {
        s_last_reported_pct = percent;
        battery_report_percent(percent);
    }
}

esp_err_t app_driver_battery_init(void)
{
    /* Map the configured GPIO to its ADC unit/channel. */
    esp_err_t err = adc_oneshot_io_to_channel(CONFIG_BATTERY_ADC_GPIO, &s_batt_unit, &s_batt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d is not ADC-capable: %s", CONFIG_BATTERY_ADC_GPIO, esp_err_to_name(err));
        return err;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = s_batt_unit,
    };
    err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc_handle, s_batt_channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Curve-fitting calibration (supported on ESP32-C6). Falls back to a rough
     * linear conversion if calibration is unavailable. */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_batt_unit,
        .chan = s_batt_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_handle) == ESP_OK) {
        s_adc_cali_enabled = true;
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable; using rough conversion");
    }

    /* Periodic sampling timer. */
    const esp_timer_create_args_t timer_args = {
        .callback = battery_sample_and_report,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "battery_sample",
    };
    err = esp_timer_create(&timer_args, &s_battery_timer);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_timer_start_periodic(s_battery_timer, BATTERY_SAMPLE_INTERVAL_US);
    if (err != ESP_OK) {
        return err;
    }

    /* Take an initial reading immediately so HA gets a real value promptly. */
    battery_sample_and_report(NULL);
    return ESP_OK;
}
