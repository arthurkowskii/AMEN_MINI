// AMEN_MINI — Player temps réel PC (J2).
// Le PC joue le rôle du Teensy : la lib audio appelle render() comme le
// callback du codec le fera. Le clavier simule la face avant de la machine :
//   numpad 1-6  = pads voix : appui = joue le break et devient la cible des
//                 encodeurs tant qu'il est tenu. Maintenir + E1 = navigateur
//                 SD, + E4 = vitesse de CE pad, + E5 = mode de CE pad.
//   numpad 7-9  = pads FX : maintien = active le FX assigné
//   F1-F7       = sélectionne l'encodeur actif (E1..E7)
//   flèches     = tournent l'encodeur sélectionné
//   entrée      = clique l'encodeur sélectionné
//   espace      = retrigger du dernier pad joué (avec sa propre vitesse)
//   retour      = dossier parent dans le navigateur
//   q           = quitter
// Usage : amen_rt [fichier.wav]
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "sample_catalog.h"
#include "sample_catalog_scanner.h"
#include "screen_preview.h"
#include "screen_ui.h"
#include "fx/live_repeat.h"
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
#include <windows.h>
#include <conio.h>
#endif

namespace {
constexpr uint32_t kOutputSampleRate = VoiceManager::kDefaultOutputSampleRate;
VoiceManager g_voices{kOutputSampleRate};
constexpr std::size_t kRepeatBufferFrames =
    LiveRepeat::requiredBufferFrames(kOutputSampleRate);
std::array<float, kRepeatBufferFrames> g_repeatHistoryL{};
std::array<float, kRepeatBufferFrames> g_repeatHistoryR{};
std::array<float, kRepeatBufferFrames> g_repeatFrozenL{};
std::array<float, kRepeatBufferFrames> g_repeatFrozenR{};
LiveRepeat g_repeat{kOutputSampleRate,
                    g_repeatHistoryL.data(),
                    g_repeatHistoryR.data(),
                    g_repeatFrozenL.data(),
                    g_repeatFrozenR.data(),
                    kRepeatBufferFrames};
std::atomic<int> g_targetSpeedPercent{100};
std::atomic<int> g_targetPadId{-1};
std::atomic<int> g_repeatAmountPercent{100};
std::atomic<int> g_repeatDivision{0};
std::atomic<int> g_bpm{145};
std::atomic<bool> g_repeatActive{false};
std::atomic<bool> g_running{true};
std::mutex g_audioMutex;
constexpr int kFxCount = 3;
constexpr int kRepeatFx = 1;
const char* kFxNames[] = {"BLANK", "REPEAT", "REVERSE", "TRANCE GATE"};
const char* kDivisionNames[] = {"1/4", "1/8", "1/12", "1/16", "1/24", "1/32"};
const char* kEncoderNames[] = {"E1 NAV", "E2 AMOUNT", "E3 DIVISION", "E4 SPEED",
                               "E5 MODE", "E6 J10", "E7 BPM"};

constexpr int kVoicePadCount = 6;

// Réglages propres à chaque pad voix : la vitesse et le mode vivent ici,
// jamais dans une variable globale. La cible des encodeurs E4/E5 est le pad
// tenu (heldVoicePad), pas le dernier pad joué.
struct PadSettings {
    int speedPercent = 100;
    PlaybackMode mode = PlaybackMode::OneShot;
};

struct UiSimulation {
    int bpm = 145;
    int effectAmount = 100;
    int repeatDivision = 0;
    std::array<PadSettings, kVoicePadCount> pads{};
    std::array<int, kVoicePadCount> padPressSeq{};
    int pressSeqCounter = 0;
    int heldVoicePad = -1;
    int lastPadId = 0;
    int selectedEncoder = 1;
    int heldFxPad = -1;
    int fxCandidate = 0;
    std::array<int, 3> fxAssign{};
    std::uint16_t numpadPrev = 0;
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

// Overlay 128x32 d'un paramètre ciblant un pad précis : le nom composite
// "PAD n SPEED" indique quelle cible est éditée, en plus de la valeur.
void show_pad_overlay(ScreenUi& screen, int pad, const char* label, int value,
                      int minimum, int maximum, std::uint64_t time,
                      const char* suffix = nullptr) {
    std::array<char, 17> name{};
    std::snprintf(name.data(), name.size(), "PAD %d %s", pad + 1, label);
    screen.showParameter(name.data(), value, minimum, maximum, time, suffix);
}

// Applique la vitesse au pad donné : stocke la valeur du pad (pour son
// prochain trigger) et pose la paire atomique lue par le callback audio
// (vitesse écrite d'abord, pad ensuite) pour la rampe live sans retrigger.
void set_pad_speed_percent(int pad, int speedPercent, ScreenUi& screen,
                           UiSimulation& simulation, std::uint64_t time) {
    if (pad < 0 || pad >= kVoicePadCount) return;
    simulation.pads[pad].speedPercent = std::clamp(speedPercent, 25, 400);
    g_targetSpeedPercent.store(simulation.pads[pad].speedPercent);
    g_targetPadId.store(pad);
    show_pad_overlay(screen, pad, "SPEED", simulation.pads[pad].speedPercent, 25,
                     400, time, "%");
    std::printf("pad %d vitesse : %d%%\n", pad + 1,
                simulation.pads[pad].speedPercent);
}

const char* mode_label(PlaybackMode mode) {
    switch (mode) {
        case PlaybackMode::OneShot:
            return "ONE SHOT";
        case PlaybackMode::Loop:
            return "LOOP";
        case PlaybackMode::Granular:
            return "GRANULAR";
        case PlaybackMode::SliceSync:
            return "SLICE SYNC";
    }
    return "ONE SHOT";
}

void set_pad_mode(int pad, PlaybackMode mode, ScreenUi& screen,
                  UiSimulation& simulation, std::uint64_t time) {
    if (pad < 0 || pad >= kVoicePadCount) return;
    simulation.pads[pad].mode = mode;
    show_pad_overlay(screen, pad, "MODE", 0, 0, 1, time);
    std::printf("pad %d mode : %s\n", pad + 1, mode_label(mode));
}

RepeatDivision repeat_division(int index) {
    switch (index) {
        case 1:
            return RepeatDivision::Eighth;
        case 2:
            return RepeatDivision::EighthTriplet;
        case 3:
            return RepeatDivision::Sixteenth;
        case 4:
            return RepeatDivision::SixteenthTriplet;
        case 5:
            return RepeatDivision::ThirtySecond;
        default:
            return RepeatDivision::Quarter;
    }
}

#ifdef _WIN32
void select_encoder(int encoder, ScreenUi& screen, UiSimulation& simulation) {
    simulation.selectedEncoder = std::clamp(encoder, 1, 7);
    if (simulation.heldFxPad >= 0) {
        screen.showFxPad(7 + simulation.heldFxPad, kFxNames[simulation.fxCandidate],
                         simulation.selectedEncoder);
    } else {
        screen.showParameter(kEncoderNames[simulation.selectedEncoder - 1], 0, 0, 0,
                             now_ms());
    }
    std::printf("encodeur : %s\n", kEncoderNames[simulation.selectedEncoder - 1]);
}

void encoder_turn(int direction, ScreenUi& screen, UiSimulation& simulation,
                  AppState& state) {
    const std::uint64_t time = now_ms();
    switch (simulation.selectedEncoder) {
        case 1:
            if (simulation.heldFxPad >= 0) {
                simulation.fxCandidate =
                    (simulation.fxCandidate + direction + kFxCount + 1) %
                    (kFxCount + 1);
                screen.showFxPad(7 + simulation.heldFxPad,
                                 kFxNames[simulation.fxCandidate], 1);
                std::printf("pad %d : %s\n", 7 + simulation.heldFxPad,
                            kFxNames[simulation.fxCandidate]);
            } else if (state.browserActive) {
                if (direction > 0) {
                    if (state.browserSelection + 1 < state.browserEntries.size()) {
                        ++state.browserSelection;
                        refresh_browser(screen, state);
                    }
                } else if (state.browserSelection > 0) {
                    --state.browserSelection;
                    refresh_browser(screen, state);
                }
            } else if (simulation.heldVoicePad >= 0) {
                state.browserActive = true;
                refresh_browser(screen, state);
                std::printf("navigateur pour pad %d (relacher pour fermer)\n",
                            simulation.heldVoicePad + 1);
            } else {
                screen.showParameter("E1 TENIR PAD", 0, 0, 0, time);
            }
            break;
        case 2:
            simulation.effectAmount =
                std::clamp(simulation.effectAmount + direction * 5, 0, 100);
            g_repeatAmountPercent.store(simulation.effectAmount);
            screen.showParameter("FX AMOUNT", simulation.effectAmount, 0, 100, time, "%");
            std::printf("repeat amount : %d%%\n", simulation.effectAmount);
            break;
        case 3:
            simulation.repeatDivision =
                (simulation.repeatDivision + direction + 6) % 6;
            g_repeatDivision.store(simulation.repeatDivision);
            screen.showParameter(kDivisionNames[simulation.repeatDivision],
                                  simulation.repeatDivision, 0, 5, time);
            std::printf("repeat division : %s\n",
                        kDivisionNames[simulation.repeatDivision]);
            break;
        case 4:
            if (simulation.heldVoicePad >= 0) {
                set_pad_speed_percent(
                    simulation.heldVoicePad,
                    simulation.pads[simulation.heldVoicePad].speedPercent +
                        5 * direction,
                    screen, simulation, time);
            } else {
                screen.showParameter("E4 TENIR PAD", 0, 0, 0, time);
            }
            break;
        case 5:
            if (simulation.heldVoicePad >= 0) {
                set_pad_mode(simulation.heldVoicePad,
                             next_mode(simulation.pads[simulation.heldVoicePad].mode),
                             screen, simulation, time);
            } else {
                screen.showParameter("E5 TENIR PAD", 0, 0, 0, time);
            }
            break;
        case 6:
            screen.showParameter("E6 RESERVE J10", 0, 0, 0, time);
            break;
        case 7:
            simulation.bpm = std::clamp(simulation.bpm + direction, 20, 300);
            g_bpm.store(simulation.bpm);
            screen.showParameter("BPM", simulation.bpm, 20, 300, time);
            break;
        default:
            break;
    }
}

void encoder_click(ScreenUi& screen, UiSimulation& simulation, AppState& state) {
    const std::uint64_t time = now_ms();
    switch (simulation.selectedEncoder) {
        case 1:
            if (simulation.heldFxPad >= 0) {
                simulation.fxAssign[simulation.heldFxPad] = simulation.fxCandidate;
                g_repeatActive.store(simulation.fxCandidate == kRepeatFx);
                screen.showFxPad(7 + simulation.heldFxPad,
                                 kFxNames[simulation.fxCandidate], 1);
                std::printf("pad %d : FX = %s\n", 7 + simulation.heldFxPad,
                            kFxNames[simulation.fxCandidate]);
            } else if (state.browserActive) {
                load_browser_selection(screen, state);
            } else if (simulation.heldVoicePad >= 0) {
                state.browserActive = true;
                refresh_browser(screen, state);
                std::printf("navigateur pour pad %d (relacher pour fermer)\n",
                            simulation.heldVoicePad + 1);
            }
            break;
        case 4:
            if (simulation.heldVoicePad >= 0) {
                set_pad_speed_percent(simulation.heldVoicePad, 100, screen,
                                      simulation, time);
            } else {
                screen.showParameter("E4 TENIR PAD", 0, 0, 0, time);
            }
            break;
        case 5:
            if (simulation.heldVoicePad >= 0) {
                set_pad_mode(simulation.heldVoicePad, PlaybackMode::OneShot, screen,
                             simulation, time);
            } else {
                screen.showParameter("E5 TENIR PAD", 0, 0, 0, time);
            }
            break;
        case 6:
            screen.showParameter("E6 RESERVE J10", 0, 0, 0, time);
            break;
        default:
            break;
    }
}

void voice_pad_down(int pad, ScreenUi& screen, UiSimulation& simulation,
                    AppState& state) {
    const PcmView pcm = state.wav.view();
    const float speed =
        static_cast<float>(simulation.pads[pad].speedPercent) / 100.0f;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_voices.trigger(static_cast<VoiceManager::PadId>(pad), pcm, 0,
                         pcm.frameCount(), speed);
    }
    // Le pad appuyé devient la cible des encodeurs tant qu'il est tenu.
    // Règle "le dernier appuyé gagne" : l'ordre d'appui est mémorisé par pad.
    simulation.heldVoicePad = pad;
    simulation.padPressSeq[pad] = ++simulation.pressSeqCounter;
    simulation.lastPadId = pad;
    if (simulation.heldFxPad < 0) {
        screen.showPerformance();
    }
    std::printf(
        "pad %d : trigger (tenir + E1 = navigateur, + E4 vitesse, + E5 mode)\n",
        pad + 1);
}

// Quand le pad tenu est relâché, la cible retombe sur le dernier pad encore
// appuyé (le plus récemment pressé), sinon aucun. La fermeture du navigateur
// reste gérée par poll_numpad quand tous les pads voix sont relâchés.
void voice_pad_up(int pad, ScreenUi& screen, UiSimulation& simulation,
                  AppState& state) {
    (void)screen;
    (void)state;
    if (simulation.heldVoicePad != pad) return;
    int best = -1;
    int bestSeq = -1;
    const std::uint16_t held = simulation.numpadPrev;
    for (int p = 0; p < kVoicePadCount; ++p) {
        if (((held >> p) & 1U) != 0 && simulation.padPressSeq[p] > bestSeq) {
            bestSeq = simulation.padPressSeq[p];
            best = p;
        }
    }
    simulation.heldVoicePad = best;
}

void fx_pad_down(int pad, ScreenUi& screen, UiSimulation& simulation) {
    simulation.heldFxPad = pad;
    simulation.fxCandidate = simulation.fxAssign[pad];
    g_repeatActive.store(simulation.fxCandidate == kRepeatFx);
    screen.showFxPad(7 + pad, kFxNames[simulation.fxCandidate],
                     simulation.selectedEncoder);
    std::printf("pad %d : %s\n", 7 + pad, kFxNames[simulation.fxCandidate]);
}

void fx_pad_up(int pad, ScreenUi& screen, UiSimulation& simulation,
               AppState& state) {
    if (simulation.heldFxPad != pad) return;
    g_repeatActive.store(false);
    simulation.heldFxPad = -1;
    if (state.browserActive) {
        refresh_browser(screen, state);
    } else {
        screen.showPerformance();
    }
}

void poll_numpad(ScreenUi& screen, UiSimulation& simulation, AppState& state) {
    std::uint16_t held = 0;
    for (int pad = 0; pad < 9; ++pad) {
        if ((GetAsyncKeyState(VK_NUMPAD1 + pad) & 0x8000) != 0) {
            held |= static_cast<std::uint16_t>(1U << pad);
        }
    }
    const std::uint16_t pressed =
        static_cast<std::uint16_t>(held & ~simulation.numpadPrev);
    const std::uint16_t released =
        static_cast<std::uint16_t>(simulation.numpadPrev & ~held);
    simulation.numpadPrev = held;

    for (int pad = 0; pad < 6; ++pad) {
        if ((pressed & (1U << pad)) != 0) {
            voice_pad_down(pad, screen, simulation, state);
        }
        if ((released & (1U << pad)) != 0) {
            voice_pad_up(pad, screen, simulation, state);
        }
    }
    for (int pad = 6; pad < 9; ++pad) {
        if ((pressed & (1U << pad)) != 0) {
            fx_pad_down(pad - 6, screen, simulation);
        }
        if ((released & (1U << pad)) != 0) {
            fx_pad_up(pad - 6, screen, simulation, state);
        }
    }
    if ((released & 0x3F) != 0 && (held & 0x3F) == 0 && state.browserActive) {
        state.browserActive = false;
        if (simulation.heldFxPad >= 0) {
            screen.showFxPad(7 + simulation.heldFxPad,
                             kFxNames[simulation.fxCandidate],
                             simulation.selectedEncoder);
        } else {
            screen.showPerformance();
        }
    }
}

void handle_key(int c, ScreenUi& screen, UiSimulation& simulation, AppState& state) {
    if (c == 'q' || c == 27) {
        g_running.store(false);
    } else if (c == 0 || c == 0xE0) {
        const int code = _getch();
        if (code == 0x48) {
            encoder_turn(1, screen, simulation, state);
        } else if (code == 0x50) {
            encoder_turn(-1, screen, simulation, state);
        } else if (code >= 0x3B && code <= 0x41) {
            select_encoder(code - 0x3B + 1, screen, simulation);
        }
    } else if (c == '\r' || c == '\n') {
        encoder_click(screen, simulation, state);
    } else if (c == 8 || c == 127) {
        if (state.browserActive && simulation.heldFxPad < 0) {
            const auto parent = state.catalog.parent(state.browserFolder);
            if (parent) {
                state.browserFolder = *parent;
                state.browserSelection = 0;
                refresh_browser(screen, state);
            }
        }
    } else if (c == ' ') {
        const PcmView pcm = state.wav.view();
        const float speed =
            static_cast<float>(simulation.pads[simulation.lastPadId].speedPercent) /
            100.0f;
        {
            std::lock_guard<std::mutex> lock(g_audioMutex);
            g_voices.trigger(static_cast<VoiceManager::PadId>(simulation.lastPadId),
                             pcm, 0, pcm.frameCount(), speed);
        }
        std::printf("retrigger pad %d\n", simulation.lastPadId + 1);
    }
    // '1'-'9' ignorés : les pads numpad sont lus par poll_numpad (VK_NUMPADx).
}

void key_loop(ScreenUi& screen, ScreenPreview& preview, AppState& state) {
    UiSimulation simulation;
    while (g_running.load()) {
        while (_kbhit()) {
            handle_key(_getch(), screen, simulation, state);
        }
        poll_numpad(screen, simulation, state);
        if (!preview.pumpEvents()) {
            g_running.store(false);
        }
        screen.setPerformance(state.breakName.c_str(), simulation.bpm,
                              simulation.pads[simulation.lastPadId].mode);
        screen.render(now_ms());
        preview.draw(screen);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
#else
#include <termios.h>
#include <unistd.h>

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
    } else if (c == 'z') {
        set_pad_speed_percent(simulation.lastPadId,
                              simulation.pads[simulation.lastPadId].speedPercent - 5,
                              screen, simulation, time);
    } else if (c == 'x') {
        set_pad_speed_percent(simulation.lastPadId,
                              simulation.pads[simulation.lastPadId].speedPercent + 5,
                              screen, simulation, time);
    } else if (c == 'c') {
        set_pad_speed_percent(simulation.lastPadId, 100, screen, simulation, time);
    } else if (c == ' ') {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        const PcmView pcm = state.wav.view();
        const float speed =
            static_cast<float>(simulation.pads[simulation.lastPadId].speedPercent) /
            100.0f;
        g_voices.trigger(static_cast<VoiceManager::PadId>(simulation.lastPadId),
                         pcm, 0, pcm.frameCount(), speed);
        std::printf("retrigger pad %d\n", simulation.lastPadId + 1);
    } else if (c == 'm') {
        simulation.pads[simulation.lastPadId].mode =
            next_mode(simulation.pads[simulation.lastPadId].mode);
    } else if (c == 'e') {
        simulation.repeatDivision = (simulation.repeatDivision + 1) % 6;
        g_repeatDivision.store(simulation.repeatDivision);
        screen.showParameter(kDivisionNames[simulation.repeatDivision],
                             simulation.repeatDivision, 0, 5, time);
    } else if (c == '[') {
        simulation.effectAmount = std::max(0, simulation.effectAmount - 5);
        g_repeatAmountPercent.store(simulation.effectAmount);
        screen.showParameter("FX AMOUNT", simulation.effectAmount, 0, 100, time, "%");
    } else if (c == ']') {
        simulation.effectAmount = std::min(100, simulation.effectAmount + 5);
        g_repeatAmountPercent.store(simulation.effectAmount);
        screen.showParameter("FX AMOUNT", simulation.effectAmount, 0, 100, time, "%");
    } else if (c == '-') {
        simulation.bpm = std::max(20, simulation.bpm - 1);
        g_bpm.store(simulation.bpm);
    } else if (c == '+' || c == '=') {
        simulation.bpm = std::min(300, simulation.bpm + 1);
        g_bpm.store(simulation.bpm);
    }
}

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
        screen.setPerformance(state.breakName.c_str(), simulation.bpm,
                              simulation.pads[simulation.lastPadId].mode);
        screen.render(now_ms());
        preview.draw(screen);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}
