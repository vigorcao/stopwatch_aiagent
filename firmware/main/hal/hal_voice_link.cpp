/*
 * Voice Code link: one BLE connection to the Mac carrying
 *   - a HID keyboard profile (esp_hidd over NimBLE), which makes macOS bond,
 *     encrypt and auto-reconnect the watch like any Bluetooth keyboard, and
 *   - a custom service: the Mac helper subscribes to push-to-talk events
 *     ("down" / "up" / "cancel") and Claude Code hooks write session state,
 *   - while A is held, the watch mic streamed as 16 kHz IMA-ADPCM (64 kbps),
 *     so the Mac can transcribe you from across the room.
 */
#include "hal.h"
#include "utils/voice_code_protocol.h"
#include <mooncake_log.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_bt.h>
#include <esp_system.h>
#include <sys/time.h>
#include <cstdlib>
#include <esp_hidd.h>
#include <esp_nimble_hci.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>

extern "C" void ble_store_config_init(void);

static const std::string_view _tag = "HAL-VoiceLink";

namespace {

/* --------------------------------- Keyboard -------------------------------- */
// Boot-compatible keyboard: report id 1 = [modifiers, reserved, 6 keys].
const uint8_t _keyboard_report_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,  // Generic Desktop / Keyboard / Report ID 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,  // Modifier usages E0..E7
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,  //   8 x 1 bit input
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03,              // Reserved byte
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,  // LED output
    0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x03,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,  // 6 key array
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
};

esp_hid_raw_report_map_t _report_maps[] = {{.data = _keyboard_report_map, .len = sizeof(_keyboard_report_map)}};

esp_hid_device_config_t _hid_config = {
    .vendor_id         = 0x16C0,
    .product_id        = 0x05DF,
    .version           = 0x0100,
    .device_name       = voice_code::DeviceName,
    .manufacturer_name = "M5Stack",
    .serial_number     = "0001",
    .report_maps       = _report_maps,
    .report_maps_len   = 1,
};

esp_hidd_dev_t* _hid_dev = nullptr;
std::atomic<bool> _connected{false};
std::atomic<bool> _started{false};
std::atomic<uint16_t> _conn_handle{BLE_HS_CONN_HANDLE_NONE};

/* ---------------------------------- Status --------------------------------- */
std::mutex _status_mutex;
std::array<VoiceCodeStatus, voice_code::AgentCount> _statuses;  // [claude, gpt, openclaw]
std::atomic<bool> _boot_reported{false};

const char* reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "PANIC (crash)";
        case ESP_RST_INT_WDT: return "INTERRUPT WATCHDOG";
        case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
        case ESP_RST_WDT: return "WATCHDOG";
        case ESP_RST_BROWNOUT: return "BROWNOUT (low voltage)";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_USB: return "usb";
        default: return "other";
    }
}

const ble_uuid128_t _svc_uuid    = BLE_UUID128_INIT(VOICE_CODE_UUID128(0x01));
const ble_uuid128_t _status_uuid = BLE_UUID128_INIT(VOICE_CODE_UUID128(0x02));
const ble_uuid128_t _talk_uuid   = BLE_UUID128_INIT(VOICE_CODE_UUID128(0x03));
const ble_uuid128_t _audio_uuid  = BLE_UUID128_INIT(VOICE_CODE_UUID128(0x04));
uint16_t _talk_handle            = 0;
uint16_t _audio_handle           = 0;

int talk_access_cb(uint16_t, uint16_t, ble_gatt_access_ctxt*, void*)
{
    return 0;  // notify-only; reads return empty
}

// Writing the time zone (NVS) and RTC (I2C) is too heavy for the NimBLE host
// task's stack, so it happens in a short-lived task of its own.
void time_sync_task(void* arg)
{
    std::unique_ptr<std::string> tz(static_cast<std::string*>(arg));
    auto& hal = GetHAL();
    if (!tz->empty() && *tz != hal.getTimezone()) {
        hal.setTimezone(*tz);  // persists; only written when it changes
    }
    hal.syncSystemTimeToRtc();  // survives power-off
    mclog::tagInfo(_tag, "time synced from Mac ({})", *tz);
    vTaskDelete(nullptr);
}

