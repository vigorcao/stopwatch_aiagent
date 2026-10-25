/*
 * Voice Code app, see app_voice_code.h.
 */
#include "app_voice_code.h"
#include "view/pet_view.h"
#include "view/view.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <apps/common/audio/audio.h>

using Mood = view::VoiceMood;

namespace {

constexpr uint32_t _sending_timeout_ms = 4000;
constexpr uint32_t _done_linger_ms     = 6000;
constexpr uint32_t _color_clock        = 0x2E1E16;  // same dark brown as the views' titles

Mood mood_from_state(VoiceCodeState state)
{
    switch (state) {
        case VoiceCodeState::Thinking: return Mood::Thinking;
        case VoiceCodeState::Working: return Mood::Working;
        case VoiceCodeState::Permission: return Mood::Permission;
        case VoiceCodeState::Done: return Mood::Done;
        case VoiceCodeState::Error: return Mood::Error;
        default: return Mood::Idle;
    }
}

}  // namespace

AppVoiceCode::Config AppVoiceCode::claude()
{
    return {"VoiceCode", &icon_voice_code, VoiceAgent::Claude, [] { return std::make_unique<view::VoiceCodeView>(); }};
}

AppVoiceCode::Config AppVoiceCode::gpt()
{
    return {"GPT", &icon_voice_gpt, VoiceAgent::Gpt, [] { return std::make_unique<view::PetView>("heidou", false, 0xF0ECE2, true); }};
}

AppVoiceCode::Config AppVoiceCode::kurotamago()
{
    return {"Tamago", &icon_voice_tamago, VoiceAgent::Claude,
            [] { return std::make_unique<view::PetView>("kurotamago"); }, {VoiceAgent::Claude, VoiceAgent::Gpt}};
}

AppVoiceCode::Config AppVoiceCode::meiqiu()
{
    return {"Meiqiu", &icon_voice_meiqiu, VoiceAgent::Claude,
            [] { return std::make_unique<view::PetView>("meiqiu3d", true, 0xFFD43B); },
            {VoiceAgent::Claude, VoiceAgent::OpenClaw, VoiceAgent::Gpt}};
}

AppVoiceCode::AppVoiceCode(Config config) : _config(std::move(config)), _agent(_config.agent)
{
    setAppInfo().name = _config.name;
    setAppInfo().icon = const_cast<void*>(_config.icon);
}

void AppVoiceCode::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppVoiceCode::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // BLE stays up after the first open so the Mac keeps the keyboard paired.
    GetHAL().voiceLinkStart();

    _key_manager = std::make_unique<input::KeyManager>();
    _talking     = false;

    LvglLockGuard lock;
    _view = _config.makeView();
    _view->init(lv_screen_active());
    _view->setAgent(_agent, switchable(), next_agent());

    _status     = GetHAL().voiceLinkStatus(_agent);
    _status_seq = _status.seq;
    apply_status(false);

    // Time and battery, so you can tell both at a glance while away from the Mac.
    _clock        = std::make_unique<view::ArcTopClock>(lv_screen_active());
    _clock->color = _color_clock;
    _clock->init();
    _clock->align(LV_ALIGN_TOP_MID, 0, 4);
    _clock->update(true);

    _battery = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(_battery, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_battery, lv_color_hex(_color_clock), 0);
    if (_view->coversBottom()) {
        lv_obj_align(_battery, LV_ALIGN_TOP_MID, 150, 156);  // right of the pet's tuft, below the hint
    } else {
        lv_obj_align(_battery, LV_ALIGN_BOTTOM_MID, 0, -24);
    }
    update_battery(true);
}

void AppVoiceCode::update_battery(bool force)
{
    const uint32_t now = GetHAL().millis();
    if (!force && now - _battery_refresh < 2000) {
        return;
    }
    _battery_refresh    = now;
    const uint8_t level = GetHAL().getBatteryLevel();
    const char* icon    = level > 80   ? LV_SYMBOL_BATTERY_FULL
                          : level > 55 ? LV_SYMBOL_BATTERY_3
                          : level > 30 ? LV_SYMBOL_BATTERY_2
                          : level > 10 ? LV_SYMBOL_BATTERY_1
                                       : LV_SYMBOL_BATTERY_EMPTY;
    const bool charging = GetHAL().isBatteryCharging();
    lv_label_set_text_fmt(_battery, "%s %u%%%s", icon, static_cast<unsigned>(level), charging ? " " LV_SYMBOL_CHARGE : "");
    lv_obj_set_style_text_color(_battery, lv_color_hex(level <= 20 && !charging ? 0xD0402B : _color_clock), 0);
}