#endif

void audio_callback(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
    (void)dev;
    (void)in;
    float* dst = static_cast<float*>(out);
    std::fill_n(dst, static_cast<std::size_t>(frames) * 2U, 0.0f);

    // Never wait from the real-time thread while the UI swaps sample ownership.
    std::unique_lock<std::mutex> lock(g_audioMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    static int appliedTargetPad = -2;
    static int appliedSpeedPercent = -1;
    static int appliedAmountPercent = -1;
    static int appliedDivision = -1;
    static int appliedBpm = -1;
    static bool appliedRepeatActive = false;
    // La cible et la vitesse voyagent ensemble : lecture pad puis vitesse
    // (l'UI écrit vitesse puis pad). Au pire, un cycle applique une valeur
    // d'un bloc avant de se corriger — imperceptible.
    const int targetPad = g_targetPadId.load();
    const int targetSpeedPercent = g_targetSpeedPercent.load();
    if (targetPad != appliedTargetPad ||
        targetSpeedPercent != appliedSpeedPercent) {
        if (targetPad >= 0) {
            const float speed = static_cast<float>(targetSpeedPercent) / 100.0f;
            g_voices.setPadSpeed(static_cast<VoiceManager::PadId>(targetPad), speed);
        }
        appliedTargetPad = targetPad;
        appliedSpeedPercent = targetSpeedPercent;
    }

    const int targetAmountPercent = g_repeatAmountPercent.load();
    if (targetAmountPercent != appliedAmountPercent) {
        g_repeat.setAmount(static_cast<float>(targetAmountPercent) / 100.0f);
        appliedAmountPercent = targetAmountPercent;
    }
    const int targetDivision = g_repeatDivision.load();
    if (targetDivision != appliedDivision) {
        g_repeat.setDivision(repeat_division(targetDivision));
        appliedDivision = targetDivision;
    }
    const int targetBpm = g_bpm.load();
    if (targetBpm != appliedBpm) {
        g_repeat.setBpm(static_cast<float>(targetBpm));
        appliedBpm = targetBpm;
    }
    const bool targetRepeatActive = g_repeatActive.load();
    if (targetRepeatActive != appliedRepeatActive) {
        g_repeat.setActive(targetRepeatActive);
        appliedRepeatActive = targetRepeatActive;
    }

    constexpr std::size_t kRenderBlock = 512;
    std::array<float, kRenderBlock> tmpL{};
    std::array<float, kRenderBlock> tmpR{};
    std::size_t rendered = 0;
    while (rendered < frames) {
        const std::size_t count = std::min<std::size_t>(kRenderBlock, frames - rendered);
        g_voices.render(tmpL.data(), tmpR.data(), static_cast<int>(count));
        g_repeat.process(tmpL.data(), tmpR.data(), static_cast<int>(count));
        for (std::size_t i = 0; i < count; ++i) {
            dst[(rendered + i) * 2] = tmpL[i];
            dst[(rendered + i) * 2 + 1] = tmpR[i];
        }
        rendered += count;
    }
}
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

    std::printf("numpad 1-6 : pads voix (appui = trigger + cible ; tenir + E1 = navigateur, + E4 vitesse, + E5 mode)\n");
    std::printf("numpad 7-9 : pads FX (maintien = activation) | F1-F7 : encodeurs | E2 amount | E3 division | E7 BPM | entree : clic | q : quitter\n");
    key_loop(screen, preview, state);

    ma_device_uninit(&device);
    return 0;
}
