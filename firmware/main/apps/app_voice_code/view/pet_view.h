/*
 * Pet view: plays a Codex v2 pet (e.g. 黑豆 / heidou) from frames stored on the
 * watch's FAT partition (/spiflash/pets/<id>/<state>_<i>.bin, made by
 * tools/make_pet_frames.py). Idle pets face front when flat or upright, look
 * toward the low side when leaned, and jump when shaken.
 *
 * Peek pets (peek = true) sit big on the bottom edge, cut off by the bezel like
 * they lean on the glass from below. On open they rise from below the screen:
 * eyes first, a short pause, then a pop up. Their frames must be 192x208
 * (make_pet_frames.py scale 1.0).
 *
 * Walk-in pets (walkIn = true) walk in from the left edge on open, playing the
 * walk_<i> frames (Codex v2 running-right row), and stop in the centre.
 */
#pragma once
#include "voice_session_view.h"
#include <lvgl.h>
#include <cstdint>
#include <string>

namespace view {

class PetView : public VoiceSessionView {
public:
    // bg must match the colour the frames were flattened onto (make_pet_frames.py --bg).
    explicit PetView(std::string petId, bool peek = false, uint32_t bg = 0xF0ECE2, bool walkIn = false)
        : _pet_id(std::move(petId)), _peek(peek), _walk_in(walkIn), _bg(bg)
    {
    }

    void init(lv_obj_t* parent) override;
    void destroy() override;
    void setMood(VoiceMood mood, std::string_view detail = "") override;
    VoiceMood mood() const override
    {
        return _mood;
    }
    void update(uint32_t now) override;
    void setAgent(VoiceAgent agent, bool switchable, VoiceAgent next) override;
    bool coversBottom() const override
    {
        return _peek;
    }

    struct Clip {
        const char* state;
        uint8_t frames;
        uint16_t frame_ms;
        uint16_t last_ms;  // hold on the final frame
        bool loop;
    };

private:
    std::string _pet_id;
    bool _peek;
    bool _walk_in;
    uint32_t _bg;
    lv_obj_t* _screen = nullptr;
    lv_obj_t* _image  = nullptr;
    lv_obj_t* _title  = nullptr;
    lv_obj_t* _detail = nullptr;
    lv_obj_t* _hint   = nullptr;
    lv_obj_t* _badge  = nullptr;  // which agent a switchable pet talks to

    VoiceAgent _agent = VoiceAgent::Gpt;
    VoiceAgent _next  = VoiceAgent::Claude;
    bool _switchable  = false;

    VoiceMood _mood        = VoiceMood::Idle;
    const Clip* _clip      = nullptr;
    int _frame             = 0;
    uint32_t _frame_until  = 0;
    int _look              = -1;  // current look direction frame, -1 = none
    uint32_t _enter_start  = 0;   // rise-in / walk-in start time, 0 = not started
    bool _entering         = false;
    std::string _src;
    std::string _detail_text;  // last detail from the app (the label may show the hint)

    void play(const Clip* clip);
    void show(const std::string& file);
    bool update_rise_in(uint32_t now);
    bool update_walk_in(uint32_t now);
};

}  // namespace view