void AppVoiceCode::apply_status(bool alert)
{
    if (!GetHAL().voiceLinkConnected()) {
        _view->setMood(Mood::Offline);
        return;
    }

    const Mood mood = mood_from_state(_status.state);
    _view->setMood(mood, _status.detail);
    if (mood == Mood::Done) {
        _done_since = GetHAL().millis();
    }

    if (!alert) {
        return;
    }
    if (mood == Mood::Permission) {
        GetHAL().vibrate(250, 100);
        audio::play_melody({84, 88, 91}, 0.08f);
    } else if (mood == Mood::Done) {
        GetHAL().vibrate(60, 80);
        audio::play_melody({79, 84}, 0.06f);
    } else if (mood == Mood::Error) {
        GetHAL().vibrate(120, 90);
    }
}

void AppVoiceCode::onRunning()
{
    auto& hal          = GetHAL();
    const auto event   = _key_manager->update();
    const uint32_t now = hal.millis();

    if (event == input::KeyEvent::GoHome) {
        if (_talking) {
            hal.voiceLinkTalk(false, false, _agent);  // cancel: discard the recording
            _talking = false;
        }
        close();
        return;
    }

    LvglLockGuard lock;

    if (event == input::KeyEvent::GoNext) {
        if (_detail.isOpen()) {
            _detail.close();
        } else {
            _detail.open(lv_screen_active(), _agent);
        }
    }
    _detail.update();
    _clock->update();
    update_battery(false);

    if (switchable() && !_talking && hal.btnB.wasHold() && hal.btnA.isReleased()) {
        switch_agent();
    }

    // Push-to-talk on A.
    if (!_talking && hal.btnA.wasPressed() && hal.btnB.isReleased() && hal.voiceLinkConnected()) {
        _detail.close();
        _talking = true;
        hal.voiceLinkTalk(true, true, _agent);
        _view->setMood(Mood::Listening);
    } else if (_talking && hal.btnA.wasReleased()) {
        _talking = false;
        hal.voiceLinkTalk(false, true, _agent);
        _sending_since = now;
        _view->setMood(Mood::Sending);
    }

    // Status from the agent's hooks.
    VoiceCodeStatus status = hal.voiceLinkStatus(_agent);
    const bool fresh       = status.seq != _status_seq;
    if (fresh) {
        _status     = status;
        _status_seq = status.seq;
    }

    if (!_talking) {
        const Mood current = _view->mood();
        const bool connected = hal.voiceLinkConnected();
        if (fresh) {
            apply_status(true);
        } else if (current == Mood::Sending && now - _sending_since > _sending_timeout_ms) {
            apply_status(false);
        } else if ((current == Mood::Offline) == connected) {
            apply_status(false);  // link came up or dropped
        } else if (current == Mood::Done && now - _done_since > _done_linger_ms) {
            _view->setMood(Mood::Idle);  // settle back to Ready after the agent finishes
        }
    }

    _view->update(now);
}

VoiceAgent AppVoiceCode::next_agent() const
{
    const auto& agents = _config.agents;
    for (size_t i = 0; i < agents.size(); i++) {
        if (agents[i] == _agent) {
            return agents[(i + 1) % agents.size()];
        }
    }
    return agents.empty() ? _agent : agents.front();
}

void AppVoiceCode::switch_agent()
{
    _agent = next_agent();
    _view->setAgent(_agent, true, next_agent());
    if (_detail.isOpen()) {
        _detail.open(lv_screen_active(), _agent);
    }
    _status     = GetHAL().voiceLinkStatus(_agent);
    _status_seq = _status.seq;
    apply_status(false);
    GetHAL().vibrate(40, 80);
    audio::play_melody({_agent == VoiceAgent::Gpt ? 76 : _agent == VoiceAgent::OpenClaw ? 79 : 72}, 0.05f);
}

void AppVoiceCode::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;
    _detail.close();
    _clock.reset();
    if (_battery != nullptr) {
        lv_obj_delete(_battery);
        _battery = nullptr;
    }
    if (_view) {
        _view->destroy();
    }
    _view.reset();
}
