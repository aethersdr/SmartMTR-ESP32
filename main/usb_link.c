#include "usb_link.h"

#include "ctr2_link.h"
#include "model.h"

#include <stdio.h>
#include <string.h>

#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

static const char *TAG = "usb_link";

// Identity AetherSDR's CTR2 relay recognizes (Ctr2HidPort::knownCtr2Devices).
#define USB_VID      0x303A
#define USB_PID      0x1001
#define USB_PRODUCT  "AETHER_KNOB"

#define RX_QUEUE_REPORTS   1024   // ~1 s of the link's full inbound rate
#define TX_QUEUE_REPORTS   256
#define ACTION_QUEUE_DEPTH 32

typedef struct {
    uint8_t b[CTR2_REPORT_BYTES];
} report_t;

static QueueHandle_t s_rx;        // TinyUSB task -> radio task
static QueueHandle_t s_tx;        // radio task -> USB IN endpoint
static QueueHandle_t s_actions;   // UI -> radio task
static TaskHandle_t s_radio_task;
static volatile bool s_rx_overflow;
static volatile bool s_mounted;

static ctr2_tx s_link_tx;
static ctr2_rx s_link_rx;
static bool s_waiting_for_host = true;  // power-up state: as if READY was just sent
static bool s_radio_open;

// ---------------------------------------------------------------- descriptors

// Report 0x01: the link, 8 bytes each way. Report 0x02: Feature report for
// link extension negotiation (AetherSDR docs/ctr2-usb-relay-design.md,
// "Extensions"). Both in one vendor collection (usage page 0xFF00, usage 1),
// which is what AetherSDR lists.
static const uint8_t s_report_desc[] = {
    HID_USAGE_PAGE_N(HID_USAGE_PAGE_VENDOR, 2),
    HID_USAGE(0x01),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_REPORT_ID(CTR2_REPORT_ID)
        HID_USAGE(0x02),
        HID_LOGICAL_MIN(0x00),
        HID_LOGICAL_MAX_N(0xff, 2),
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(CTR2_REPORT_BYTES),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        HID_USAGE(0x03),
        HID_LOGICAL_MIN(0x00),
        HID_LOGICAL_MAX_N(0xff, 2),
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(CTR2_REPORT_BYTES),
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        HID_REPORT_ID(CTR2_FEATURE_REPORT_ID)
        HID_USAGE(0x04),
        HID_LOGICAL_MIN(0x00),
        HID_LOGICAL_MAX_N(0xff, 2),
        HID_REPORT_SIZE(8),
        HID_REPORT_COUNT(CTR2_FEATURE_BYTES),
        HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

// Link extensions this firmware offers, and the ones AetherSDR accepted.
// The accepted set is written from the TinyUSB task and applied by the radio
// task; it lasts until the device leaves the bus.
#define OFFERED_EXTENSIONS (CTR2_CAP(CTR2_EXT_AUDIO_SPECTRUM) | CTR2_CAP(CTR2_EXT_RELAY_UDP_PORT))
static volatile uint32_t s_accepted_extensions;

static const tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0, 100),
    // 16-byte endpoints: report ID + 8 bytes; 1 ms polling = ~7 KB/s of payload.
    TUD_HID_INOUT_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(s_report_desc), 0x01, 0x81, 16, 1),
};

static char s_serial[13];         // the chip's MAC, so two knobs are told apart

static const char *s_strings[] = {
    (const char[]){0x09, 0x04},  // English (US)
    "AetherSDR",
    USB_PRODUCT,
    s_serial,
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    if (type == HID_REPORT_TYPE_FEATURE && report_id == CTR2_FEATURE_REPORT_ID
        && reqlen >= CTR2_FEATURE_BYTES) {
        ctr2_caps_encode(OFFERED_EXTENSIONS, buffer);   // TinyUSB adds the report ID
        return CTR2_FEATURE_BYTES;
    }
    return 0;
}

