// AMEN_MINI — Player temps réel PC (J2).
// Le PC joue le rôle du Teensy : la lib audio appelle render() comme le
// callback du codec le fera. Clavier :
//   1..5  = vitesse 0.5 / 0.75 / 1.0 / 1.5 / 2.0
//   space = retrigger depuis le début
//   m     = mode du chop sélectionné
//   e     = effet affiché, [/] = intensité
//   -/+   = tempo
//   b     = browser, j/k = navigation, Entrée = charger, Retour = remonter
//   q     = quitter
// Usage : amen_rt [fichier.wav]
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "sample_catalog.h"
#include "sample_catalog_scanner.h"
#include "screen_preview.h"
#include "screen_ui.h"
#include "voice_manager.h"
#include "wav_loader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#endif

namespace {
constexpr uint32_t kOutputSampleRate = VoiceManager::kDefaultOutputSampleRate;
constexpr VoiceManager::PadId kSpacePadId = 0;
VoiceManager g_voices{kOutputSampleRate};
std::atomic<float> g_speed{1.0f};
std::atomic<bool> g_running{true};
std::mutex g_audioMutex;
const float kSpeeds[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
const char* kEffects[] = {"TRANCE GATE", "DISPERSER", "RESONATOR"};

struct UiSimulation {
    int bpm = 145;
    int effect = 0;
    int effectAmount = 5;
    PlaybackMode mode = PlaybackMode::OneShot;
};

struct AppState {
    WavData wav;
    std::filesystem::path sampleRoot;
    std::string breakName;
    SampleCatalog catalog;
    SampleCatalog::FolderId browserFolder = SampleCatalog::rootId();
    std::vector<SampleCatalog::Entry> browserEntries;
    std::size_t browserSelection = 0;
    bool browserActive = false;
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

void refresh_browser(ScreenUi& screen, AppState& state) {
    state.browserEntries = state.catalog.entries(state.browserFolder);
    if (state.browserEntries.empty()) {
        state.browserSelection = 0;
    } else if (state.browserSelection >= state.browserEntries.size()) {
        state.browserSelection = state.browserEntries.size() - 1;
    }

    std::vector<ScreenUi::BrowserLine> lines;
    lines.reserve(state.browserEntries.size());
    for (const auto& entry : state.browserEntries) {
        lines.push_back({entry.name.c_str(), entry.kind == SampleCatalog::EntryKind::Folder});
    }
    const auto folder = state.catalog.folder(state.browserFolder);
    const char* folderName = folder && !folder->relativePath.empty()
        ? folder->relativePath.c_str()
        : "ROOT";
    screen.showBrowser(folderName, lines.data(), lines.size(), state.browserSelection);
}

void load_browser_selection(ScreenUi& screen, AppState& state) {
    if (state.browserEntries.empty()) return;
    const auto& selected = state.browserEntries[state.browserSelection];
    if (selected.kind == SampleCatalog::EntryKind::Folder) {
        state.browserFolder = selected.id;
        state.browserSelection = 0;
        refresh_browser(screen, state);
        return;
    }

    const std::filesystem::path path = state.sampleRoot / selected.relativePath;
    WavData loaded = wav_load(path.string());
    if (!loaded.valid()) {
        std::fprintf(stderr, "WAV invalide : %s\n", path.string().c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_voices.stopAll();
        state.wav = std::move(loaded);
    }
    state.breakName = selected.name;
    state.browserActive = false;
    screen.showPerformance();
    std::printf("charge : %s (%u Hz natif, sortie %u Hz)\n", path.string().c_str(),
                state.wav.sampleRate, kOutputSampleRate);
}

void handle_key(int c, ScreenUi& screen, UiSimulation& simulation, AppState& state) {
    const std::uint64_t time = now_ms();
    if (c == 'q' || c == 27) {
        g_running.store(false);
    } else if (c == 'b') {
        state.browserActive = !state.browserActive;
        if (state.browserActive) {
            refresh_browser(screen, state);
        } else {
            screen.showPerformance();
        }
    } else if (state.browserActive) {
        if (c == 'j' && state.browserSelection + 1 < state.browserEntries.size()) {
            ++state.browserSelection;
            refresh_browser(screen, state);
        } else if (c == 'k' && state.browserSelection > 0) {
            --state.browserSelection;
            refresh_browser(screen, state);
        } else if (c == '\r' || c == '\n') {
            load_browser_selection(screen, state);
        } else if (c == 8 || c == 127) {
            const auto parent = state.catalog.parent(state.browserFolder);
            if (parent) {
                state.browserFolder = *parent;
                state.browserSelection = 0;
                refresh_browser(screen, state);
            }
        }
    } else if (c >= '1' && c <= '5') {
        g_speed.store(kSpeeds[c - '1']);
        screen.showParameter("SPEED", static_cast<int>(g_speed.load() * 10.0f), 0, 20, time);
        std::printf("vitesse : %.2f\n", g_speed.load());
    } else if (c == ' ') {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        const PcmView pcm = state.wav.view();
        g_voices.trigger(kSpacePadId, pcm, 0, pcm.frameCount(), g_speed.load());
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
    }
}

void audio_callback(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
    (void)dev;
    (void)in;
    float* dst = static_cast<float*>(out);
    std::fill_n(dst, static_cast<std::size_t>(frames) * 2U, 0.0f);

    // Never wait from the real-time thread while the UI swaps sample ownership.
    std::unique_lock<std::mutex> lock(g_audioMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    constexpr std::size_t kRenderBlock = 512;
    std::array<float, kRenderBlock> tmpL{};
    std::array<float, kRenderBlock> tmpR{};
    std::size_t rendered = 0;
    while (rendered < frames) {
        const std::size_t count = std::min<std::size_t>(kRenderBlock, frames - rendered);
        g_voices.render(tmpL.data(), tmpR.data(), static_cast<int>(count));
        for (std::size_t i = 0; i < count; ++i) {
            dst[(rendered + i) * 2] = tmpL[i];
            dst[(rendered + i) * 2 + 1] = tmpR[i];
        }
        rendered += count;
    }
}

#ifdef _WIN32
void key_loop(ScreenUi& screen, ScreenPreview& preview, AppState& state) {
    UiSimulation simulation;
    while (g_running.load()) {
        if (_kbhit()) {
            handle_key(_getch(), screen, simulation, state);
        }
        if (!preview.pumpEvents()) {
            g_running.store(false);
        }
        screen.setPerformance(state.breakName.c_str(), simulation.bpm, simulation.mode);
        screen.render(now_ms());
        preview.draw(screen);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
#else
#include <termios.h>
#include <unistd.h>
void key_loop(ScreenUi& screen, ScreenPreview& preview, AppState& state) {
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
            handle_key(c, screen, simulation, state);
        }
        screen.setPerformance(state.breakName.c_str(), simulation.bpm, simulation.mode);
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
    AppState state;
    state.wav = wav_load(path);
    if (!state.wav.valid()) {
        std::fprintf(stderr, "impossible de charger %s\n", path);
        return 1;
    }
    std::printf("charge : %s (%u Hz natif, %u canal, %.2f s, sortie %u Hz)\n", path,
                state.wav.sampleRate, state.wav.channels,
                static_cast<double>(state.wav.samples.size() / state.wav.channels) /
                    state.wav.sampleRate,
                kOutputSampleRate);
    const std::filesystem::path absolutePath = std::filesystem::absolute(path);
    state.sampleRoot = absolutePath.parent_path();
    state.breakName = absolutePath.filename().string();
    std::string scanError;
    if (!scanSampleDirectory(state.sampleRoot, state.catalog, scanError)) {
        std::fprintf(stderr, "scan impossible : %s\n", scanError.c_str());
    } else {
        std::printf("browser : %zu WAV dans %s\n", state.catalog.wavCount(),
                    state.sampleRoot.string().c_str());
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);

    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = kOutputSampleRate;
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

    std::printf("1-5 vitesse | espace retrigger | b browser | j/k naviguer | entree charger | retour remonter | q quitter\n");
    key_loop(screen, preview, state);

    ma_device_uninit(&device);
    return 0;
}