void sync_time(std::string_view rest)
{
    const auto tab        = rest.find('\t');
    const std::string sec = std::string(rest.substr(0, tab));
    const long long epoch = std::atoll(sec.c_str());
    if (epoch < 1700000000LL) {
        return;  // garbage
    }
    timeval tv{static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
    auto* tz = new std::string(tab == std::string_view::npos ? "" : rest.substr(tab + 1));
    if (xTaskCreate(time_sync_task, "time_sync", 4096, tz, 3, nullptr) != pdPASS) {
        delete tz;
    }
}

int status_access_cb(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    // Static, not on the stack: this runs in the NimBLE host task, whose stack is small.
    // Replies arrive as long writes of up to 512 bytes.
    static char buf[640];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    const std::string_view payload(buf, len);
    if (payload.substr(0, 5) == "time\t") {
        // "time\t<unix seconds>\t<POSIX TZ>": the Mac's clock, sent on every connect.
        sync_time(payload.substr(5));
    } else {
        std::lock_guard<std::mutex> lock(_status_mutex);
        voice_code::apply_payload(_statuses, payload, GetHAL().millis());
    }
    // First write from the Mac helper: tell it why the watch last restarted.
    if (!_boot_reported.exchange(true)) {
        char msg[120];
        const int n = snprintf(msg, sizeof(msg), "boot\t%s\t%lus, battery %u%%%s",
                               reset_reason_name(esp_reset_reason()),
                               static_cast<unsigned long>(GetHAL().millis() / 1000), GetHAL().getBatteryLevel(),
                               GetHAL().isBatteryCharging() ? " charging" : "");
        ble_gatts_notify_custom(_conn_handle, _talk_handle, ble_hs_mbuf_from_flat(msg, n));
    }
    return 0;
}

const ble_gatt_chr_def _status_chrs[] = {
    {
        .uuid      = &_status_uuid.u,
        .access_cb = status_access_cb,
        .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid       = &_talk_uuid.u,
        .access_cb  = talk_access_cb,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &_talk_handle,
    },
    {
        .uuid       = &_audio_uuid.u,
        .access_cb  = talk_access_cb,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &_audio_handle,
    },
    {},
};

const ble_gatt_svc_def _status_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &_svc_uuid.u,
        .characteristics = _status_chrs,
    },
    {},
};

/* ------------------------------ GAP / advertise ----------------------------- */
int gap_event_cb(ble_gap_event* event, void*)
{
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0) {
        _conn_handle = event->connect.conn_handle;
        // Ask the Mac to pair/encrypt right away; macOS only keeps a
        // keyboard connected once the link is bonded.
        int rc = ble_gap_security_initiate(event->connect.conn_handle);
        mclog::tagInfo(_tag, "connected, security initiate rc={}", rc);
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_MTU) {
        mclog::tagInfo(_tag, "mtu {}", event->mtu.value);
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        mclog::tagInfo(_tag, "encryption change status={}", event->enc_change.status);
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING) {
        // Mac forgot us and re-pairs: drop the stale bond and accept.
        ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    return 0;
}

void advertise()
{
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);
    ble_hs_adv_fields fields{};
    fields.flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance            = ESP_HID_APPEARANCE_KEYBOARD;
    fields.appearance_is_present = 1;
    fields.name                  = (uint8_t*)voice_code::DeviceName;
    fields.name_len              = strlen(voice_code::DeviceName);
    fields.name_is_complete      = 1;
    fields.uuids16               = &hid_uuid;
    fields.num_uuids16           = 1;
    fields.uuids16_is_complete   = 1;
    ble_gap_adv_set_fields(&fields);

    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min  = BLE_GAP_ADV_ITVL_MS(30);
    params.itvl_max  = BLE_GAP_ADV_ITVL_MS(50);
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &params, gap_event_cb, nullptr);
    mclog::tagInfo(_tag, "advertising as '{}', rc={}", voice_code::DeviceName, rc);
}

void hidd_event_cb(void*, esp_event_base_t, int32_t id, void*)
{
    switch (static_cast<esp_hidd_event_t>(id)) {
        case ESP_HIDD_START_EVENT:
            advertise();
            break;
        case ESP_HIDD_CONNECT_EVENT:
            mclog::tagInfo(_tag, "mac connected");
            _connected = true;
            break;
        case ESP_HIDD_DISCONNECT_EVENT:
            mclog::tagInfo(_tag, "mac disconnected");
            _connected   = false;
            _conn_handle = BLE_HS_CONN_HANDLE_NONE;
            advertise();
            break;
        default:
            break;
    }
}

