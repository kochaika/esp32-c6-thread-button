/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <log_heap_numbers.h>

#include <app_priv.h>
#include <app/util/attribute-storage.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

static const char *TAG = "app_main";
uint16_t switch_endpoint_id = 0;
uint16_t switch2_endpoint_id = 0;
uint16_t power_source_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace esp_matter::cluster;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

namespace {
// Please refer to https://github.com/CHIP-Specifications/connectedhomeip-spec/blob/master/src/namespaces
constexpr const uint8_t kNamespaceSwitches = 0x43;
// Switches Namespace: 0x43, tag 0 (On)
constexpr const uint8_t kTagSwitchOn = 0;
// Common Position Namespace: 8, tag: 0 (Left), tag: 1 (Right)
constexpr const uint8_t kNamespacePosition = 8;
constexpr const uint8_t kTagPositionLeft = 0;
constexpr const uint8_t kTagPositionRight = 1;

// Button 1 is tagged "Left", button 2 "Right", so controllers can tell the two
// otherwise-identical switches apart.
const Descriptor::Structs::SemanticTagStruct::Type gEp1TagList[] = {
    {.namespaceID = kNamespaceSwitches, .tag = kTagSwitchOn},
    {.namespaceID = kNamespacePosition, .tag = kTagPositionLeft}
};
const Descriptor::Structs::SemanticTagStruct::Type gEp2TagList[] = {
    {.namespaceID = kNamespaceSwitches, .tag = kTagSwitchOn},
    {.namespaceID = kNamespacePosition, .tag = kTagPositionRight}
};
}

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed successfully");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager  &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
            if (!commissionMgr.IsCommissioningWindowOpen()) {
                /* After removing last fabric, this example does not remove the Wi-Fi credentials
                 * and still has IP connectivity so, only advertising on DNS-SD.
                 */
                CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                                            chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        MEMORY_PROFILER_DUMP_HEAP_STAT("BLE deinitialized");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        /* Driver update */
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }

    return err;
}

// Create a Generic Switch endpoint configured as a classic momentary switch
// (MS | MSR | MSL | MSM). Feature order matters: MSR must be added before MSL
// and MSM, which both require Momentary Switch Release to already be present.
static endpoint_t *create_generic_switch_endpoint(node_t *node, app_driver_handle_t button_handle)
{
    generic_switch::config_t switch_config;
    switch_config.switch_cluster.feature_flags =
        cluster::switch_cluster::feature::momentary_switch::get_id();

    endpoint_t *endpoint = generic_switch::create(node, &switch_config, ENDPOINT_FLAG_NONE, button_handle);
    if (endpoint == nullptr) {
        return nullptr;
    }

    /* Add descriptor tag_list feature */
    cluster_t *descriptor = cluster::get(endpoint, Descriptor::Id);
    descriptor::feature::tag_list::add(descriptor);

    /* Add switch features (classic momentary switch model, no Action Switch) */
    cluster_t *switch_cluster_handle = cluster::get(endpoint, Switch::Id);
    cluster::switch_cluster::feature::momentary_switch_release::add(switch_cluster_handle);
    cluster::switch_cluster::feature::momentary_switch_long_press::add(switch_cluster_handle);
    cluster::switch_cluster::feature::momentary_switch_multi_press::config_t msm;
    msm.multi_press_max = 5;
    cluster::switch_cluster::feature::momentary_switch_multi_press::add(switch_cluster_handle, &msm);

    return endpoint;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Select external UFL antenna via FM8625H RF switch on XIAO ESP32-C6 */
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_3, 0);   /* Enable RF switch */
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 1);  /* Select external antenna */

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {};
    pm_config.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    pm_config.min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
    pm_config.light_sleep_enable = true;
#endif
    err = esp_pm_configure(&pm_config);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to configure power management, err:%d", err));
