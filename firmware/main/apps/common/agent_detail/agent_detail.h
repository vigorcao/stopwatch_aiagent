/*
 * Full-screen details for one agent: its state, how long it has been busy and
 * its latest reply (Chinese included), so you can read what Claude / Codex said
 * without walking back to the Mac. Drag to scroll long replies.
 */
#pragma once
#include <hal/hal.h>
#include <lvgl.h>
#include <string>

namespace view {

class AgentDetailPage {
public:
    ~AgentDetailPage();

    void open(lv_obj_t* parent, VoiceAgent agent);
    void close();
    bool isOpen() const
    {
        return _root != nullptr;
    }
    VoiceAgent agent() const
    {
        return _agent;
    }
    // Refreshes text when the agent's state or reply changes; call every frame.
    void update();

private:
    lv_obj_t* _root   = nullptr;
    lv_obj_t* _title  = nullptr;
    lv_obj_t* _state  = nullptr;
    lv_obj_t* _body   = nullptr;
    VoiceAgent _agent = VoiceAgent::Claude;
    uint32_t _seq     = 0;
    uint32_t _reply_seq    = 0;
    uint32_t _last_refresh = 0;
    std::string _state_text;

    void refresh(bool force);
};

// The CJK font in the file partition, loaded once (nullptr if missing).
const lv_font_t* cjk_font();

}  // namespace view
