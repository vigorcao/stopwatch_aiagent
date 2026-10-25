/*
 * Web simulator HAL: implements hal.h on top of Emscripten + SDL2 + browser APIs
 * so the unmodified apps under main/apps run in a browser before flashing.
 *
 * Browser-side state lives in `Module.sim` (see shell.html); C reads it through
 * the EM_JS accessors below.
 */
#include "hal/hal.h"
#include "hal/utils/settings/settings.h"
#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <mooncake_log.h>
#include <emscripten.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

static const std::string_view _tag = "HAL-Web";

static constexpr int _screen_width  = 468;
static constexpr int _screen_height = 466;
static constexpr int _sample_rate   = 44100;

// clang-format off
EM_JS(int, web_btn_state, (int id), { return Module.sim.buttons[id] ? 1 : 0; });
EM_JS(int, web_touch_down, (), { return Module.sim.touch.down ? 1 : 0; });
EM_JS(int, web_touch_x, (), { return Module.sim.touch.x | 0; });
EM_JS(int, web_touch_y, (), { return Module.sim.touch.y | 0; });
EM_JS(double, web_imu, (int axis), { return Module.sim.imu[axis]; });
EM_JS(int, web_battery_level, (), { return Module.sim.battery | 0; });
EM_JS(int, web_battery_charging, (), { return Module.sim.charging ? 1 : 0; });
EM_JS(void, web_set_brightness, (int percent), { Module.sim.onBrightness(percent); });
EM_JS(void, web_vibrate, (int ms, int strength), { Module.sim.onVibrate(ms, strength); });
EM_JS(void, web_audio_play, (const int16_t* pcm, int count, int sampleRate, int volume), {
    Module.sim.playPcm(HEAP16.subarray(pcm >> 1, (pcm >> 1) + count), sampleRate, volume);
});
EM_JS(int, web_mic_read, (int16_t* dst, int count), {
    const chunk = Module.sim.readMic(count);
    if (!chunk) return 0;
    HEAP16.set(chunk, dst >> 1);
    return 1;
});
EM_JS(void, web_status, (const char* text), { Module.sim.onStatus(UTF8ToString(text)); });
EM_JS(void, web_reload, (), { location.reload(); });
// clang-format on

/* -------------------------------------------------------------------------- */
/*                                   System                                   */
/* -------------------------------------------------------------------------- */
static std::unique_ptr<Hal> _hal_instance;

Hal& GetHAL()
{
    if (!_hal_instance) {
        _hal_instance = std::make_unique<Hal>();
    }
    return *_hal_instance.get();
}

namespace {

// Alarm beep pattern mirrors hal_alarm.cpp: 4 beeps 180ms apart, cycle every 720ms.
bool _alarm_active          = false;
uint32_t _alarm_next_beep   = 0;
int _alarm_beep_index       = 0;

void web_service_alarm()
{
    if (!_alarm_active) {
        return;
    }
    const uint32_t now = GetHAL().millis();
    if (now < _alarm_next_beep) {
        return;
    }
    audio::play_tone(1760, 0.07f);
    GetHAL().vibrate(70, 90);
    _alarm_beep_index = (_alarm_beep_index + 1) % 4;
    _alarm_next_beep  = now + (_alarm_beep_index == 0 ? 720 : 180);
}

}  // namespace

// Called while the app code blocks (delay) and from the main loop, so LVGL keeps
// rendering the way the dedicated lvgl task does on the device.
void web_pump()
{
    lv_timer_handler();
    web_service_alarm();
}

void Hal::init()
{
    mclog::tagInfo(_tag, "init (browser simulator)");
    display_init();
    lvgl_init();
    audio_init();
    button_init();
}

void Hal::delay(std::uint32_t ms)
{
    const uint32_t until = millis() + ms;
    do {
        web_pump();
        emscripten_sleep(std::min<uint32_t>(10, ms));
    } while (millis() < until);
}

std::uint32_t Hal::millis()
{
    return static_cast<std::uint32_t>(emscripten_get_now());
}

// main.cpp calls this once per loop iteration: render a frame and yield to the browser.
void Hal::feedTheDog()
{
    web_pump();
    emscripten_sleep(4);
}

