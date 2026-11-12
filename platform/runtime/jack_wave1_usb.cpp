#include "jack_wave1_runtime.h"

#include <stdio.h>
#include <string.h>

#include "hardware/dma.h"
#include "pio_usb.h"
#include "hardware/gpio.h"
#include "tusb.h"

namespace {
bool g_usb_initialized = false;
uint16_t g_keyboard_buttons[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID]{};
bool g_keyboard_mounted[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID]{};
uint16_t g_keyboard_latched_buttons = 0;
uint32_t g_keyboard_report_count = 0;
uint32_t g_usb_service_count = 0;
uint32_t g_receive_failure_count = 0;
uint32_t g_last_report_us = 0;
uint8_t g_keyboard_count = 0;
jack_wave1_key_event_t g_key_queue[32]{};
uint8_t g_key_head = 0;
uint8_t g_key_tail = 0;
uint32_t g_key_queue_drop_count = 0;
hid_keyboard_report_t g_previous_reports[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID]{};

void queue_key(uint8_t keycode, uint8_t modifier)
{
    const uint8_t next = static_cast<uint8_t>((g_key_head + 1) % 32);
    if (next == g_key_tail) {
        ++g_key_queue_drop_count;
        return;
    }
    g_key_queue[g_key_head] = {keycode, modifier};
    g_key_head = next;
}

uint16_t translate_keyboard_report(const hid_keyboard_report_t &report)
{
    uint16_t buttons = 0;
    for (const uint8_t key : report.keycode) {
        switch (key) {
        case HID_KEY_ARROW_UP:    buttons |= JACK_BUTTON_UP; break;
        case HID_KEY_ARROW_DOWN:  buttons |= JACK_BUTTON_DOWN; break;
        case HID_KEY_ARROW_LEFT:  buttons |= JACK_BUTTON_LEFT; break;
        case HID_KEY_ARROW_RIGHT: buttons |= JACK_BUTTON_RIGHT; break;
        case HID_KEY_A:           buttons |= JACK_BUTTON_SELECT | JACK_KEYBOARD_A; break;
        case HID_KEY_B:           buttons |= JACK_KEYBOARD_B; break;
        case HID_KEY_S:
        case HID_KEY_ENTER:       buttons |= JACK_BUTTON_START; break;
        case HID_KEY_Z:           buttons |= JACK_BUTTON_B; break;
        case HID_KEY_X:           buttons |= JACK_BUTTON_A | JACK_KEYBOARD_X; break;
        case HID_KEY_SPACE:       buttons |= JACK_BUTTON_A; break;
#ifdef JACK_WAVE1_HDMI_AUDIO
        case HID_KEY_M:           buttons |= JACK_KEYBOARD_MUTE; break;
#endif
        case HID_KEY_C:           buttons |= JACK_BUTTON_X; break;
        case HID_KEY_V:           buttons |= JACK_BUTTON_Y; break;
        case HID_KEY_Q:           buttons |= JACK_BUTTON_L; break;
        case HID_KEY_W:           buttons |= JACK_BUTTON_R; break;
        default: break;
        }
    }
    return buttons;
}

void clear_device(uint8_t dev_addr)
{
    if (dev_addr <= CFG_TUH_DEVICE_MAX) {
        memset(g_keyboard_buttons[dev_addr], 0,
               sizeof(g_keyboard_buttons[dev_addr]));
        for (unsigned instance = 0; instance < CFG_TUH_HID; ++instance) {
            g_previous_reports[dev_addr][instance] = {};
            if (g_keyboard_mounted[dev_addr][instance]) {
                g_keyboard_mounted[dev_addr][instance] = false;
                if (g_keyboard_count != 0) --g_keyboard_count;
            }
        }
    }
    g_keyboard_latched_buttons = 0;
}

int find_usb_dma_channel()
{
    /* HSTX uses DMA 0 and 1 directly, without claiming them through the SDK. */
    for (int channel = 2; channel < NUM_DMA_CHANNELS; ++channel) {
        if (!dma_channel_is_claimed(channel)) return channel;
    }
    return -1;
}
}  // namespace

