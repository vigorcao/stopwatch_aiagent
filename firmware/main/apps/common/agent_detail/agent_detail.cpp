#include "agent_detail.h"
#include <hal/utils/voice_code_protocol.h>
#include <mooncake_log.h>
#include <cstdio>

using namespace view;

namespace {

constexpr int _screen       = 466;
constexpr int _body_w       = 340;  // fits the round screen between y = 120 and 400
constexpr int _body_h       = 270;
constexpr int _body_y       = 118;
constexpr uint32_t _color_bg    = 0x111114;
constexpr uint32_t _color_title = 0xFFFFFF;
constexpr uint32_t _color_body  = 0xE6E6EA;
constexpr uint32_t _color_dim   = 0x8E8E93;

constexpr const char* _font_path = "A:/spiflash/fonts/cjk20.bin";

void style_plain(lv_obj_t* obj)
{
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

uint32_t state_color(VoiceCodeState state)
{
    switch (state) {
        case VoiceCodeState::Thinking: return 0xB48CFF;
        case VoiceCodeState::Working: return 0x4AA3FF;
        case VoiceCodeState::Permission: return 0xFFB020;
        case VoiceCodeState::Done: return 0x4CD964;
        case VoiceCodeState::Error: return 0xFF5A5A;
        default: return _color_dim;
    }
}

std::string state_line(const VoiceCodeStatus& status, uint32_t now)
{
    std::string text;
    switch (status.state) {
        case VoiceCodeState::Thinking: text = "Thinking"; break;
        case VoiceCodeState::Working: text = "Working"; break;
        case VoiceCodeState::Permission: text = "Needs you!"; break;
        case VoiceCodeState::Done: text = "Done " LV_SYMBOL_OK; break;
        case VoiceCodeState::Error: text = "Error"; break;
        default: text = "Idle"; break;
    }
    if (voice_code::is_busy(status.state)) {
        const uint32_t s = (now - status.sinceMs) / 1000;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "  %u:%02u", static_cast<unsigned>(s / 60), static_cast<unsigned>(s % 60));
        text += buf;
        if (!status.detail.empty()) {
            text += "  " + status.detail.substr(0, 16);
        }
    }
    return text;
}

}  // namespace

const lv_font_t* view::cjk_font()
{
    static lv_font_t* font = nullptr;
    static bool tried      = false;
    if (!tried) {
        tried = true;
        font  = lv_binfont_create(_font_path);
        if (font != nullptr) {
            font->fallback = &lv_font_montserrat_20;  // LVGL symbols and anything the subset lacks
        }
        mclog::tagInfo("AgentDetail", "cjk font {}", font ? "loaded" : "missing (flash storage.bin)");
    }
    return font;
}

AgentDetailPage::~AgentDetailPage()
{
    close();
}

void AgentDetailPage::open(lv_obj_t* parent, VoiceAgent agent)
{
    close();
    _agent = agent;

    _root = lv_obj_create(parent);
    style_plain(_root);
    lv_obj_set_size(_root, _screen, _screen);
    lv_obj_center(_root);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_root, lv_color_hex(_color_bg), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);

    _title = lv_label_create(_root);
    lv_obj_set_style_text_font(_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(_title, lv_color_hex(_color_title), 0);
    lv_label_set_text(_title, agent == VoiceAgent::Gpt ? "Codex" : voice_code::agent_label(agent));
    lv_obj_align(_title, LV_ALIGN_TOP_MID, 0, 44);

    _state = lv_label_create(_root);
    lv_obj_set_style_text_font(_state, &lv_font_montserrat_20, 0);
    lv_obj_align(_state, LV_ALIGN_TOP_MID, 0, 82);

    // Scrollable body: drag up/down for long replies.
    lv_obj_t* scroller = lv_obj_create(_root);
    style_plain(scroller);
    lv_obj_set_style_bg_opa(scroller, LV_OPA_TRANSP, 0);
    lv_obj_set_size(scroller, _body_w, _body_h);
    lv_obj_align(scroller, LV_ALIGN_TOP_MID, 0, _body_y);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_AUTO);

    _body = lv_label_create(scroller);
    const lv_font_t* font = cjk_font();
    lv_obj_set_style_text_font(_body, font ? font : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_body, lv_color_hex(_color_body), 0);
    lv_obj_set_style_text_line_space(_body, 4, 0);
    lv_obj_set_width(_body, _body_w);
    lv_label_set_long_mode(_body, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t* hint = lv_label_create(_root);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(_color_dim), 0);
    lv_label_set_text(hint, "B: next   A: close");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -34);

    refresh(true);
}

void AgentDetailPage::close()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root  = nullptr;
        _title = _state = _body = nullptr;
        _state_text.clear();
    }
}

void AgentDetailPage::update()
{
    if (_root != nullptr) {
        refresh(false);
    }
}

void AgentDetailPage::refresh(bool force)
{
    auto& hal          = GetHAL();
    const uint32_t now = hal.millis();
    if (!force && now - _last_refresh < 250) {
        return;
    }
    _last_refresh = now;

    const VoiceCodeStatus status = hal.voiceLinkStatus(_agent);
    const std::string line       = hal.voiceLinkConnected() ? state_line(status, now) : "Offline";
    if (force || line != _state_text) {
        _state_text = line;
        lv_label_set_text(_state, line.c_str());
        lv_obj_set_style_text_color(_state, lv_color_hex(state_color(status.state)), 0);
    }
    if (force || status.replySeq != _reply_seq || status.seq != _seq) {
        _reply_seq = status.replySeq;
        _seq       = status.seq;
        lv_label_set_text(_body, status.reply.empty() ? "No reply yet." : status.reply.c_str());
    }
}
