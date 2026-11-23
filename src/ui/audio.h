#pragma once
// Music, retro sound effects and the sound button. Everything is a no-op when there is no
// audio device (tests, headless runs), so callers never need to check.
#include "ui.h"

#include <string>

namespace audio {

enum class Sfx { Hit, Coin, Flap, Brick, Shoot, Boom, Miss, Click };

void init(const std::string &assets_dir, const std::string &settings_file);
void shutdown();
void update();                              // once per frame: keeps the music streaming

void music(const std::string &name);        // "lobbyMusic", "bossMusic1", ... "" = silence; same name keeps playing
void play(const std::string &name);         // one-shot clip: "maxonTransition", "maxonWins"
void sfx(Sfx s);                            // a generated 8-bit beep

// The speaker button: click opens a small panel with mute, music and effects volume.
void button(Rectangle r);
void draw_panel();                          // call once per frame, after the screens, so it is on top
bool panel_open();
bool mouse_over_panel();                    // screens ignore the mouse while it is over the panel

} // namespace audio