extern "C" void tuh_mount_cb(uint8_t dev_addr)
{
    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    printf("WAVE1_USB mount addr=%u vid=%04x pid=%04x\n", dev_addr, vid, pid);
}

extern "C" void tuh_umount_cb(uint8_t dev_addr)
{
    clear_device(dev_addr);
    printf("WAVE1_USB unmount addr=%u\n", dev_addr);
}

extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                                  const uint8_t *, uint16_t)
{
    const uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);
    printf("WAVE1_USB hid_mount addr=%u instance=%u protocol=%u\n",
           dev_addr, instance, protocol);
    if (protocol == HID_ITF_PROTOCOL_KEYBOARD &&
        dev_addr <= CFG_TUH_DEVICE_MAX && instance < CFG_TUH_HID &&
        !g_keyboard_mounted[dev_addr][instance]) {
        g_keyboard_mounted[dev_addr][instance] = true;
        ++g_keyboard_count;
    }
    if (!tuh_hid_receive_report(dev_addr, instance)) {
        ++g_receive_failure_count;
        printf("WAVE1_USB receive_start_failed addr=%u instance=%u\n",
               dev_addr, instance);
    }
}

extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    if (dev_addr <= CFG_TUH_DEVICE_MAX && instance < CFG_TUH_HID) {
        g_keyboard_buttons[dev_addr][instance] = 0;
        g_previous_reports[dev_addr][instance] = {};
        if (g_keyboard_mounted[dev_addr][instance]) {
            g_keyboard_mounted[dev_addr][instance] = false;
            if (g_keyboard_count != 0) --g_keyboard_count;
        }
    }
    g_keyboard_latched_buttons = 0;
    printf("WAVE1_USB hid_unmount addr=%u instance=%u\n", dev_addr, instance);
}

extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                             const uint8_t *report,
                                             uint16_t length)
{
    if (dev_addr <= CFG_TUH_DEVICE_MAX && instance < CFG_TUH_HID &&
        tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD &&
        length >= sizeof(hid_keyboard_report_t)) {
        ++g_keyboard_report_count;
        g_last_report_us = time_us_32();
        const auto &incoming = *reinterpret_cast<const hid_keyboard_report_t *>(report);
        const auto &previous_report = g_previous_reports[dev_addr][instance];
        for (const uint8_t key : incoming.keycode) {
            if (key == 0) continue;
            bool held = false;
            for (const uint8_t old_key : previous_report.keycode)
                held |= key == old_key;
            if (!held) queue_key(key, incoming.modifier);
        }
        g_previous_reports[dev_addr][instance] = incoming;
        const uint16_t previous = g_keyboard_buttons[dev_addr][instance];
        const uint16_t current = translate_keyboard_report(
            incoming);
        const uint16_t pressed = static_cast<uint16_t>(current & ~previous);
        g_keyboard_buttons[dev_addr][instance] = current;
        g_keyboard_latched_buttons |= pressed;
#ifndef JACK_WAVE1_QUIET_INPUT_LOG
        if (pressed != 0)
            printf("WAVE1_USB key_press buttons=%04x\n", pressed);
#endif
    }
    if (!tuh_hid_receive_report(dev_addr, instance)) {
        ++g_receive_failure_count;
        printf("WAVE1_USB receive_restart_failed addr=%u instance=%u\n",
               dev_addr, instance);
    }
}