std::array<uint8_t, 6> Hal::getFactoryMac()
{
    return {0x44, 0x1B, 0xF6, 0x00, 0x5E, 0xB0};
}

std::string Hal::getFactoryMacString(std::string divider)
{
    auto mac = getFactoryMac();
    return fmt::format("{:02X}{}{:02X}{}{:02X}{}{:02X}{}{:02X}{}{:02X}", mac[0], divider, mac[1], divider, mac[2],
                       divider, mac[3], divider, mac[4], divider, mac[5]);
}

void Hal::reboot()
{
    web_reload();
}

void Hal::factoryReset()
{
    mclog::tagInfo(_tag, "factory reset: clear simulated NVS");
    for (const char* ns : {"system", "alarm"}) {
        Settings(ns, true).EraseAll();
    }
    reboot();
}

// Device-only bring-up steps: nothing to do in the browser.
void Hal::i2c_init() {}
void Hal::i2c_detect() {}
void Hal::pmic_init() {}
void Hal::ioe_init() {}
void Hal::ioe_tp_reset() {}
void Hal::ioe_speaker_enable(bool) {}
void Hal::touchpad_init() {}
void Hal::imu_init() {}
void Hal::rtc_init() {}
void Hal::fs_init() {}

bool Hal::pmic_get_pwr_btn_state()
{
    return web_btn_state(2);
}

bool Hal::shouldShowGuide()
{
    Settings settings(std::string(Hal::SettingsNs), true);
    const int launch_count = settings.GetInt("launch_count", 0);
    if (launch_count >= 5) {
        return false;
    }
    settings.SetInt("launch_count", launch_count + 1);
    return true;
}

/* -------------------------------------------------------------------------- */
/*                                    Power                                   */
/* -------------------------------------------------------------------------- */
uint8_t Hal::getBatteryLevel()
{
    return static_cast<uint8_t>(std::clamp(web_battery_level(), 0, 100));
}

bool Hal::isBatteryCharging(bool)
{
    return web_battery_charging();
}

/* -------------------------------------------------------------------------- */
/*                                   Display                                  */
/* -------------------------------------------------------------------------- */
static LGFX_Device _display_stub;
static LGFX_Sprite _canvas_stub;

void Hal::display_init()
{
    setBackLightBrightness(getBackLightBrightness(true), false);
}

LGFX_Device& Hal::getDisplay()
{
    return _display_stub;
}

LGFX_Sprite& Hal::getCanvas()
{
    return _canvas_stub;
}

void Hal::updateCanvas() {}

void Hal::setBackLightBrightness(int brightness, bool saveToSettings)
{
    _bl_brightness = uitk::clamp(brightness, 0, 100);
    web_set_brightness(_bl_brightness);
    if (saveToSettings) {
        Settings(std::string(Hal::SettingsNs), true).SetInt("bl_lev", _bl_brightness);
    }
}

int Hal::getBackLightBrightness(bool loadFromSettings)
{
    if (loadFromSettings) {
        _bl_brightness = uitk::clamp((int)Settings(std::string(Hal::SettingsNs)).GetInt("bl_lev", 80), 10, 100);
    }
    return _bl_brightness;
}

Hal::TouchPoint Hal::getTouchPoint()
{
    Hal::TouchPoint point;
    if (web_touch_down()) {
        point.num = 1;
        point.x   = web_touch_x();
        point.y   = web_touch_y();
    }
    return point;
}

static void lvgl_read_cb(lv_indev_t*, lv_indev_data_t* data)
{
    auto tp = GetHAL().getTouchPoint();
    if (tp.num == 0) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = tp.x;
        data->point.y = tp.y;
    }
}

void Hal::lvgl_init()
{
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return GetHAL().millis(); });

    lv_display_t* disp = lv_sdl_window_create(_screen_width, _screen_height);

    // Same pointer indev path as the device (touch -> getTouchPoint), fed by
    // browser pointer events instead of the CST820.
    lvTouchpad = lv_indev_create();
    lv_indev_set_type(lvTouchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvTouchpad, lvgl_read_cb);
    lv_indev_set_display(lvTouchpad, disp);

    uitk::lvgl_cpp::ScreenActive screen;
    screen.setBgColor(lv_color_black());
    bootLogo = std::make_unique<BootLogo>();
    web_pump();
}

