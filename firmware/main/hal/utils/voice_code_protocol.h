/*
 * Voice Code status protocol, shared by the device and web HALs.
 *
 * The Mac writes UTF-8 "<state>\t<detail>[\t<agent>[\t<elapsed s>]]" to the
 * status characteristic, e.g. "working\tBash" or "done\t\tgpt". Agent is
 * "claude" (default), "gpt" or "openclaw". Unknown states map to Idle. The Mac helper adds
 * how long the agent has been busy and repeats the status every 10 s, so the
 * watch can show a timer and notice when the helper goes away.
 * "reply\t<text>\t<agent>" carries the agent's latest reply (UTF-8, newlines
 * kept, up to ~500 bytes) for the details page; it doesn't change the state.
 * "time\t<unix seconds>\t<POSIX TZ>" (e.g. "<+08>-8") sets the watch clock;
 * the Mac helper sends it on every (re)connect.
 * Talk notifications from the watch are "<event>\t<agent>[\twatch]", event
 * being down / up / cancel. A "watch" third field on "down" means the watch
 * mic streams this utterance on the audio characteristic until "up"/"cancel".
 *
 * Audio notifications: 16 kHz mono IMA-ADPCM, each packet self-contained:
 *   [seq u16 LE][predictor i16 LE][step index u8][4-bit codes, low nibble first]
 */
#pragma once
#include "hal/hal.h"
#include <array>
#include <string>
#include <string_view>

namespace voice_code {

// 128-bit UUIDs, little-endian byte order for NimBLE BLE_UUID128_INIT.
// Service:        7c5a0001-5e0c-4e7a-9b1d-c1a0de00c0de
// Status (write): 7c5a0002-5e0c-4e7a-9b1d-c1a0de00c0de
// Talk (notify):  7c5a0003-5e0c-4e7a-9b1d-c1a0de00c0de
// Audio (notify): 7c5a0004-5e0c-4e7a-9b1d-c1a0de00c0de
#define VOICE_CODE_UUID128(n) \
    0xde, 0xc0, 0x00, 0xde, 0xa0, 0xc1, 0x1d, 0x9b, 0x7a, 0x4e, 0x0c, 0x5e, (n), 0x00, 0x5a, 0x7c

inline constexpr const char* DeviceName = "M5 Voice Code";

inline VoiceCodeState parse_state(std::string_view name)
{
    if (name == "thinking") return VoiceCodeState::Thinking;
    if (name == "working") return VoiceCodeState::Working;
    if (name == "permission") return VoiceCodeState::Permission;
    if (name == "done") return VoiceCodeState::Done;
    if (name == "error") return VoiceCodeState::Error;
    return VoiceCodeState::Idle;
}

inline constexpr size_t AgentCount = 3;

inline const char* agent_name(VoiceAgent agent)
{
    switch (agent) {
        case VoiceAgent::Gpt: return "gpt";
        case VoiceAgent::OpenClaw: return "openclaw";
        default: return "claude";
    }
}

// Shown on the watch: badges, hints and the details page title.
inline const char* agent_label(VoiceAgent agent)
{
    switch (agent) {
        case VoiceAgent::Gpt: return "GPT";
        case VoiceAgent::OpenClaw: return "OpenClaw";
        default: return "Claude";
    }
}

// Slot in the per-agent status array.
inline size_t agent_index(VoiceAgent agent)
{
    return static_cast<size_t>(agent) < AgentCount ? static_cast<size_t>(agent) : 0;
}

inline size_t agent_index(std::string_view name)
{
    if (name == "gpt") return agent_index(VoiceAgent::Gpt);
    if (name == "openclaw") return agent_index(VoiceAgent::OpenClaw);
    return agent_index(VoiceAgent::Claude);
}

inline bool is_busy(VoiceCodeState state)
{
    return state == VoiceCodeState::Thinking || state == VoiceCodeState::Working ||
           state == VoiceCodeState::Permission;
}

// Splits off the next tab-separated field.
inline std::string_view next_field(std::string_view& rest)
{
    const auto tab         = rest.find('\t');
    const std::string_view field = rest.substr(0, tab);
    rest                   = tab == std::string_view::npos ? std::string_view() : rest.substr(tab + 1);
    return field;
}

// Applies a status write to the matching agent's slot.
inline void apply_payload(std::array<VoiceCodeStatus, AgentCount>& statuses, std::string_view payload, uint32_t nowMs)
{
    std::string_view rest   = payload;
    const auto state_name   = next_field(rest);
    const auto detail       = next_field(rest);
    const auto agent        = next_field(rest);
    const auto elapsed      = next_field(rest);
    auto& status            = statuses[agent_index(agent)];
    if (state_name == "reply") {
        status.reply = std::string(detail);
        status.replySeq++;
        return;
    }
    const auto state        = parse_state(state_name);

    status.heartbeat = !elapsed.empty();
    if (status.heartbeat) {
        uint32_t seconds = 0;
        for (char c : elapsed) {
            if (c < '0' || c > '9') break;
            seconds = seconds * 10 + (c - '0');
        }
        status.sinceMs = nowMs - seconds * 1000;
    } else if (is_busy(state) && !is_busy(status.state)) {
        status.sinceMs = nowMs;  // hooks talking to the watch directly: time it here
    }
    status.updatedMs = nowMs;

    // Heartbeats repeat the same state; only real changes bump seq (apps alert on seq).
    if (state != status.state || detail != status.detail || status.seq == 0) {
        status.state  = state;
        status.detail = std::string(detail.substr(0, 48));
        status.seq++;
    }
}

}  // namespace voice_code