extern "C" bool jack_wave1_usb_init(void)
{
#ifdef ADAFRUIT_FRUIT_JAM_USB_HOST_5V_POWER_PIN
    gpio_init(ADAFRUIT_FRUIT_JAM_USB_HOST_5V_POWER_PIN);
    gpio_set_dir(ADAFRUIT_FRUIT_JAM_USB_HOST_5V_POWER_PIN, GPIO_OUT);
    gpio_put(ADAFRUIT_FRUIT_JAM_USB_HOST_5V_POWER_PIN, 1);
#endif
    pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
    const int dma_channel = find_usb_dma_channel();
    if (dma_channel < 0) {
        printf("WAVE1_USB no_dma_channel\n");
        return false;
    }

    config.tx_ch = static_cast<uint8_t>(dma_channel);
    config.pio_rx_num = 2;
    config.pio_tx_num = 2;
    config.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
    if (!tuh_configure(BOARD_TUH_RHPORT,
                       TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &config)) {
        printf("WAVE1_USB configure_failed\n");
        return false;
    }

    tusb_rhport_init_t host_init{};
    host_init.role = TUSB_ROLE_HOST;
    host_init.speed = TUSB_SPEED_AUTO;
    if (!tusb_init(BOARD_TUH_RHPORT, &host_init)) {
        printf("WAVE1_USB init_failed\n");
        return false;
    }

    printf("WAVE1_USB ready dma=%d\n", dma_channel);
    g_usb_initialized = true;
    return true;
}

extern "C" uint32_t jack_wave1_usb_report_count(void)
{
    return g_keyboard_report_count;
}

extern "C" void jack_wave1_usb_service(void)
{
    if (!g_usb_initialized) return;
    ++g_usb_service_count;
#if defined(JACK_WAVE1_USB_NONBLOCKING) && JACK_WAVE1_USB_NONBLOCKING
    // A foreground emulator loop must never wait for the next USB event.
    // Process everything already queued, then return immediately.
    tuh_task_ext(0, false);
#else
    tuh_task();
#endif
}

extern "C" void jack_wave1_usb_get_status(jack_wave1_usb_status_t *status)
{
    if (status == nullptr) return;
    uint16_t current = 0;
    for (unsigned dev = 1; dev <= CFG_TUH_DEVICE_MAX; ++dev) {
        for (unsigned instance = 0; instance < CFG_TUH_HID; ++instance)
            current |= g_keyboard_buttons[dev][instance];
    }
    *status = {g_keyboard_report_count, g_usb_service_count,
               g_receive_failure_count, g_last_report_us, current,
               g_keyboard_latched_buttons, g_keyboard_count,
               g_usb_initialized};
}

extern "C" uint16_t jack_wave1_usb_poll_buttons(void)
{
    if (!g_usb_initialized) return 0;
    jack_wave1_usb_service();
    uint16_t buttons = g_keyboard_latched_buttons;
    g_keyboard_latched_buttons = 0;
    for (unsigned dev = 1; dev <= CFG_TUH_DEVICE_MAX; ++dev) {
        for (unsigned instance = 0; instance < CFG_TUH_HID; ++instance)
            buttons |= g_keyboard_buttons[dev][instance];
    }
    return buttons;
}

extern "C" bool jack_wave1_usb_pop_key(jack_wave1_key_event_t *event)
{
    if (event == nullptr || g_key_head == g_key_tail) return false;
    *event = g_key_queue[g_key_tail];
    g_key_tail = static_cast<uint8_t>((g_key_tail + 1) % 32);
    return true;
}

extern "C" uint8_t jack_wave1_usb_key_queue_depth(void)
{
    return static_cast<uint8_t>((g_key_head + 32 - g_key_tail) % 32);
}

extern "C" uint32_t jack_wave1_usb_key_queue_drop_count(void)
{
    return g_key_queue_drop_count;
}

extern "C" uint8_t jack_wave1_usb_current_modifiers(void)
{
    uint8_t modifiers = 0;
    for (unsigned dev = 1; dev <= CFG_TUH_DEVICE_MAX; ++dev)
        for (unsigned instance = 0; instance < CFG_TUH_HID; ++instance)
            modifiers |= g_previous_reports[dev][instance].modifier;
    return modifiers;
}

extern "C" bool native_usb_key_held(uint8_t key)
{
    if(key<4)return false;
    for(unsigned dev=1;dev<=CFG_TUH_DEVICE_MAX;++dev)
        for(unsigned instance=0;instance<CFG_TUH_HID;++instance)
            if(g_keyboard_mounted[dev][instance])
                for(uint8_t held:g_previous_reports[dev][instance].keycode)
                    if(held==key)return true;
    return false;
}