// Single-threaded in the browser: LVGL is only touched from the main loop.
bool Hal::lvglLock()
{
    return true;
}

void Hal::lvglUnlock() {}
void Hal::startLvglUpdate() {}
void Hal::stopLvglUpdate() {}

/* -------------------------------------------------------------------------- */
/*                                    Audio                                   */
/* -------------------------------------------------------------------------- */
namespace {

// Spectrum pipeline copied from hal_audio.cpp so the FFT app looks the same.
constexpr int spectrum_fft_size = 512;
constexpr int spectrum_hop_size = 256;
constexpr std::size_t band_count = Hal::AudioSpectrumFrame::bandCount;

struct Spectrum {
    std::array<int16_t, spectrum_hop_size> pcm_hop            = {};
    std::array<float, spectrum_fft_size> time_domain          = {};
    std::array<float, spectrum_fft_size> window               = {};
    std::array<float, spectrum_fft_size * 2> fft              = {};
    std::array<float, band_count> raw_bands                   = {};
    std::array<float, band_count> smoothed_bands              = {};
    std::array<float, band_count> noise_floor                 = {};
    std::array<int, band_count + 1> band_bin_edges            = {};
    std::size_t samples_ready                                 = 0;
    float normalization_level                                 = 0.03f;

    void init()
    {
        for (int i = 0; i < spectrum_fft_size; ++i) {
            window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (spectrum_fft_size - 1)));
        }
        constexpr int max_bin = spectrum_fft_size / 2;
        const float nyquist   = _sample_rate * 0.5f;
        const float min_hz    = static_cast<float>(_sample_rate) / spectrum_fft_size;
        const float log_min   = std::log10(min_hz);
        const float log_max   = std::log10(nyquist);
        band_bin_edges[0]     = 1;
        for (std::size_t i = 1; i < band_count; ++i) {
            float t           = static_cast<float>(i) / static_cast<float>(band_count);
            float edge_hz     = std::pow(10.0f, log_min + (log_max - log_min) * t);
            int edge_bin      = static_cast<int>(std::lround(edge_hz * spectrum_fft_size / _sample_rate));
            band_bin_edges[i] = std::clamp(edge_bin, band_bin_edges[i - 1] + 1,
                                           max_bin - static_cast<int>(band_count - i));
        }
        band_bin_edges[band_count] = max_bin;
    }

    // In-place radix-2 complex FFT on interleaved re/im.
    void fft_inplace()
    {
        const int n = spectrum_fft_size;
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) {
                std::swap(fft[i * 2], fft[j * 2]);
                std::swap(fft[i * 2 + 1], fft[j * 2 + 1]);
            }
        }
        for (int len = 2; len <= n; len <<= 1) {
            const float ang = -2.0f * M_PI / len;
            for (int i = 0; i < n; i += len) {
                for (int k = 0; k < len / 2; ++k) {
                    const float wr = std::cos(ang * k), wi = std::sin(ang * k);
                    const int a = (i + k) * 2, b = (i + k + len / 2) * 2;
                    const float xr = fft[b] * wr - fft[b + 1] * wi;
                    const float xi = fft[b] * wi + fft[b + 1] * wr;
                    fft[b]         = fft[a] - xr;
                    fft[b + 1]     = fft[a + 1] - xi;
                    fft[a] += xr;
                    fft[a + 1] += xi;
                }
            }
        }
    }

    bool read_hop()
    {
        if (!web_mic_read(pcm_hop.data(), spectrum_hop_size)) {
            return false;
        }
        std::move(time_domain.begin() + spectrum_hop_size, time_domain.end(), time_domain.begin());
        for (int i = 0; i < spectrum_hop_size; ++i) {
            time_domain[spectrum_fft_size - spectrum_hop_size + i] = pcm_hop[i] / 32768.0f;
        }
        samples_ready = std::min<std::size_t>(samples_ready + spectrum_hop_size, spectrum_fft_size);
        return true;
    }

    void process(Hal::AudioSpectrumFrame& frame)
    {
        float mean = 0.0f;
        for (float s : time_domain) mean += s;
        mean /= spectrum_fft_size;
        for (int i = 0; i < spectrum_fft_size; ++i) {
            fft[i * 2]     = (time_domain[i] - mean) * window[i];
            fft[i * 2 + 1] = 0.0f;
        }
        fft_inplace();

        float peak_bin_magnitude = 0.0f;
        int peak_bin_index       = 0;
        float top1 = 0.0f, top2 = 0.0f, top3 = 0.0f;
        for (std::size_t band = 0; band < band_count; ++band) {
            float energy = 0.0f, peak = 0.0f;
            int count = 0;
            for (int bin = band_bin_edges[band]; bin < band_bin_edges[band + 1]; ++bin) {
                float re  = fft[bin * 2], im = fft[bin * 2 + 1];
                float mag = std::sqrt(re * re + im * im) * (2.0f / spectrum_fft_size);
                if (mag > peak_bin_magnitude) {
                    peak_bin_magnitude = mag;
                    peak_bin_index     = bin;
                }
                peak = std::max(peak, mag);
                energy += mag * mag;
                ++count;
            }
            float rms = count > 0 ? std::sqrt(energy / count) : 0.0f;
            float raw = (rms * 0.48f + peak * 0.52f) * (1.12f - 0.22f * (float(band) / float(band_count - 1)));
            float floor_alpha = raw < noise_floor[band] ? 0.45f : 0.004f;
            noise_floor[band] += (raw - noise_floor[band]) * floor_alpha;
            raw             = std::max(raw - (noise_floor[band] * 2.20f + 0.0018f), 0.0f);
            raw_bands[band] = raw;
            if (raw >= top1) {
                top3 = top2, top2 = top1, top1 = raw;
            } else if (raw >= top2) {
                top3 = top2, top2 = raw;
            } else if (raw > top3) {
                top3 = raw;
            }
        }

        float frame_reference = std::max(top1 * 0.80f + top2 * 0.14f + top3 * 0.06f, 0.0015f);
        float norm_alpha      = frame_reference > normalization_level ? 0.44f : 0.16f;
        normalization_level += (frame_reference - normalization_level) * norm_alpha;
        normalization_level   = std::clamp(normalization_level, 0.0015f, 1.0f);
        frame.peakFrequencyHz = peak_bin_magnitude > 0.0f ? peak_bin_index * float(_sample_rate) / spectrum_fft_size
                                                          : 0.0f;

        for (std::size_t band = 0; band < band_count; ++band) {
            float normalized = std::clamp(std::pow(raw_bands[band] / normalization_level, 0.55f), 0.0f, 1.0f);
            if (normalized < 0.035f) normalized = 0.0f;
            float smooth_alpha = normalized > smoothed_bands[band] ? 0.82f : 0.40f;
            smoothed_bands[band] += (normalized - smoothed_bands[band]) * smooth_alpha;
            frame.bands[band] = std::clamp(smoothed_bands[band], 0.0f, 1.0f);
        }
    }
} _spectrum;

}  // namespace