/* ------------------------------- Mic streaming ------------------------------ */
constexpr int _out_rate       = 16000;
constexpr uint16_t _chunk_ms  = 40;     // one blocking mic read
constexpr float _mic_gain_db  = 30.0f;  // close speech peaks ~16k; the Mac boosts quiet, distant speech
constexpr size_t _header_size = 5;
std::atomic<bool> _streaming{false};
SemaphoreHandle_t _mic_done = nullptr;

// 44.1 kHz -> 16 kHz: windowed-sinc low-pass (7 kHz), then linear interpolation.
class Downsampler {
public:
    explicit Downsampler(int inRate) : _step(static_cast<float>(inRate) / _out_rate)
    {
        constexpr float cutoff = 7000.0f;
        const float fc         = cutoff / inRate;
        float sum              = 0.0f;
        for (int i = 0; i < _taps; ++i) {
            const float n      = i - (_taps - 1) / 2.0f;
            const float sinc   = n == 0.0f ? 2.0f * fc : std::sin(2.0f * M_PI * fc * n) / (M_PI * n);
            const float window = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (_taps - 1));
            _h[i]              = sinc * window;
            sum += _h[i];
        }
        for (float& h : _h) {
            h /= sum;
        }
    }

    void push(const std::vector<int16_t>& in, std::vector<int16_t>& out)
    {
        for (int16_t x : in) {
            _hist[_head] = x;
            _head        = (_head + 1) % _taps;
            float y      = 0.0f;
            for (int i = 0; i < _taps; ++i) {
                y += _h[i] * _hist[(_head + i) % _taps];
            }
            // Emit every output instant that falls between the previous and this sample.
            while (_pos <= 1.0f) {
                const float v = _prev + (y - _prev) * _pos;
                out.push_back(static_cast<int16_t>(std::clamp(v, -32768.0f, 32767.0f)));
                _pos += _step;
            }
            _pos -= 1.0f;
            _prev = y;
        }
    }

private:
    static constexpr int _taps = 31;
    std::array<float, _taps> _h{};
    std::array<float, _taps> _hist{};
    int _head   = 0;
    float _prev = 0.0f;
    float _pos  = 0.0f;
    float _step;
};

// Standard IMA-ADPCM.
class AdpcmEncoder {
public:
    int16_t predictor = 0;
    uint8_t index     = 0;

