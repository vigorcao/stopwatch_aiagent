/*
 * Pet view, see pet_view.h. Frame timings follow the Codex V2 pet contract.
 */
#include "pet_view.h"
#include <hal/hal.h>
#include <hal/utils/voice_code_protocol.h>
#include <cmath>

using namespace view;

namespace {

constexpr uint32_t _color_title  = 0x2B2522;
constexpr uint32_t _color_detail = 0x6B5F57;
constexpr uint32_t _color_hint   = 0x9A8E84;
constexpr uint32_t _image_scale  = 320;  // 256 = 1:1; frames are 154x166
constexpr float _look_threshold  = 0.30f;  // in-plane gravity (g) before the pet looks around
constexpr float _upright_enter   = 30.0f;  // degrees off upright before the pet starts to look
constexpr float _upright_exit    = 20.0f;  // degrees off upright before it faces front again
constexpr float _shake_dps       = 400.0f;  // gyro speed that makes an idle pet jump
constexpr int _image_y           = 8;

// Peek pets: 192x208 frames drawn at 1.7x (326x354) and anchored to the bottom edge.
// LVGL scales around the image centre without growing the object, so the drawn bottom
// sits (1.7 - 1) * 208 / 2 = 73 px below the object bottom. -59 puts it 14 px past the
// screen edge, so the paws rest on the bezel.
constexpr uint32_t _peek_scale   = 435;
constexpr int _peek_rest_y       = -59;
constexpr int _peek_eyes_y       = _peek_rest_y + 130;  // only the tuft and eyes show
constexpr int _peek_hidden_y     = _peek_rest_y + 370;  // fully below the screen
constexpr uint32_t _rise_eyes_ms = 700;   // hidden -> eyes
constexpr uint32_t _rise_hold_ms = 600;   // hold with just the tuft and eyes out
constexpr uint32_t _rise_pop_ms  = 450;   // eyes -> rest, with a small overshoot

// Walk-in pets: start fully off the left edge (466 px screen, 192 px drawn frame) and
// walk to the centre at a steady pace, so the feet do not slide.
constexpr int _walk_hidden_x      = -340;
constexpr uint32_t _walk_ms       = 1500;
constexpr uint16_t _walk_frame_ms = 90;
constexpr uint8_t _walk_frames    = 8;

const PetView::Clip _idle     = {"idle", 6, 140, 320, true};
const PetView::Clip _waving   = {"waving", 4, 140, 280, true};
const PetView::Clip _jumping  = {"jumping", 5, 140, 280, false};
const PetView::Clip _failed   = {"failed", 8, 140, 240, false};
const PetView::Clip _waiting  = {"waiting", 6, 150, 260, true};
const PetView::Clip _running  = {"running", 6, 120, 220, true};
const PetView::Clip _review   = {"review", 6, 150, 280, true};

const PetView::Clip* clip_for(VoiceMood mood)
{
    switch (mood) {
        case VoiceMood::Listening: return &_waving;
        case VoiceMood::Sending: return &_running;
        case VoiceMood::Thinking: return &_review;
        case VoiceMood::Working: return &_running;
        case VoiceMood::Permission: return &_waiting;
        case VoiceMood::Done: return &_jumping;
        case VoiceMood::Error: return &_failed;
        default: return &_idle;
    }
}

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int y)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, 360);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

}  // namespace

