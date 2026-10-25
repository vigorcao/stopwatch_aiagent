/*
 * Voice Code session: hold A to dictate to a coding agent on the Mac (the Mac
 * helper records, transcribes and types it in), and watch a mascot act out
 * what the agent is doing (states pushed by agent hooks over the BLE link).
 *
 * One class serves every session; Config picks the name, icon, agent and view:
 * the hand-drawn box for Claude Code, 黑豆 (a Codex pet) for ChatGPT / Codex.
 */
#pragma once
#include "view/voice_session_view.h"
#include <apps/common/key_manager/key_manager.h>
#include <apps/common/agent_detail/agent_detail.h>
#include <apps/common/arc_top_clock/arc_top_clock.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AppVoiceCode : public mooncake::AppAbility {
public:
    struct Config {
        std::string name;
        const void* icon;
        VoiceAgent agent;
        std::function<std::unique_ptr<view::VoiceSessionView>()> makeView;
        std::vector<VoiceAgent> agents = {};  // hold B cycles through these (starting at agent)
    };

    explicit AppVoiceCode(Config config);

    static Config claude();
    static Config gpt();
    static Config kurotamago();
    static Config meiqiu();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    Config _config;
    VoiceAgent _agent;  // starts at _config.agent, kept across opens
    std::unique_ptr<view::VoiceSessionView> _view;
    std::unique_ptr<input::KeyManager> _key_manager;
    bool _talking            = false;
    view::AgentDetailPage _detail;  // B toggles the agent's latest reply
    std::unique_ptr<view::ArcTopClock> _clock;  // time along the top edge
    lv_obj_t* _battery          = nullptr;      // battery level at the bottom
    uint32_t _battery_refresh   = 0;
    void update_battery(bool force);
    uint32_t _sending_since  = 0;
    uint32_t _done_since     = 0;
    uint32_t _status_seq     = 0;
    VoiceCodeStatus _status;

    void apply_status(bool alert);
    bool switchable() const
    {
        return _config.agents.size() > 1;
    }
    VoiceAgent next_agent() const;
    void switch_agent();
};