    uint8_t encode(int16_t sample)
    {
        static constexpr int16_t steps[89] = {
            7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
            31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
            130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
            544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
            2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
            9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
        static constexpr int8_t index_adjust[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

        const int step = steps[index];
        int diff       = sample - predictor;
        uint8_t code   = 0;
        if (diff < 0) {
            code = 8;
            diff = -diff;
        }
        int delta = step >> 3;
        if (diff >= step) {
            code |= 4;
            diff -= step;
            delta += step;
        }
        if (diff >= step >> 1) {
            code |= 2;
            diff -= step >> 1;
            delta += step >> 1;
        }
        if (diff >= step >> 2) {
            code |= 1;
            delta += step >> 2;
        }
        predictor = static_cast<int16_t>(std::clamp(predictor + ((code & 8) ? -delta : delta), -32768, 32767));
        index     = static_cast<uint8_t>(std::clamp(index + index_adjust[code & 7], 0, 88));
        return code;
    }
};

// Notifications share the ACL buffers with everything else; wait briefly when full.
bool notify_audio(const uint8_t* data, size_t len)
{
    for (int attempt = 0; attempt < 25; ++attempt) {
        const uint16_t conn = _conn_handle;
        if (conn == BLE_HS_CONN_HANDLE_NONE) {
            return false;
        }
        os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
        if (om != nullptr) {
            const int rc = ble_gatts_notify_custom(conn, _audio_handle, om);  // consumes om
            if (rc == 0) {
                return true;
            }
            if (rc != BLE_HS_ENOMEM) {
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
}

void mic_task(void*)
{
    auto& hal = GetHAL();
    Downsampler downsampler(hal.getAudioSampleRate());
    AdpcmEncoder encoder;
    std::vector<int16_t> raw;
    std::vector<int16_t> pcm;
    std::vector<uint8_t> packet;

    // Fill each notification to the negotiated MTU (capped at the ACL size).
    const uint16_t conn    = _conn_handle;
    const size_t mtu       = conn == BLE_HS_CONN_HANDLE_NONE ? 23 : ble_att_mtu(conn);
    const size_t payload   = std::min<size_t>(mtu - 3, 244);
    const size_t per_packet = (payload - _header_size) * 2;

    uint16_t seq    = 0;
    uint32_t sent   = 0;
    uint32_t failed = 0;
    int peak        = 0;

    auto send = [&](size_t count) {
        packet.assign({static_cast<uint8_t>(seq), static_cast<uint8_t>(seq >> 8),
                       static_cast<uint8_t>(encoder.predictor), static_cast<uint8_t>(encoder.predictor >> 8),
                       encoder.index});
        for (size_t i = 0; i < count; i += 2) {
            const uint8_t lo = encoder.encode(pcm[i]);
            const uint8_t hi = i + 1 < count ? encoder.encode(pcm[i + 1]) : 0;
            packet.push_back(lo | (hi << 4));
        }
        for (size_t i = 0; i < count; ++i) {
            peak = std::max(peak, std::abs(static_cast<int>(pcm[i])));
        }
        pcm.erase(pcm.begin(), pcm.begin() + count);
        (notify_audio(packet.data(), packet.size()) ? sent : failed)++;
        seq++;
    };

    while (_streaming) {
        hal.audioRecord(raw, _chunk_ms, _mic_gain_db);
        downsampler.push(raw, pcm);
        while (pcm.size() >= per_packet) {
            send(per_packet);
        }
    }
    if (!pcm.empty()) {
        send(pcm.size());
    }
    mclog::tagInfo(_tag, "mic stream: mtu {} sent {} failed {} peak {}", mtu, sent, failed, peak);
    xSemaphoreGive(_mic_done);
    vTaskDelete(nullptr);
}

void mic_stream_start()
{
    if (_audio_handle == 0 || _streaming.exchange(true)) {
        return;
    }
    if (_mic_done == nullptr) {
        _mic_done = xSemaphoreCreateBinary();
    }
    xSemaphoreTake(_mic_done, 0);
    if (xTaskCreate(mic_task, "voice_mic", 6 * 1024, nullptr, 6, nullptr) != pdPASS) {
        _streaming = false;
        mclog::tagError(_tag, "mic task create failed");
    }
}

// Blocks until the last packet is queued, so "up" is never sent ahead of audio.
void mic_stream_stop()
{
    if (!_streaming.exchange(false)) {
        return;
    }
    if (xSemaphoreTake(_mic_done, pdMS_TO_TICKS(500)) != pdTRUE) {
        mclog::tagWarn(_tag, "mic task did not finish in time");
    }
}

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

void Hal::voiceLinkStart()
{
    if (_started.exchange(true)) {
        return;
    }
    mclog::tagInfo(_tag, "start");

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_nimble_init());

    // Just Works bonding: a keyboard with no passkey entry.
    ble_hs_cfg.sm_io_cap         = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding        = 1;
    ble_hs_cfg.sm_mitm           = 0;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;

    // HID registers its GATT services here; ours goes in before the host starts.
    ESP_ERROR_CHECK(esp_hidd_dev_init(&_hid_config, ESP_HID_TRANSPORT_BLE, hidd_event_cb, &_hid_dev));
    ESP_ERROR_CHECK(ble_gatts_count_cfg(_status_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(_status_svcs));
    ble_svc_gap_device_name_set(voice_code::DeviceName);
    ble_store_config_init();

    ESP_ERROR_CHECK(esp_nimble_enable(reinterpret_cast<void*>(host_task)));
}

bool Hal::voiceLinkConnected()
{
    return _connected;
}

void Hal::voiceLinkTalk(bool pressed, bool submit, VoiceAgent agent)
{
    if (!pressed) {
        mic_stream_stop();
    }
    const uint16_t conn = _conn_handle;
    if (!_connected || conn == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    std::string payload = std::string(pressed ? "down" : (submit ? "up" : "cancel")) + "\t" +
                          voice_code::agent_name(agent);
    if (pressed && _audio_handle != 0) {
        payload += "\twatch";
    }
    os_mbuf* om = ble_hs_mbuf_from_flat(payload.data(), payload.size());
    int rc      = ble_gatts_notify_custom(conn, _talk_handle, om);
    mclog::tagInfo(_tag, "talk {} rc={}", payload, rc);
    if (pressed && rc == 0) {
        mic_stream_start();  // after "down", so the Mac is ready for the first packet
    }
}

VoiceCodeStatus Hal::voiceLinkStatus(VoiceAgent agent)
{
    std::lock_guard<std::mutex> lock(_status_mutex);
    return _statuses[voice_code::agent_index(agent)];
}
