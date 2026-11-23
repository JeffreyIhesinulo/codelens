// Music, 8-bit effects and the sound panel.
#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <vector>

namespace audio {
namespace {

bool ready = false;
std::string dir, settings;
int music_vol = 45, sfx_vol = 70;   // 0..100
bool muted = false;
bool open_panel = false;
Rectangle anchor{};

Music current{};
bool has_music = false;
std::string current_name;
std::map<std::string, Sound> clips;
Sound beeps[8];

void save() {
    if (settings.empty()) return;
    std::ofstream(settings) << "muted " << (muted ? 1 : 0) << "\nmusic " << music_vol << "\neffects " << sfx_vol << "\n";
}

void apply_volume() {
    if (!ready) return;
    float m = muted ? 0 : music_vol / 100.0f, e = muted ? 0 : sfx_vol / 100.0f;
    if (has_music) SetMusicVolume(current, m);
    for (auto &c : clips) SetSoundVolume(c.second, e);
    for (Sound &b : beeps) SetSoundVolume(b, e * 0.55f);
}

// A tiny synthesizer: a square wave sliding from f0 to f1 (or noise), with a fast decay.
Sound make(float f0, float f1, float seconds, bool noise = false, float duty = 0.5f, float f_second = 0) {
    const int rate = 22050;
    int n = (int)(rate * seconds);
    std::vector<short> data(n);
    float phase = 0;
    short held = 0;
    for (int i = 0; i < n; ++i) {
        float t = (float)i / n;
        float f = f_second > 0 && t > 0.3f ? f_second : f0 + (f1 - f0) * t;
        phase += f / rate;
        phase -= std::floor(phase);
        float env = (1 - t) * (1 - t);
        float v;
        if (noise) {
            if (i % 6 == 0) held = (short)(std::rand() % 20000 - 10000);
            v = held / 10000.0f;
        } else
            v = phase < duty ? 1.0f : -1.0f;
        data[i] = (short)(v * env * 9000);
    }
    Wave w{};
    w.frameCount = (unsigned)n;
    w.sampleRate = rate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data.data();
    return LoadSoundFromWave(w);   // copies the samples
}

} // namespace

void init(const std::string &assets_dir, const std::string &settings_file) {
    dir = assets_dir + "/sounds/";
    settings = settings_file;
    std::ifstream in(settings);
    for (std::string k; in >> k;) {
        int v = 0;
        in >> v;
        if (k == "muted") muted = v != 0;
        if (k == "music") music_vol = v;
        if (k == "effects") sfx_vol = v;
    }
    InitAudioDevice();
    ready = IsAudioDeviceReady();
    if (!ready) return;
    for (const char *name : {"maxonTransition", "maxonWins"}) {
        std::string path = dir + name + ".mp3";
        if (FileExists(path.c_str())) clips[name] = LoadSound(path.c_str());
    }
    beeps[(int)Sfx::Hit] = make(440, 660, 0.07f);
    beeps[(int)Sfx::Coin] = make(988, 988, 0.18f, false, 0.5f, 1319);
    beeps[(int)Sfx::Flap] = make(300, 620, 0.08f, false, 0.25f);
    beeps[(int)Sfx::Brick] = make(720, 480, 0.08f);
    beeps[(int)Sfx::Shoot] = make(1250, 260, 0.13f, false, 0.25f);
    beeps[(int)Sfx::Boom] = make(0, 0, 0.32f, true);
    beeps[(int)Sfx::Miss] = make(240, 90, 0.35f);
    beeps[(int)Sfx::Click] = make(1000, 1000, 0.025f);
    apply_volume();
}

void shutdown() {
    if (!ready) return;
    if (has_music) UnloadMusicStream(current);
    for (auto &c : clips) UnloadSound(c.second);
    for (Sound &b : beeps) UnloadSound(b);
    CloseAudioDevice();
    ready = false;
}

void update() {
    if (ready && has_music) UpdateMusicStream(current);
}

void music(const std::string &name) {
    if (!ready || name == current_name) return;
    if (has_music) {
        StopMusicStream(current);
        UnloadMusicStream(current);
        has_music = false;
    }
    current_name = name;
    if (name.empty()) return;
    std::string path = dir + name + ".mp3";
    if (!FileExists(path.c_str())) return;
    current = LoadMusicStream(path.c_str());
    current.looping = true;
    has_music = true;
    PlayMusicStream(current);
    apply_volume();
}

void play(const std::string &name) {
    auto it = clips.find(name);
    if (ready && it != clips.end()) PlaySound(it->second);
}

void sfx(Sfx s) {
    if (ready) PlaySound(beeps[(int)s]);
}

bool panel_open() { return open_panel; }

void button(Rectangle r) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    if (hover) ui_cursor_hand();
    DrawRectangleRounded(r, 1, 8, hover || open_panel ? theme::panel_hi : theme::accent_soft);
    DrawRectangleRoundedLinesEx(r, 1, 8, 1, open_panel ? theme::accent : theme::accent_dk);
    // speaker icon
    float cx = r.x + r.width / 2 - 4, cy = r.y + r.height / 2, s = r.height * 0.18f;
    Color c = muted ? theme::text_dim : theme::text;
    DrawRectangle((int)(cx - 2.2f * s), (int)(cy - s), (int)(1.4f * s), (int)(2 * s), c);
    DrawTriangle({cx - 0.8f * s, cy - s}, {cx - 0.8f * s, cy + s}, {cx + 1.2f * s, cy + 2.2f * s}, c);
    DrawTriangle({cx - 0.8f * s, cy - s}, {cx + 1.2f * s, cy + 2.2f * s}, {cx + 1.2f * s, cy - 2.2f * s}, c);
    if (muted) {
        DrawLineEx({cx + 2 * s, cy - s}, {cx + 4 * s, cy + s}, 2, theme::danger);
        DrawLineEx({cx + 4 * s, cy - s}, {cx + 2 * s, cy + s}, 2, theme::danger);
    } else {
        DrawRing({cx + 1.2f * s, cy}, 1.6f * s, 1.6f * s + 1.6f, -50, 50, 12, c);
        if (music_vol > 50 || sfx_vol > 50) DrawRing({cx + 1.2f * s, cy}, 2.6f * s, 2.6f * s + 1.6f, -50, 50, 12, c);
    }
    if (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        open_panel = !open_panel;
        anchor = r;
    }
}

