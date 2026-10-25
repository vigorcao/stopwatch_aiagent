/*
 * Voice Code view: a hand-drawn box mascot that acts out the Claude Code state.
 */
#pragma once
#include "voice_session_view.h"
#include <hal/hal.h>
#include <lvgl.h>
#include <array>
#include <cstdint>
#include <string>

namespace view {

class VoiceCodeView : public VoiceSessionView {
public:
    using Mood = VoiceMood;

    void init(lv_obj_t* parent) override;
    void destroy() override;

    void setMood(Mood mood, std::string_view detail = "") override;
    Mood mood() const override
    {
        return _mood;
    }
    void update(uint32_t now) override;

private:
    lv_obj_t* _screen   = nullptr;
    lv_obj_t* _root     = nullptr;  // everything that bobs / jumps together
    lv_obj_t* _body     = nullptr;
    lv_obj_t* _arm_left = nullptr;
    lv_obj_t* _arm_right = nullptr;
    std::array<lv_obj_t*, 4> _legs{};
    std::array<lv_obj_t*, 2> _eyes{};
    std::array<lv_obj_t*, 2> _blush{};
    std::array<lv_obj_t*, 10> _grass{};
    lv_obj_t* _title  = nullptr;
    lv_obj_t* _detail = nullptr;
    lv_obj_t* _hint   = nullptr;

    Mood _mood             = Mood::Idle;
    uint32_t _mood_since   = 0;
    uint32_t _next_blink   = 0;
    uint32_t _blink_until  = 0;
    float _ground_offset   = 0.0f;
    uint32_t _last_update  = 0;

    lv_obj_t* make_part(lv_obj_t* parent, int x, int y, int w, int h, int radius);
    void layout_grass(float offset);
};

}  // namespace view
