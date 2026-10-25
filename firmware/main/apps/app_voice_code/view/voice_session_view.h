/*
 * What a Voice Code session screen must provide: the app controller drives the
 * mood, the view decides how a mascot acts it out.
 */
#pragma once
#include <hal/hal.h>
#include <lvgl.h>
#include <cstdint>
#include <string_view>

namespace view {

enum class VoiceMood : uint8_t {
    Idle,
    Listening,  // A held, recording on the Mac
    Sending,    // A released, transcribing / waiting for the agent
    Thinking,
    Working,
    Permission,
    Done,
    Error,
    Offline,  // Mac not connected over BLE
};

class VoiceSessionView {
public:
    virtual ~VoiceSessionView() = default;

    virtual void init(lv_obj_t* parent)                                   = 0;
    virtual void destroy()                                                = 0;
    virtual void setMood(VoiceMood mood, std::string_view detail = "")   = 0;
    virtual VoiceMood mood() const                                        = 0;
    virtual void update(uint32_t now)                                     = 0;
    // next: where holding B goes, when the session can switch agents.
    virtual void setAgent(VoiceAgent agent, bool switchable, VoiceAgent next)
    {
    }
    // True when the mascot fills the bottom edge, so the app moves its overlays up.
    virtual bool coversBottom() const
    {
        return false;
    }
};

}  // namespace view