void Hal::audio_init()
{
    _spectrum.init();
    _spk_volume = getSpeakerVolume(true);
}

void Hal::setSpeakerVolume(int volume, bool saveToSettings)
{
    _spk_volume = uitk::clamp(volume, 0, 100);
    if (saveToSettings) {
        Settings(std::string(Hal::SettingsNs), true).SetInt("spk_vol", _spk_volume);
    }
}

int Hal::getSpeakerVolume(bool loadFromSettings)
{
    if (loadFromSettings) {
        _spk_volume = uitk::clamp((int)Settings(std::string(Hal::SettingsNs)).GetInt("spk_vol", 80), 0, 100);
    }
    return _spk_volume;
}

int Hal::getAudioSampleRate()
{
    return _sample_rate;
}

void Hal::audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float)
{
    data.assign(static_cast<size_t>(_sample_rate) * durationMs / 1000, 0);
    for (size_t off = 0; off + spectrum_hop_size <= data.size(); off += spectrum_hop_size) {
        while (!web_mic_read(data.data() + off, spectrum_hop_size)) {
            delay(5);
        }
    }
}

void Hal::audioPlay(std::vector<int16_t>& data, bool)
{
    if (!data.empty()) {
        web_audio_play(data.data(), static_cast<int>(data.size()), _sample_rate, _spk_volume);
    }
}