// Output reports. On the interrupt OUT endpoint TinyUSB passes report_id 0 and
// leaves the ID byte in the buffer; a control-pipe SET_REPORT may strip it.
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                           uint8_t const *buffer, uint16_t len)
{
    (void)instance;
    if (type == HID_REPORT_TYPE_FEATURE) {
        uint32_t caps;
        if (report_id == CTR2_FEATURE_REPORT_ID && ctr2_caps_decode(buffer, len, &caps)) {
            s_accepted_extensions = caps & OFFERED_EXTENSIONS;
            xTaskNotifyGive(s_radio_task);
        }
        return;
    }
    if (type != HID_REPORT_TYPE_OUTPUT && type != HID_REPORT_TYPE_INVALID)
        return;
    if (report_id == 0 && len == CTR2_REPORT_BYTES + 1 && buffer[0] == CTR2_REPORT_ID) {
        buffer++;
        len--;
    } else if (report_id != 0 && report_id != CTR2_REPORT_ID) {
        return;
    }
    if (len != CTR2_REPORT_BYTES)
        return;
    report_t r;
    memcpy(r.b, buffer, CTR2_REPORT_BYTES);
    if (xQueueSend(s_rx, &r, 0) != pdTRUE)
        s_rx_overflow = true;  // a lost report breaks the stream: restart the link
    xTaskNotifyGive(s_radio_task);
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance; (void)report; (void)len;
    xTaskNotifyGive(s_radio_task);
}

static void usb_event(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    s_mounted = event->id == TINYUSB_EVENT_ATTACHED;
    s_accepted_extensions = 0;   // a new host session negotiates afresh
    if (s_radio_task)
        xTaskNotifyGive(s_radio_task);
}

// ---------------------------------------------------------------- sending

static void pump_tx(void)
{
    report_t r;
    while (tud_hid_ready() && xQueuePeek(s_tx, &r, 0) == pdTRUE) {
        if (!tud_hid_report(CTR2_REPORT_ID, r.b, CTR2_REPORT_BYTES))
            break;
        xQueueReceive(s_tx, &r, 0);
    }
}

static void report_sink(void *ctx, const uint8_t report[CTR2_REPORT_BYTES])
{
    (void)ctx;
    report_t r;
    memcpy(r.b, report, CTR2_REPORT_BYTES);
    // Only the radio task sends, so it drains the queue itself when full.
    while (xQueueSend(s_tx, &r, 0) != pdTRUE) {
        if (!s_mounted) {
            xQueueReset(s_tx);
            return;
        }
        pump_tx();
        vTaskDelay(1);
    }
}

static void send_tcp(const char *bytes, size_t len)
{
    while (len > 0) {
        uint16_t chunk = len > CTR2_MAX_PAYLOAD ? CTR2_MAX_PAYLOAD : (uint16_t)len;
        ctr2_tx_send(&s_link_tx, CTR2_TYPE_DATA, (const uint8_t *)bytes, chunk, report_sink, NULL);
        bytes += chunk;
        len -= chunk;
    }
}

static void send_udp(uint16_t port, const uint8_t *bytes, size_t len)
{
    if (len > 0 && len <= CTR2_MAX_DATAGRAM)
        ctr2_tx_send_datagram(&s_link_tx, port, bytes, (uint16_t)len, report_sink, NULL);
}

// ---------------------------------------------------------------- link state

static void set_link(link_state_t state)
{
    model_lock();
    knob_model_t *m = model_get();
    if (m->link != state) {
        m->link = state;
        m->revision++;
    }
    model_unlock();
}

static void radio_closed(void)
{
    if (s_radio_open) {
        s_radio_open = false;
        flex_api_disconnected();
    }
}

static void begin_waiting(void)
{
    ctr2_rx_reset(&s_link_rx);
    ctr2_tx_reset(&s_link_tx);
    xQueueReset(s_tx);
    s_waiting_for_host = true;
}

// HELLO from the host: answer READY and wait for its READY (radio open) or
// CLOSED (radio unreachable).
static void answer_hello(void)
{
    radio_closed();
    begin_waiting();
    ctr2_tx_send(&s_link_tx, CTR2_TYPE_READY, NULL, 0, report_sink, NULL);
    set_link(LINK_WAITING);
}

// Framing error or lost report: drop the radio connection and ask the host
// for a fresh one.
static void restart_link(const char *why)
{
    ESP_LOGW(TAG, "restarting link: %s", why);
    radio_closed();
    begin_waiting();
    ctr2_tx_send(&s_link_tx, CTR2_TYPE_HELLO, NULL, 0, report_sink, NULL);
    set_link(LINK_WAITING);
}

static uint32_t now_ms(void);

// AUDIO_SPECTRUM: [bars N][low Hz hi][low Hz lo][span Hz hi][span Hz lo][N levels].
static void store_spectrum(const uint8_t *data, uint16_t len)
{
    if (len <= CTR2_SPECTRUM_HEADER || data[0] == 0 || data[0] > CTR2_SPECTRUM_MAX_BARS
        || len != CTR2_SPECTRUM_HEADER + data[0])
        return;
    model_lock();
    knob_model_t *m = model_get();
    m->spectrum_bars = data[0];
    m->spectrum_low_hz = (data[1] << 8) | data[2];
    m->spectrum_span_hz = (data[3] << 8) | data[4];
    memcpy(m->spectrum, data + CTR2_SPECTRUM_HEADER, data[0]);
    m->spectrum_ms = now_ms();
    m->revision++;
    model_unlock();
}

