// AMEN_MINI — Player temps réel PC (J2).
// Le PC joue le rôle du Teensy : la lib audio appelle render() comme le
// callback du codec le fera. Clavier :
//   1..5  = vitesse 0.5 / 0.75 / 1.0 / 1.5 / 2.0
//   space = retrigger depuis le début
//   m     = mode du chop sélectionné
//   e     = effet affiché, [/] = intensité
//   -/+   = tempo
//   q     = quitter
// Usage : amen_rt [fichier.wav]
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "sample_player.h"
#include "screen_preview.h"
#include "screen_ui.h"
#include "wav_loader.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#endif

namespace {
SamplePlayer g_player;
std::atomic<float> g_speed{1.0f};
std::atomic<bool> g_running{true};
const float kSpeeds[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
const char* kEffects[] = {"TRANCE GATE", "DISPERSER", "RESONATOR"};

struct UiSimulation {
    int bpm = 145;
    int effect = 0;
    int effectAmount = 5;
    PlaybackMode mode = PlaybackMode::OneShot;
};

std::uint64_t now_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

PlaybackMode next_mode(PlaybackMode mode) {
    switch (mode) {
        case PlaybackMode::OneShot:
            return PlaybackMode::Loop;
        case PlaybackMode::Loop:
            return PlaybackMode::Granular;
        case PlaybackMode::Granular:
            return PlaybackMode::SliceSync;
        case PlaybackMode::SliceSync:
            return PlaybackMode::OneShot;
    }
    return PlaybackMode::OneShot;
}

void handle_key(int c, ScreenUi& screen, UiSimulation& simulation) {
    const std::uint64_t time = now_ms();
    if (c >= '1' && c <= '5') {
        g_speed.store(kSpeeds[c - '1']);
        screen.showParameter("SPEED", static_cast<int>(g_speed.load() * 10.0f), 0, 20, time);
        std::printf("vitesse : %.2f\n", g_speed.load());
    } else if (c == ' ') {
        g_player.trigger();
        std::printf("retrigger\n");
    } else if (c == 'm') {
        simulation.mode = next_mode(simulation.mode);
    } else if (c == 'e') {
        simulation.effect = (simulation.effect + 1) % 3;
        screen.showParameter(kEffects[simulation.effect], simulation.effectAmount, 0, 10, time);
    } else if (c == '[') {
        simulation.effectAmount = std::max(0, simulation.effectAmount - 1);
        screen.showParameter(kEffects[simulation.effect], simulation.effectAmount, 0, 10, time);
    } else if (c == ']') {
        simulation.effectAmount = std::min(10, simulation.effectAmount + 1);
        screen.showParameter(kEffects[simulation.effect], simulation.effectAmount, 0, 10, time);
    } else if (c == '-') {
        simulation.bpm = std::max(20, simulation.bpm - 1);
    } else if (c == '+' || c == '=') {
        simulation.bpm = std::min(300, simulation.bpm + 1);
    } else if (c == 'q' || c == 27) {
        g_running.store(false);
    }
}

void audio_callback(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
    (void)dev;
    (void)in;
    float* dst = static_cast<float*>(out);
    static std::vector<float> tmpL(4096), tmpR(4096);
    if (tmpL.size() < frames) {
        tmpL.resize(frames);
        tmpR.resize(frames);
    }
    g_player.setSpeed(g_speed.load());
    g_player.render(tmpL.data(), tmpR.data(), static_cast<int>(frames));
    for (ma_uint32 i = 0; i < frames; ++i) {
        dst[i * 2] = tmpL[i];
        dst[i * 2 + 1] = tmpR[i];
    }
}

#ifdef _WIN32
void key_loop(ScreenUi& screen, ScreenPreview& preview, const char* breakName) {
    UiSimulation simulation;
    while (g_running.load()) {
        if (_kbhit()) {
            handle_key(_getch(), screen, simulation);
        }
        if (!preview.pumpEvents()) {
            g_running.store(false);
        }
        screen.setPerformance(breakName, simulation.bpm, simulation.mode);
        screen.render(now_ms());
        preview.draw(screen);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
#else
#include <termios.h>
#include <unistd.h>
void key_loop(ScreenUi& screen, ScreenPreview& preview, const char* breakName) {
    UiSimulation simulation;
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    newt.c_cc[VMIN] = 0;
    newt.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    while (g_running.load()) {
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) == 1) {
            handle_key(c, screen, simulation);
        }
        screen.setPerformance(breakName, simulation.bpm, simulation.mode);
        screen.render(now_ms());
        preview.draw(screen);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}
#endif
}  // namespace

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "test_native/test_wavs/t16.wav";
    WavData wav = wav_load(path);
    if (!wav.valid()) {
        std::fprintf(stderr, "impossible de charger %s\n", path);
        return 1;
    }
    std::printf("charge : %s (%u Hz, %u canal, %.2f s)\n", path, wav.sampleRate,
                wav.channels, static_cast<double>(wav.samples.size() / wav.channels) / wav.sampleRate);
    g_player.setSample(wav);

    const char* breakName = path;
    for (const char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            breakName = cursor + 1;
        }
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);

    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = wav.sampleRate;
    cfg.dataCallback = audio_callback;
    ma_device device;
    if (ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS) {
        std::fprintf(stderr, "pas de peripherique audio\n");
        return 1;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        std::fprintf(stderr, "impossible de demarrer l'audio\n");
        ma_device_uninit(&device);
        return 1;
    }

    ScreenUi screen;
    ScreenPreview preview;
    if (!preview.open()) {
        std::fprintf(stderr, "impossible d'ouvrir l'apercu OLED\n");
    }

    std::printf("1-5 vitesse | espace retrigger | m mode | e effet | [/] intensite | -/+ BPM | q quitter\n");
    key_loop(screen, preview, breakName);

    ma_device_uninit(&device);
    return 0;
}