void Hal::updateAudioSpectrum()
{
    // Drain every hop the browser has buffered so the display tracks real time.
    bool updated = false;
    while (_spectrum.read_hop()) {
        updated = true;
    }
    if (updated && _spectrum.samples_ready >= spectrum_fft_size) {
        _spectrum.process(_audio_spectrum);
    }
}

extern "C" const uint8_t web_boot_sfx[];
extern "C" const size_t web_boot_sfx_size;

void Hal::playBootSfx()
{
    const auto* samples = reinterpret_cast<const int16_t*>(web_boot_sfx);
    std::vector<int16_t> pcm(samples, samples + web_boot_sfx_size / sizeof(int16_t));
    audioPlay(pcm, true);
}

/* -------------------------------------------------------------------------- */
/*                                  Vibrator                                  */
/* -------------------------------------------------------------------------- */
void Hal::vibrate(uint16_t durationMs, uint8_t strength)
{
    web_vibrate(durationMs, strength);
}

void Hal::stopVibrate()
{
    web_vibrate(0, 0);
}

/* -------------------------------------------------------------------------- */
/*                                     IMU                                    */
/* -------------------------------------------------------------------------- */
void Hal::updateImuData()
{
    _imu_data.accelX = web_imu(0);
    _imu_data.accelY = web_imu(1);
    _imu_data.accelZ = web_imu(2);
    _imu_data.gyroX  = web_imu(3);
    _imu_data.gyroY  = web_imu(4);
    _imu_data.gyroZ  = web_imu(5);
}

/* -------------------------------------------------------------------------- */
/*                                    Time                                    */
/* -------------------------------------------------------------------------- */
// The simulator follows the browser clock; setting time/timezone is logged only.
void Hal::syncRtcTimeToSystem() {}
void Hal::syncSystemTimeToRtc() {}

DateYmd Hal::getDateYmd()
{
    std::time_t now = std::time(nullptr);
    std::tm tm_curr{};
    localtime_r(&now, &tm_curr);
    return DateYmd{static_cast<uint16_t>(tm_curr.tm_year + 1900), static_cast<uint8_t>(tm_curr.tm_mon + 1),
                   static_cast<uint8_t>(tm_curr.tm_mday)};
}

bool Hal::setDateYmd(const DateYmd& date)
{
    web_status(fmt::format("setDate {:04d}-{:02d}-{:02d} (simulator keeps browser clock)", date.year, date.month,
                           date.day)
                   .c_str());
    return date.isValid();
}

TimeHms Hal::getTimeHms()
{
    std::time_t now = std::time(nullptr);
    std::tm tm_curr{};
    localtime_r(&now, &tm_curr);
    return TimeHms{static_cast<uint8_t>(tm_curr.tm_hour), static_cast<uint8_t>(tm_curr.tm_min),
                   static_cast<uint8_t>(tm_curr.tm_sec)};
}

bool Hal::setTimeHms(const TimeHms& time)
{
    web_status(fmt::format("setTime {:02d}:{:02d}:{:02d} (simulator keeps browser clock)", time.hour, time.minute,
                           time.second)
                   .c_str());
    return time.isValid();
}

void Hal::setTimezone(std::string_view tz)
{
    Settings("system", true).SetString("tz", std::string(tz));
}

std::string Hal::getTimezone()
{
    return Settings("system").GetString("tz", "GMT0");
}

bool Hal::loadAlarmStorage(AlarmStorageSnapshot& snapshot)
{
    snapshot         = AlarmStorageSnapshot{};
    size_t blob_size = sizeof(snapshot);
    Settings("alarm").GetBlob("storage", &snapshot, &blob_size);
    return true;
}

bool Hal::saveAlarmStorage(const AlarmStorageSnapshot& snapshot)
{
    return Settings("alarm", true).SetBlob("storage", &snapshot, sizeof(snapshot)) == ESP_OK;
}