static void on_report(const uint8_t r[CTR2_REPORT_BYTES])
{
    if (r[0] == CTR2_MARKER && r[3] == CTR2_TYPE_HELLO) {
        answer_hello();
        return;
    }
    if (s_waiting_for_host) {
        // Skip leftovers; data reports never start with 0xFF.
        if (r[0] != CTR2_MARKER || (r[3] != CTR2_TYPE_READY && r[3] != CTR2_TYPE_CLOSED))
            return;
        s_waiting_for_host = false;
        ctr2_rx_reset(&s_link_rx);
    }
    uint8_t type;
    const uint8_t *data;
    uint16_t len;
    switch (ctr2_rx_feed(&s_link_rx, r, &type, &data, &len)) {
    case CTR2_RX_MESSAGE:
        if (type == CTR2_TYPE_READY) {
            ESP_LOGI(TAG, "radio connection open");
            s_radio_open = true;
            flex_api_connected();
        } else if (type == CTR2_TYPE_CLOSED) {
            ESP_LOGI(TAG, "radio connection closed");
            radio_closed();
            begin_waiting();
            set_link(LINK_WAITING);
        } else if (type == CTR2_TYPE_DATA && s_radio_open) {
            flex_api_rx_bytes(data, len);
        } else if (type == CTR2_TYPE_DATAGRAM && s_radio_open && len > 2) {
            flex_api_rx_datagram((uint16_t)((data[0] << 8) | data[1]), data + 2, (size_t)(len - 2));
        } else if (type == CTR2_EXT_AUDIO_SPECTRUM) {
            store_spectrum(data, len);
        } else if (type == CTR2_EXT_RELAY_UDP_PORT && s_radio_open && len == 2) {
            flex_api_set_relay_udp_port((uint16_t)((data[0] << 8) | data[1]));
        }
        break;
    case CTR2_RX_ERROR:
        restart_link("framing error");
        break;
    case CTR2_RX_PENDING:
        break;
    }
}

// ---------------------------------------------------------------- task

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void radio_task(void *arg)
{
    (void)arg;
    bool was_mounted = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        ctr2_rx_set_extensions(&s_link_rx, s_accepted_extensions);

        if (s_mounted != was_mounted) {
            was_mounted = s_mounted;
            ESP_LOGI(TAG, "USB %s", was_mounted ? "attached" : "detached");
            if (!was_mounted) {
                radio_closed();
                begin_waiting();
                xQueueReset(s_rx);
                set_link(LINK_NO_HOST);
            }
        }

        if (s_rx_overflow) {
            s_rx_overflow = false;
            xQueueReset(s_rx);
            restart_link("receive queue overflow");
        }
        report_t r;
        while (xQueueReceive(s_rx, &r, 0) == pdTRUE)
            on_report(r.b);

        knob_action_t a;
        while (xQueueReceive(s_actions, &a, 0) == pdTRUE)
            flex_api_action(&a);

        flex_api_tick(now_ms());
        pump_tx();
    }
}

void usb_link_post_action(const knob_action_t *action)
{
    if (s_actions)
        xQueueSend(s_actions, action, 0);
}

void usb_link_start(void)
{
    s_rx = xQueueCreate(RX_QUEUE_REPORTS, sizeof(report_t));
    s_tx = xQueueCreate(TX_QUEUE_REPORTS, sizeof(report_t));
    s_actions = xQueueCreate(ACTION_QUEUE_DEPTH, sizeof(knob_action_t));
    flex_api_init(send_tcp, send_udp);
    ctr2_rx_init(&s_link_rx);   // plain version 0 until AetherSDR accepts extensions
    begin_waiting();
    xTaskCreatePinnedToCore(radio_task, "radio", 6144, NULL, 5, &s_radio_task, 1);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_serial, sizeof s_serial, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(usb_event);
    cfg.descriptor.device = &s_device_desc;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = sizeof s_strings / sizeof s_strings[0];
    cfg.descriptor.full_speed_config = s_config_desc;
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));
    ESP_LOGI(TAG, "USB HID up as %04X:%04X %s", USB_VID, USB_PID, USB_PRODUCT);
}