#endif

    MEMORY_PROFILER_DUMP_HEAP_STAT("Bootup");

    /* Per-button contexts persist for the app lifetime (static). */
    static button_ctx_t button1_ctx = {};
    static button_ctx_t button2_ctx = {};

    /* Initialize button drivers */
    app_driver_handle_t button1_handle = app_driver_button_init(BUTTON_GPIO_PIN, &button1_ctx);
    ABORT_APP_ON_FAILURE(button1_handle != nullptr, ESP_LOGE(TAG, "Failed to initialize button 1"));
    app_driver_handle_t button2_handle = app_driver_button_init(BUTTON2_GPIO_PIN, &button2_ctx);
    ABORT_APP_ON_FAILURE(button2_handle != nullptr, ESP_LOGE(TAG, "Failed to initialize button 2"));

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;

    // node handle can be used to add/modify other endpoints.
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    MEMORY_PROFILER_DUMP_HEAP_STAT("node created");

    /* Create one Generic Switch endpoint per button (identical behavior) */
    endpoint_t *switch1_ep = create_generic_switch_endpoint(node, button1_handle);
    ABORT_APP_ON_FAILURE(switch1_ep != nullptr, ESP_LOGE(TAG, "Failed to create generic switch endpoint 1"));
    switch_endpoint_id = endpoint::get_id(switch1_ep);
    button1_ctx.endpoint_id = switch_endpoint_id;
    ESP_LOGI(TAG, "Generic Switch 1 created with endpoint_id %d", switch_endpoint_id);

    /* Second Generic Switch endpoint (button 2) on ep2. */
    endpoint_t *switch2_ep = create_generic_switch_endpoint(node, button2_handle);
    ABORT_APP_ON_FAILURE(switch2_ep != nullptr, ESP_LOGE(TAG, "Failed to create generic switch endpoint 2"));
    switch2_endpoint_id = endpoint::get_id(switch2_ep);
    button2_ctx.endpoint_id = switch2_endpoint_id;
    ESP_LOGI(TAG, "Generic Switch 2 created with endpoint_id %d", switch2_endpoint_id);

    /* Create the Power Source endpoint (device type 0x0011) last, so the two
     * buttons occupy ep1/ep2 and the battery is on ep3.
     *
     * IMPORTANT: Matter endpoint ids must stay stable for a given commissioned
     * device — controllers cache which endpoint is which. If you change this
     * layout (add/reorder endpoints) on an already-commissioned device you must
     * re-commission it (factory reset + re-add) so controllers re-learn it,
     * otherwise entities like the battery show up as "Unavailable".
     *
     * BatPercentRemaining is created here with a placeholder value and then kept
     * up to date by the battery ADC driver. */
    endpoint::power_source::config_t power_source_config;
    power_source_config.power_source.status = 1;  /* PowerSourceStatus: Active */
    power_source_config.power_source.order = 0;
    power_source_config.power_source.feature_flags =
        cluster::power_source::feature::battery::get_id();
    /* Battery-feature mandatory attributes */
    power_source_config.power_source.features.battery.bat_charge_level = 0;      /* BatChargeLevel: OK */
    power_source_config.power_source.features.battery.bat_replacement_needed = false;
    power_source_config.power_source.features.battery.bat_replaceability = 1;    /* NotReplaceable */

    endpoint_t *power_source_ep =
        endpoint::power_source::create(node, &power_source_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(power_source_ep != nullptr, ESP_LOGE(TAG, "Failed to create power source endpoint"));

    /* Associate the battery with the first switch endpoint */
    endpoint::set_parent_endpoint(power_source_ep, switch1_ep);

    /* BatPercentRemaining is optional and in half-percent units (0-200).
     * Placeholder value 154 = 77%; overwritten by the first real ADC reading. */
    cluster_t *power_source_cluster = cluster::get(power_source_ep, PowerSource::Id);
    cluster::power_source::attribute::create_bat_percent_remaining(power_source_cluster, nullable<uint8_t>(154),
                                                                   nullable<uint8_t>(0), nullable<uint8_t>(200));

    power_source_endpoint_id = endpoint::get_id(power_source_ep);
    ESP_LOGI(TAG, "Power Source (battery) created with endpoint_id %d", power_source_endpoint_id);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD && CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
    // Enable secondary network interface
    secondary_network_interface::config_t secondary_network_interface_config;
    endpoint_t *secondary_network_interface_ep = endpoint::secondary_network_interface::create(node, &secondary_network_interface_config, ENDPOINT_FLAG_NONE, nullptr);
    ABORT_APP_ON_FAILURE(secondary_network_interface_ep != nullptr, ESP_LOGE(TAG, "Failed to create secondary network interface endpoint"));
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    auto * dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
    static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
    static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    MEMORY_PROFILER_DUMP_HEAP_STAT("matter started");

    /* Set semantic tags on both switch endpoints */
    endpoint::set_semantic_tags(endpoint::get(switch_endpoint_id), gEp1TagList, 2);
    endpoint::set_semantic_tags(endpoint::get(switch2_endpoint_id), gEp2TagList, 2);

    /* Start battery ADC sampling now that Matter is up and the Power Source
     * endpoint exists (the driver updates BatPercentRemaining on that endpoint). */
    err = app_driver_battery_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize battery driver, err:%d", err);
    }

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
#if CONFIG_OPENTHREAD_CLI
    esp_matter::console::otcli_register_commands();
#endif
    esp_matter::console::init();
#endif
}