void PetView::init(lv_obj_t* parent)
{
    _screen = lv_obj_create(parent);
    lv_obj_remove_style_all(_screen);
    lv_obj_set_size(_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_screen, lv_color_hex(_bg), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    _image = lv_image_create(_screen);
    if (_peek) {
        lv_image_set_scale(_image, _peek_scale);
        lv_obj_align(_image, LV_ALIGN_BOTTOM_MID, 0, _peek_hidden_y);
    } else {
        lv_image_set_scale(_image, _image_scale);
        lv_obj_align(_image, LV_ALIGN_CENTER, _walk_in ? _walk_hidden_x : 0, _image_y);
    }

    // Peek pets fill the lower half, so all text moves above them.
    // Peek pets fill the lower two thirds: one compact text block above the head,
    // the hint takes the detail line when there is no detail.
    _title  = make_label(_screen, &lv_font_montserrat_28, _color_title, _peek ? 50 : 58);
    _detail = make_label(_screen, &lv_font_montserrat_18, _color_detail, _peek ? 118 : 360);
    _hint   = make_label(_screen, &lv_font_montserrat_16, _color_hint, 392);
    if (_peek) {
        lv_obj_set_width(_detail, 300);
        lv_obj_add_flag(_hint, LV_OBJ_FLAG_HIDDEN);
    }

    _badge = lv_label_create(_screen);
    lv_obj_set_style_text_font(_badge, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_badge, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(_badge, 12, 0);
    lv_obj_set_style_pad_ver(_badge, 3, 0);
    lv_obj_align(_badge, LV_ALIGN_TOP_MID, 0, _peek ? 86 : 96);
    lv_obj_add_flag(_badge, LV_OBJ_FLAG_HIDDEN);

    _mood = VoiceMood::Offline;  // force setMood to apply
    setMood(VoiceMood::Idle);
    _entering = _peek || _walk_in;  // update_rise_in() / update_walk_in() start the clock on the first frame
}

void PetView::setAgent(VoiceAgent agent, bool switchable, VoiceAgent next)
{
    _agent      = agent;
    _switchable = switchable;
    _next       = next;
    if (!_switchable) {
        lv_obj_add_flag(_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        const uint32_t color = agent == VoiceAgent::Gpt ? 0x10A37F : agent == VoiceAgent::OpenClaw ? 0xE5484D : 0xD97757;
        lv_label_set_text(_badge, voice_code::agent_label(agent));
        lv_obj_set_style_bg_color(_badge, lv_color_hex(color), 0);
        lv_obj_remove_flag(_badge, LV_OBJ_FLAG_HIDDEN);
    }
    setMood(_mood, std::string(_detail_text));  // same mood: refresh texts only
}

void PetView::destroy()
{
    if (_screen) {
        lv_obj_delete(_screen);
        _screen = nullptr;
    }
}

void PetView::setMood(VoiceMood mood, std::string_view detail)
{
    const char* title = "";
    const std::string hint_text = _switchable ? std::string("A: talk  |  hold B: ") + voice_code::agent_label(_next)
                                              : std::string("Hold A to talk to ") + voice_code::agent_label(_agent);
    const char* hint  = hint_text.c_str();
    switch (mood) {
        case VoiceMood::Idle: title = "Ready"; break;
        case VoiceMood::Listening: title = "Listening..."; hint = "Release A to send"; break;
        case VoiceMood::Sending: title = "Sending..."; hint = ""; break;
        case VoiceMood::Thinking: title = "Thinking..."; break;
        case VoiceMood::Working: title = "Working"; break;
        case VoiceMood::Permission: title = "Needs you!"; hint = "Check your Mac"; break;
        case VoiceMood::Done: title = "Done!"; break;
        case VoiceMood::Error: title = "Oops"; break;
        case VoiceMood::Offline:
            title  = "Not connected";
            detail = detail.empty() ? "Pair \"M5 Voice Code\" in Mac Bluetooth" : detail;
            hint   = "A+B hold: home";
            break;
    }
    _detail_text = std::string(detail);
    lv_label_set_text(_title, title);
    lv_label_set_text(_detail, std::string(_peek && detail.empty() ? hint : detail).c_str());
    lv_label_set_text(_hint, hint);

    if (mood != _mood) {
        _mood = mood;
        play(clip_for(mood));
    }
}

void PetView::play(const Clip* clip)
{
    _clip        = clip;
    _frame       = 0;
    _frame_until = 0;  // show the first frame on the next update
    _look        = -1;
}

void PetView::show(const std::string& file)
{
    const std::string src = "A:/spiflash/pets/" + _pet_id + "/" + file + ".bin";
    if (src != _src) {
        _src = src;
        lv_image_set_src(_image, _src.c_str());
    }
}

// Rises from below the bottom edge: eyes first, a pause, then a pop with overshoot.
// Returns true while the rise-in is still going.
bool PetView::update_rise_in(uint32_t now)
{
    if (!_entering || !_peek) {
        return false;
    }
    if (_enter_start == 0) {
        _enter_start = now ? now : 1;
    }
    const uint32_t t = now - _enter_start;
    float y;
    if (t < _rise_eyes_ms) {
        const float k = static_cast<float>(t) / _rise_eyes_ms;
        const float e = 1.0f - (1.0f - k) * (1.0f - k);  // ease-out quad
        y             = _peek_hidden_y + (_peek_eyes_y - _peek_hidden_y) * e;
    } else if (t < _rise_eyes_ms + _rise_hold_ms) {
        y = _peek_eyes_y;
    } else if (t < _rise_eyes_ms + _rise_hold_ms + _rise_pop_ms) {
        const float k  = static_cast<float>(t - _rise_eyes_ms - _rise_hold_ms) / _rise_pop_ms;
        const float c1 = 1.70158f, c3 = c1 + 1.0f;  // ease-out back
        const float e  = 1.0f + c3 * std::pow(k - 1.0f, 3.0f) + c1 * std::pow(k - 1.0f, 2.0f);
        y              = _peek_eyes_y + (_peek_rest_y - _peek_eyes_y) * e;
    } else {
        y         = _peek_rest_y;
        _entering = false;
    }
    lv_obj_set_y(_image, static_cast<int>(y));
    return _entering;
}

// Walks in from the left edge to the centre, then hands over to the current clip.
// Returns true while the walk-in is still going.
bool PetView::update_walk_in(uint32_t now)
{
    if (!_entering || !_walk_in) {
        return false;
    }
    if (_enter_start == 0) {
        _enter_start = now ? now : 1;
    }
    const uint32_t t = now - _enter_start;
    if (t >= _walk_ms) {
        lv_obj_set_x(_image, 0);
        _entering = false;
        play(_clip);  // restart the mood clip from its first frame
        return false;
    }
    const float k = static_cast<float>(t) / _walk_ms;
    lv_obj_set_x(_image, static_cast<int>(_walk_hidden_x * (1.0f - k)));
    show("walk_" + std::to_string((t / _walk_frame_ms) % _walk_frames));
    return true;
}

void PetView::update(uint32_t now)
{
    if (!_clip) {
        return;
    }
    if (update_walk_in(now)) {
        return;
    }
    const bool rising_in = update_rise_in(now);

    // Idle (or offline) pets react to the IMU:
    //   flat, upright or on the wrist -> face front (normal idle loop)
    //   leaned left / right / upside down -> look toward the low side
    //   shaken -> jump once, then settle
    if (!rising_in && (_mood == VoiceMood::Idle || _mood == VoiceMood::Offline) && _clip != &_jumping) {
        GetHAL().updateImuData();
        const auto& imu  = GetHAL().getImuData();
        const float spin = std::sqrt(imu.gyroX * imu.gyroX + imu.gyroY * imu.gyroY + imu.gyroZ * imu.gyroZ);
        if (spin > _shake_dps) {
            play(&_jumping);
            return;
        }

        // On this board accelY reads the reaction to gravity (it points up), but the HAL's
        // X/Y swap leaves accelX pointing to the low side. Flip Y only to get the low side.
        const float down_x = imu.accelX;
        const float down_y = -imu.accelY;
        int look           = -1;
        if (std::sqrt(down_x * down_x + down_y * down_y) > _look_threshold) {
            // 0 deg = up (12 o'clock), clockwise, same as the pet contract. 180 = held upright.
            float deg = std::atan2(down_x, -down_y) * 180.0f / 3.14159265f;
            deg       = std::fmod(deg + 360.0f, 360.0f);
            if (std::fabs(deg - 180.0f) > (_look >= 0 ? _upright_exit : _upright_enter)) {
                look = static_cast<int>(std::lround(deg / 22.5f)) % 16;
            }
        }
        if (look >= 0) {
            if (look != _look) {
                _look = look;
                show("look_" + std::to_string(look));
            }
            return;
        }
        if (_look >= 0) {
            _look = -1;
            play(_clip);
        }
    }

    if (now < _frame_until) {
        return;
    }
    show(std::string(_clip->state) + "_" + std::to_string(_frame));
    const bool last = _frame == _clip->frames - 1;
    _frame_until    = now + (last ? _clip->last_ms : _clip->frame_ms);

    if (!last) {
        _frame++;
    } else if (_clip->loop) {
        _frame = 0;
    } else if (_clip == &_jumping) {
        play(&_idle);  // celebrate once, then settle
        _frame_until = now + _clip->last_ms;
    }
}
