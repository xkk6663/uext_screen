set(priv_requires esp_timer tinyusb)   # usb_device_uac.c includes tusb.h (IDF v6 enforces include ownership)

if(${IDF_VERSION_MAJOR} LESS 6)
    list(APPEND priv_requires usb)       # USB PHY is part of usb component in IDF < 6.0
endif()

idf_component_register(SRCS usb_device_uac.c
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES ${priv_requires})

if(NOT CONFIG_USB_DEVICE_UAC_AS_PART)
        # Determine whether tinyusb is fetched from component registry or from local path
    idf_build_get_property(build_components BUILD_COMPONENTS)
    if(tinyusb IN_LIST build_components)
        set(tinyusb_name tinyusb) # Local component
    else()
        set(tinyusb_name espressif__tinyusb) # Managed component
    endif()

    idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
    target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}/tusb" "${COMPONENT_DIR}/tusb_uac")
    target_sources(${tusb_lib} PUBLIC "${COMPONENT_DIR}/tusb/usb_descriptors.c")
endif()


# Replaces the stripped cmake_utilities/package_manager hook (see button
# component for the same pattern): version macros used by usb_device_uac.c:544.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    USB_DEVICE_UAC_VER_MAJOR=1
    USB_DEVICE_UAC_VER_MINOR=3
    USB_DEVICE_UAC_VER_PATCH=1)