void Hal::startAlarm()
{
    _alarm_active     = true;
    _alarm_beep_index = 0;
    _alarm_next_beep  = 0;
}

void Hal::stopAlarm()
{
    _alarm_active = false;
}

/* -------------------------------------------------------------------------- */
/*                                   Button                                   */
/* -------------------------------------------------------------------------- */
void Hal::button_init()
{
    getButtonConfig(true);
}

void Hal::updateButtonStates()
{
    btnA.setRawState(millis(), web_btn_state(0));
    btnB.setRawState(millis(), web_btn_state(1));

    auto& config = getButtonConfig();
    if (btnA.wasPressed() || btnB.wasPressed()) {
        if (config.sfxEnabled) {
            audio::play_tone_from_midi((btnA.wasPressed() ? 62 : 64) + 32, 0.02);
        }
        if (config.vibrateEnabled) {
            vibrate(20, 60);
        }
    }
}

void Hal::setButtonConfig(ButtonConfig config, bool saveToSettings)
{
    _btn_config = config;
    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetBool("btn_sfx", config.sfxEnabled);
        settings.SetBool("btn_vibrate", config.vibrateEnabled);
    }
}

const Hal::ButtonConfig& Hal::getButtonConfig(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings{std::string(Hal::SettingsNs)};
        _btn_config.sfxEnabled     = settings.GetBool("btn_sfx", true);
        _btn_config.vibrateEnabled = settings.GetBool("btn_vibrate", true);
    }
    return _btn_config;
}

/* -------------------------------------------------------------------------- */
/*                                    Badge                                   */
/* -------------------------------------------------------------------------- */
// Badge slots are uploaded over the device's Wi-Fi AP; the simulator shows the
// built-in default badge and explains that editing needs real hardware.
bool Hal::loadBadgeImage(lv_obj_t* image)
{
    lv_image_set_src(image, &icon_badge);
    return false;
}

bool Hal::loadNextBadgeImage(lv_obj_t* image)
{
    return loadBadgeImage(image);
}

bool Hal::loadPreviousBadgeImage(lv_obj_t* image)
{
    return loadBadgeImage(image);
}

void Hal::startBadgeEditModeViaAp(std::function<void(std::string_view)> onLog)
{
    if (onLog) {
        onLog("Badge AP editing needs the real device");
    }
    delay(1500);
}

/* -------------------------------------------------------------------------- */
/*                                 Voice Code                                 */
/* -------------------------------------------------------------------------- */
// The page's "Mac / Claude Code" panel stands in for the BLE link: it toggles
// the connection, shows push-to-talk key sequences, and sends hook states.
#include "hal/utils/voice_code_protocol.h"

// clang-format off
EM_JS(int, web_voice_connected, (), { return Module.sim.voice.connected ? 1 : 0; });
EM_JS(int, web_voice_seq, (), { return Module.sim.voice.seq | 0; });
EM_JS(char*, web_voice_payload, (), { return stringToNewUTF8(Module.sim.voice.payload); });
EM_JS(void, web_voice_talk, (int pressed, int submit, const char* agent), { Module.sim.onVoiceTalk(!!pressed, !!submit, UTF8ToString(agent)); });
// clang-format on

static std::array<VoiceCodeStatus, voice_code::AgentCount> _voice_statuses;
static int _voice_seq_seen = 0;

void Hal::voiceLinkStart()
{
    web_status("voice link: simulated BLE keyboard 'M5 Voice Code' started");
}

bool Hal::voiceLinkConnected()
{
    return web_voice_connected();
}

void Hal::voiceLinkTalk(bool pressed, bool submit, VoiceAgent agent)
{
    web_voice_talk(pressed, submit, voice_code::agent_name(agent));
}

VoiceCodeStatus Hal::voiceLinkStatus(VoiceAgent agent)
{
    const int seq = web_voice_seq();
    if (seq != _voice_seq_seen) {
        _voice_seq_seen = seq;
        char* payload   = web_voice_payload();
        voice_code::apply_payload(_voice_statuses, payload, millis());
        free(payload);
    }
    return _voice_statuses[voice_code::agent_index(agent)];
}
