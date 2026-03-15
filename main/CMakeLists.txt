idf_component_register(SRC_DIRS          "."
                       PRIV_INCLUDE_DIRS  "." "${ESP_MATTER_PATH}/examples/common/utils")

if (CONFIG_ENABLE_SET_CERT_DECLARATION_API)
    target_add_binary_data(${COMPONENT_TARGET} "certification_declaration/certification_declaration.der" BINARY)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

# Override vendor and product names
idf_build_set_property(COMPILE_DEFINITIONS "-DCHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME=\"ChaikaMatter\"" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DCHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME=\"Smart_Button\"" APPEND)
#idf_build_set_property(COMPILE_DEFINITIONS "-DCONFIG_BUTTON_LONG_PRESS_TIME_MS=2500" APPEND)
