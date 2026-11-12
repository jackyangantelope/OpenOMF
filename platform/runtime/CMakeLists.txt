if(NOT TARGET jack_wave1_runtime)
    set(JACK_WAVE1_DIR "${CMAKE_CURRENT_LIST_DIR}")
    set(JACK_PICO_SHARED "${PORT_ROOT}/deps/pico_shared")

    add_subdirectory("${JACK_PICO_SHARED}/drivers/pico_fatfs"
                     "${CMAKE_BINARY_DIR}/jack_pico_fatfs")
    if(JACK_WAVE1_HDMI_AUDIO)
        if(JACK_WAVE1_HDMI_DRIVER_ROOT)
            set(JACK_HDMI_ROOT "${JACK_WAVE1_HDMI_DRIVER_ROOT}")
        else()
            set(JACK_HDMI_ROOT "${JACK_PICO_SHARED}/drivers/pico_hdmi")
        endif()
        if(NOT JACK_WAVE1_HDMI_ADAPTER)
            set(JACK_WAVE1_HDMI_ADAPTER "${JACK_WAVE1_DIR}/hdmi/jack_hdmi.c")
        endif()
        add_library(jack_wave1_hdmi INTERFACE)
        if(NOT JACK_WAVE1_HDMI_VIDEO_SOURCE)
            set(JACK_WAVE1_HDMI_VIDEO_SOURCE "${JACK_HDMI_ROOT}/video_output.c")
        endif()
        target_sources(jack_wave1_hdmi INTERFACE
            "${JACK_WAVE1_HDMI_VIDEO_SOURCE}"
            "${JACK_HDMI_ROOT}/hstx_packet.c"
            "${JACK_WAVE1_HDMI_ADAPTER}"
            "${JACK_WAVE1_DIR}/hdmi/jack_di_queue.c")
        target_include_directories(jack_wave1_hdmi INTERFACE "${JACK_HDMI_ROOT}")
        target_compile_definitions(jack_wave1_hdmi INTERFACE NOHSTXCLOCK=1)
        if(JACK_WAVE1_DEFER_HDMI_AUDIO)
            target_compile_definitions(jack_wave1_hdmi INTERFACE
                JACK_WAVE1_DEFER_HDMI_AUDIO=1)
        endif()
        set(JACK_VIDEO_TARGET jack_wave1_hdmi)
    else()
        add_subdirectory("${JACK_PICO_SHARED}/drivers/pico_hstx"
                         "${CMAKE_BINARY_DIR}/jack_pico_hstx")
        set(JACK_VIDEO_TARGET pico_hstx)
    endif()
    # The SDK helper expands TinyUSB sources through its TOP variable. It is
    # not preserved when pico_sdk_init() returns, so scope it explicitly here.
    set(JACK_SAVED_TINYUSB_TOP "${TOP}")
    set(TOP "${PICO_SDK_PATH}/lib/tinyusb")
    check_and_add_pico_pio_usb_support()
    set(TOP "${JACK_SAVED_TINYUSB_TOP}")
    if(NOT TARGET tinyusb_pico_pio_usb)
        message(FATAL_ERROR "Pico-PIO-USB support could not be configured: ${PICO_PIO_USB_PATH}")
    endif()

    add_library(jack_wave1_runtime STATIC
        "${JACK_WAVE1_DIR}/jack_wave1_runtime.cpp"
        "${JACK_WAVE1_DIR}/jack_wave1_usb.cpp"
    )
    target_include_directories(jack_wave1_runtime PUBLIC
        "${JACK_WAVE1_DIR}"
        "${JACK_PICO_SHARED}"
        "${JACK_PICO_SHARED}/drivers/pico_fatfs"
        "${JACK_PICO_SHARED}/drivers/pico_fatfs/fatfs"
    )
    target_compile_definitions(jack_wave1_runtime PUBLIC
        HW_CONFIG=8
        SDCARD_PIN_CS=39
        SDCARD_PIN_SCK=34
        SDCARD_PIN_MOSI=35
        SDCARD_PIN_MISO=36
        SDCARD_PIO=pio0
        SDCARD_SPI=spi0
        GPIOHSTXD0=15
        GPIOHSTXD1=17
        GPIOHSTXD2=19
        GPIOHSTXCK=13
        GPIOHSTXINVERTED=1
        PICO_PIO_USE_GPIO_BASE=1
        CFG_TUH_RPI_PIO_USB=1
        PIO_USB_USE_PIO=2
        PICO_DEFAULT_PIO_USB_DP_PIN=1
    )
    target_compile_options(jack_wave1_runtime PRIVATE
        -Wall -Wextra -Wno-unused-parameter)
    if(JACK_WAVE1_HDMI_AUDIO)
        target_compile_definitions(jack_wave1_runtime PUBLIC JACK_WAVE1_HDMI_AUDIO=1)
    endif()
    target_link_libraries(jack_wave1_runtime PUBLIC
        pico_stdlib
        pico_multicore
        hardware_clocks
        hardware_dma
        hardware_pio
        hardware_spi
        hardware_timer
        hardware_vreg
        pico_fatfs
        ${JACK_VIDEO_TARGET}
        tinyusb_host
        tinyusb_pico_pio_usb
    )
endif()
