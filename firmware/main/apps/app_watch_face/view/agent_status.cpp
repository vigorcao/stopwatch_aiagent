/*
 * Claude / Codex status strip at the bottom of every watch face, so you can
 * tell from across the room whether the Mac is still working, done, or waiting
 * for you. Fed by the Voice Code BLE link (Claude Code / Codex hooks).
 */
#include "watch_face.h"
#include <hal/hal.h>
#include <hal/utils/voice_code_protocol.h>
#include <cstdio>
#include <string>

using namespace view;

namespace {

constexpr int _panel_w = 320;
constexpr int _panel_h = 76;
constexpr int _panel_y = -64;  // from the bottom; keeps the corners inside the round screen
constexpr int _row_h   = 30;

// Without a heartbeat for this long the Mac helper is gone (closed / asleep / out of range).
constexpr uint32_t _stale_ms = 40000;

constexpr uint32_t _color_name       = 0xFFFFFF;
constexpr uint32_t _color_idle       = 0x8E8E93;
constexpr uint32_t _color_thinking   = 0xB48CFF;
constexpr uint32_t _color_working    = 0x4AA3FF;
constexpr uint32_t _color_permission = 0xFFB020;
constexpr uint32_t _color_done       = 0x4CD964;
constexpr uint32_t _color_error      = 0xFF5A5A;
constexpr uint32_t _color_offline    = 0x5A5A5E;

void style_plain(lv_obj_t* obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);  // taps fall through to the watch face
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

lv_obj_t* make_label(lv_obj_t* parent, int x, int y, int w, lv_text_align_t align)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    return label;
}

std::string format_elapsed(uint32_t ms)
{
    const uint32_t s = ms / 1000;
    char buf[16];
    if (s < 3600) {
        std::snprintf(buf, sizeof(buf), "%u:%02u", static_cast<unsigned>(s / 60), static_cast<unsigned>(s % 60));
    } else {
        std::snprintf(buf, sizeof(buf), "%uh%02u", static_cast<unsigned>(s / 3600),
                      static_cast<unsigned>(s / 60 % 60));
    }
    return buf;
}

}  // namespace

void AgentStatusPanel::create(lv_obj_t* parent)
{
    // Keep the link up whenever the watch face is showing, not only after Voice Code was opened.
    GetHAL().voiceLinkStart();

    _panel = lv_obj_create(parent);
    style_plain(_panel);
    lv_obj_set_size(_panel, _panel_w, _panel_h);
    lv_obj_align(_panel, LV_ALIGN_BOTTOM_MID, 0, _panel_y);
    lv_obj_set_style_radius(_panel, 22, 0);
    lv_obj_set_style_bg_color(_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_70, 0);

    static constexpr const char* names[2] = {"Claude", "Codex"};
    for (int i = 0; i < 2; ++i) {
        Row& row       = _rows[i];
        const int y    = 8 + i * _row_h;
        row.dot        = lv_obj_create(_panel);
        style_plain(row.dot);
        lv_obj_set_size(row.dot, 12, 12);
        lv_obj_set_pos(row.dot, 20, y + 6);
        lv_obj_set_style_radius(row.dot, LV_RADIUS_CIRCLE, 0);
        row.name = make_label(_panel, 40, y, 78, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_color(row.name, lv_color_hex(_color_name), 0);
        lv_label_set_text(row.name, names[i]);
        row.state = make_label(_panel, 118, y, _panel_w - 118 - 20, LV_TEXT_ALIGN_RIGHT);
        row.seq   = GetHAL().voiceLinkStatus(i == 0 ? VoiceAgent::Claude : VoiceAgent::Gpt).seq;
    }
    _last_refresh = 0;
    update();
}

void AgentStatusPanel::destroy()
{
    if (_panel != nullptr) {
        lv_obj_delete(_panel);
        _panel = nullptr;
    }
}

void AgentStatusPanel::update()
{
    if (_panel == nullptr) {
        return;
    }
    auto& hal          = GetHAL();
    const uint32_t now = hal.millis();
    if (_last_refresh != 0 && now - _last_refresh < 250) {
        return;
    }
    _last_refresh = now;

    const bool connected = hal.voiceLinkConnected();
    const bool blink_on  = (now / 500) % 2 == 0;

    for (int i = 0; i < 2; ++i) {
        Row& row                     = _rows[i];
        const VoiceCodeStatus status = hal.voiceLinkStatus(i == 0 ? VoiceAgent::Claude : VoiceAgent::Gpt);

        // Buzz on the transitions worth walking back for.
        if (status.seq != row.seq) {
            if (status.state == VoiceCodeState::Permission) {
                hal.vibrate(250, 100);
            } else if (status.state == VoiceCodeState::Done && voice_code::is_busy(row.state_)) {
                hal.vibrate(80, 80);
            }
            row.seq   = status.seq;
            row.state_ = status.state;
        }

        uint32_t color = _color_idle;
        std::string text;
        const bool stale = status.heartbeat && now - status.updatedMs > _stale_ms;
        if (!connected) {
            color = _color_offline;
            text  = "Offline";
        } else if (stale) {
            color = _color_offline;
            text  = "No signal";
        } else {
            const std::string elapsed = "  " + format_elapsed(now - status.sinceMs);
            switch (status.state) {
                case VoiceCodeState::Thinking:
                    color = _color_thinking;
                    text  = "Thinking" + elapsed;
                    break;
                case VoiceCodeState::Working:
                    color = _color_working;
                    text  = "Working" + elapsed;
                    break;
                case VoiceCodeState::Permission:
                    color = _color_permission;
                    text  = "Needs you!";
                    break;
                case VoiceCodeState::Done:
                    color = _color_done;
                    text  = "Done " LV_SYMBOL_OK;
                    break;
                case VoiceCodeState::Error:
                    color = _color_error;
                    text  = "Error";
                    break;
                default:
                    text = "Idle";
                    break;
            }
        }

        const bool hide_dot = status.state == VoiceCodeState::Permission && connected && !stale && !blink_on;
        lv_obj_set_style_bg_color(row.dot, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(row.dot, hide_dot ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(row.state, lv_color_hex(color), 0);
        if (text != row.text) {
            row.text = text;
            lv_label_set_text(row.state, text.c_str());
        }
    }
}