Rectangle panel_rect() {
    float w = 300, h = 196;
    float x = std::min(anchor.x, GetScreenWidth() - w - 12.0f), y = anchor.y + anchor.height + 8;
    if (y + h > GetScreenHeight() - 8) y = anchor.y - h - 8;   // open upwards near the bottom
    return {x, y, w, h};
}

bool mouse_over_panel() { return open_panel && CheckCollisionPointRec(GetMousePosition(), panel_rect()); }

void draw_panel() {
    if (!open_panel) return;
    Rectangle p = panel_rect();
    float w = p.width, h = p.height;
    DrawRectangleRounded({p.x, p.y + 4, p.width, p.height}, 0.1f, 8, {0, 0, 0, 90});
    DrawRectangleRounded(p, 0.1f, 8, theme::panel);
    DrawRectangleRoundedLinesEx(p, 0.1f, 8, 1, theme::accent_dk);
    draw_text(g_fonts.ui_bold, 16, "Sound", p.x + 16, p.y + 14, theme::text);
    if (button({p.x + w - 112, p.y + 10, 96, 30}, muted ? "Turn on" : "Mute", true,
               muted ? ButtonStyle::Primary : ButtonStyle::Normal)) {
        muted = !muted;
        apply_volume();
        save();
    }
    int m = music_vol, e = sfx_vol;
    slider({p.x + 16, p.y + 56, w - 32, 37}, "Music", m, 0, 100);
    slider({p.x + 16, p.y + 112, w - 32, 37}, "Effects", e, 0, 100);
    if (m != music_vol || e != sfx_vol) {
        bool effects_changed = e != sfx_vol;
        music_vol = m;
        sfx_vol = e;
        apply_volume();
        if (effects_changed && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) sfx(Sfx::Coin);
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) save();
    draw_text(g_fonts.ui, 13, "Settings are saved for next time.", p.x + 16, p.y + h - 26, theme::text_dim);
    // click outside closes the panel
    Vector2 mp = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mp, p) && !CheckCollisionPointRec(mp, anchor))
        open_panel = false;
    if (IsKeyPressed(KEY_ESCAPE)) open_panel = false;
}

} // namespace audio
