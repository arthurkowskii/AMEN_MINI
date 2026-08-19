// AMEN_MINI — Player temps réel PC (J2).
// Le PC joue le rôle du Teensy : la lib audio appelle render() comme le
// callback du codec le fera. Le clavier simule la face avant de la machine :
//   numpad 1-6  = pads voix : appui = joue le break et devient la cible des
//                 encodeurs tant qu'il est tenu. Maintenir + E1 = navigateur
//                 SD, + E4 = vitesse de CE pad, + E5 = lecture ONE SHOT/LOOP
//                 (rotation) et comportement GATE/LATCH (clic).
//   numpad 7-9  = pads FX : maintien = active le FX assigné
//   F1-F7       = sélectionne l'encodeur actif (E1..E7)
//   flèches     = tournent l'encodeur sélectionné
//   entrée      = clique l'encodeur sélectionné
//   espace      = simule l'appui du dernier pad (LOOP+LATCH bascule)
//   retour      = dossier parent dans le navigateur
//   q           = quitter
// Usage : amen_rt [fichier.wav]
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "assignment_session.h"
#include "browser_interaction.h"
#include "capture_buffer.h"
#include "granular.h"
#include "pad_assignment.h"
#include "pad_trigger_logic.h"
#include "sample_catalog.h"
#include "sample_catalog_scanner.h"
#include "screen_preview.h"
#include "screen_ui.h"
#include "transient_detector.h"
#include "fx/live_repeat.h"
#include "fx/phase_distortion.h"
#include "fx/reverse_player.h"
#include "fx/spectral_freeze.h"
#include "fx/spectral_gate.h"
#include "pad_recorder.h"
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

constexpr int kVoicePadCount = 6;

// Nuage granulaire par pad voix (mode CLOUD, plan 7.3). Le PCM est emprunte
// (jamais copie) : il vit dans state.wav ou dans la session TRANSIENT. Tout
// chemin qui detruit/remplace ces buffers DOIT arreter les nuages avant
// (voir HarnessVoiceStopper et le chargement du navigateur).
std::array<GrainCloud, kVoicePadCount> g_padClouds{};

// Capture retrospective COMMIT (plan 8) : anneau stereo de 15 s, stockage
// statique fourni par l'appelant (futur : PSRAM Teensy), rempli par le
// callback audio apres la chaine d'effets.
constexpr std::size_t kCaptureFrames =
    CaptureBuffer::requiredBufferFrames(kOutputSampleRate,
                                        CaptureBuffer::kDefaultWindowSeconds);
std::array<float, kCaptureFrames> g_captureStorageL{};
std::array<float, kCaptureFrames> g_captureStorageR{};
CaptureBuffer g_captureBuffer{kOutputSampleRate, g_captureStorageL.data(),
                              g_captureStorageR.data(), kCaptureFrames};
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
std::atomic<int> g_repeatAmountPercent{100};
std::atomic<int> g_repeatDivision{0};
std::atomic<int> g_bpm{145};
std::atomic<bool> g_repeatActive{false};
std::atomic<int> g_repeatMode{0};  // 0 = LOOP, 1 = SHEPARD
std::atomic<bool> g_gateActive{false};
SpectralGate g_spectralGate{kOutputSampleRate};
std::atomic<bool> g_freezeActive{false};
SpectralFreeze g_spectralFreeze{kOutputSampleRate};
// REVERSE (pad FX) : boucle inversee de la fenetre precedant l'appui (2 s).
constexpr std::size_t kReverseBufferFrames =
    ReversePlayer::requiredBufferFrames(kOutputSampleRate);
std::array<float, kReverseBufferFrames> g_reverseHistoryL{};
std::array<float, kReverseBufferFrames> g_reverseHistoryR{};
std::array<float, kReverseBufferFrames> g_reverseFrozenL{};
std::array<float, kReverseBufferFrames> g_reverseFrozenR{};
ReversePlayer g_reverse{kOutputSampleRate,
                        g_reverseHistoryL.data(),
                        g_reverseHistoryR.data(),
                        g_reverseFrozenL.data(),
                        g_reverseFrozenR.data(),
                        kReverseBufferFrames};
std::atomic<bool> g_reverseActive{false};
// PHASE DIST (pad FX) : allpass a coefficient variable, sans buffer.
PhaseDistortion g_phase{kOutputSampleRate};
std::atomic<bool> g_phaseActive{false};
std::atomic<int> g_phaseMode{0};  // index PhaseDistMode
// Enregistrement direct par pad (J15, Shift + pad maintenu) : source = mix
// post-FX, 6 s par pad, matiere persistante (seul un re-arm efface).
constexpr std::size_t kPadRecordSeconds = 6;
constexpr std::size_t kPadRecordFrames = kPadRecordSeconds * kOutputSampleRate;
std::array<std::int16_t,
           PadRecorder::requiredSamples(kVoicePadCount, kPadRecordFrames)>
    g_padRecordStorage{};
PadRecorder g_padRecorder{kVoicePadCount, g_padRecordStorage.data(),
                          kPadRecordFrames};
std::atomic<bool> g_running{true};
std::mutex g_audioMutex;

// --- Assignation atomique TRANSIENT (plan, tache 6B) ---
// Une seule session possede le plan de douze plages publie et son break.
// Le harness adapte VoiceManager au contrat VoiceStopper par ce wrapper
// minimal : la session appelle stopAll() juste avant de publier un nouveau
// plan, pour qu'aucune voix ne rende des plages obsoletes contre le nouveau
// buffer. Le verrou est celui de toutes les commandes du chemin de controle
// (le callback audio le tente sans attendre).
class HarnessVoiceStopper final : public VoiceStopper {
public:
    void stopAll() override {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_voices.stopAll();
        // Les nuages CLOUD empruntent le PCM qui va etre detruit par
        // l'echange atomique : hardStop (synchrone) — le fondu de stop()
        // continuerait de lire le buffer libere pendant ~10 ms.
        for (GrainCloud& cloud : g_padClouds) {
            cloud.hardStop();
        }
    }
};

AssignmentSession g_assignment;
HarnessVoiceStopper g_voiceStopper;

// Machine a etats de l'appui long E1 : press/hold/release sur une entree du
// navigateur. Chemin de controle uniquement, jamais dans le callback audio.
BrowserInteraction g_browserInteraction;
// Nombre d'effets assignables hors BLANK : REPEAT, REVERSE, TRANCE GATE,
// FREEZE, PHASE DIST. La liste kFxNames compte kFxCount + 1 entrees
// (BLANK compris) ; le modulo du cycle FX est donc (kFxCount + 1) et couvre
// exactement les indices valides. Ne pas modifier kFxNames sans ajuster
// kFxCount : la static_assert ci-dessous fait echouer la compilation sinon.
constexpr int kFxCount = 5;
constexpr int kRepeatFx = 1;
constexpr int kReverseFx = 2;
constexpr int kGateFx = 3;
constexpr int kFreezeFx = 4;
constexpr int kPhaseFx = 5;
const char* kFxNames[] = {"BLANK", "REPEAT", "REVERSE", "TRANCE GATE",
                          "FREEZE", "PHASE DIST"};
static_assert(sizeof(kFxNames) / sizeof(kFxNames[0]) ==
                  static_cast<std::size_t>(kFxCount) + 1U,
              "kFxCount doit laisser BLANK + kFxCount effets dans kFxNames");
const char* kDivisionNames[] = {"1/4", "1/8", "1/12", "1/16", "1/24", "1/32"};
const char* kEncoderNames[] = {"E1 NAV", "E2 AMOUNT", "E3 DIVISION", "E4 SPEED",
                               "E5 MODE", "E6 MODE", "E7 BPM"};

// Libelles des modes granulaires (cycle E6, pad GRANULAR tenu) et des modes
// de PHASE DIST (cycle E6, pad PHASE DIST tenu).
const char* grain_mode_label(int grainMode) {
    switch (grainMode) {
        case 1:
            return "PITCH";
        case 2:
            return "RISE";
        default:
            return "CLOUD";
    }
}

const char* phase_mode_label(int phaseMode) {
    switch (phaseMode) {
        case 1:
            return "PHASE SAW";
        case 2:
            return "PHASE SQUARE";
        case 3:
            return "PHASE SELF";
        default:
            return "PHASE SINE";
    }
}

PhaseDistMode phase_mode(int index) {
    switch (index) {
        case 1:
            return PhaseDistMode::Saw;
        case 2:
            return PhaseDistMode::Square;
        case 3:
            return PhaseDistMode::Self;
        default:
            return PhaseDistMode::Sine;
    }
}

// Réglages propres à chaque pad voix : la vitesse et le mode vivent ici,
// jamais dans une variable globale. La cible des encodeurs E4/E5 est le pad
// tenu (heldVoicePad), pas le dernier pad joué. Le mode granulaire, la plage
// de hauteur et la densite du pad GRANULAR vivent ici aussi (E6/E2/E3).
struct PadSettings {
    int speedPercent = 100;
    PlaybackMode mode = PlaybackMode::OneShot;
    TriggerBehavior behavior = TriggerBehavior::Gate;
    int grainMode = 0;       // GrainMode::Cloud
    int grainPitchSt = 12;   // plage de hauteur +/- demi-tons (0..24)
    int grainDensity = 4;    // x0.25 => 1.0 (0.25..4.0 par pas de 1)
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
    int recordingPad = -1;  // pad en cours d'enregistrement (Shift+pad)
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
    std::uint64_t browserEventTimeMs = 0;
    bool browserActive = false;
    bool browserEventTimeInitialized = false;
    // Vrai apres une assignation TRANSIENT reussie : les pads voix
    // declenchent alors leur plage du plan publie au lieu du WAV entier.
    // Remis a false par le chargement classique d'un WAV (appui court).
    bool transientPlanActive = false;
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
            return PlaybackMode::Granular;  // CLOUD
        case PlaybackMode::Granular:
        case PlaybackMode::SliceSync:
            return PlaybackMode::OneShot;
    }
    return PlaybackMode::OneShot;
}

void record_browser_event(AppState& state, std::uint64_t time) {
    state.browserEventTimeMs = time;
    state.browserEventTimeInitialized = true;
}

void establish_initial_browser_event(AppState& state, std::uint64_t time) {
    if (!state.browserEventTimeInitialized) {
        record_browser_event(state, time);
    }
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
    screen.showBrowser(folderName, lines.data(), lines.size(), state.browserSelection,
                       state.browserEventTimeMs);
}

void open_browser(ScreenUi& screen, AppState& state, std::uint64_t time) {
    state.browserActive = true;
    establish_initial_browser_event(state, time);
    refresh_browser(screen, state);
}

void load_browser_selection(ScreenUi& screen, AppState& state, std::uint64_t time) {
    if (state.browserEntries.empty()) return;
    const auto& selected = state.browserEntries[state.browserSelection];
    if (selected.kind == SampleCatalog::EntryKind::Folder) {
        state.browserFolder = selected.id;
        state.browserSelection = 0;
        record_browser_event(state, time);
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
        for (GrainCloud& cloud : g_padClouds) {
            cloud.hardStop();  // le PCM emprunte est detruit par le move
        }
        state.wav = std::move(loaded);
    }
    // Le chargement classique d'un WAV (appui court) reprend le mode
    // fichier-entier ; un eventuel plan TRANSIENT cesse d'etre actif.
    state.transientPlanActive = false;
    state.breakName = selected.name;
    state.browserActive = false;
    screen.showPerformance();
    std::printf("charge : %s (%u Hz natif, sortie %u Hz)\n", path.string().c_str(),
                state.wav.sampleRate, kOutputSampleRate);
}

// Applique l'action TRANSIENT confirmee dans le menu : charge le WAV cible,
// detecte les onze frontieres internes puis publie atomiquement le plan de
// douze plages via la session. En cas d'echec, le WAV en place et le plan
// precedent restent intacts et le menu est reouvert pour permettre
// l'annulation. Les modes E5, latchs et vitesses des pads ne sont jamais
// touches.
void perform_transient_assignment(ScreenUi& screen, AppState& state,
                                  const char* targetName,
                                  std::uint64_t time) {
    // time n'est plus utilise depuis que les echecs rouvrent le navigateur au
    // lieu du menu (refresh_browser conserve la chronologie existante).
    (void)time;
    // Recherche robuste : le nom copie par la machine a etats est borne a
    // 63 caracteres, donc un nom tronque est accepte par correspondance de
    // prefixe. Seuls les WAV sont eligibles ; la selection courante ne sert
    // de repli que si c'est un WAV dont le nom correspond aussi.
    auto matches = [targetName](const SampleCatalog::Entry& candidate) {
        const std::string target(targetName == nullptr ? "" : targetName);
        return candidate.kind == SampleCatalog::EntryKind::Wav &&
               (candidate.name == target ||
                (target.size() == kBrowserTargetNameMax &&
                 candidate.name.compare(0, kBrowserTargetNameMax, target) == 0));
    };
    const SampleCatalog::Entry* entry = nullptr;
    for (const auto& candidate : state.browserEntries) {
        if (matches(candidate)) {
            entry = &candidate;
            break;
        }
    }
    if (entry == nullptr &&
        state.browserSelection < state.browserEntries.size() &&
        matches(state.browserEntries[state.browserSelection])) {
        entry = &state.browserEntries[state.browserSelection];
    }
    if (entry == nullptr) {
        // Pas de zombie : la machine a etats est deja revenue en Browsing,
        // le navigateur reste donc ouvert et l'erreur va en console.
        std::fprintf(stderr, "assignation impossible : cible introuvable\n");
        refresh_browser(screen, state);
        return;
    }

    const std::filesystem::path path = state.sampleRoot / entry->relativePath;
    WavData loaded = wav_load(path.string());
    if (!loaded.valid()) {
        std::fprintf(stderr, "assignation impossible : %s\n",
                     path.string().c_str());
        refresh_browser(screen, state);
        return;
    }

    const PcmView pcm = loaded.view();
    const auto boundaries = detectTransientBoundaries(pcm);
    if (!boundaries.has_value() ||
        !g_assignment.applyTransient(std::move(loaded), *boundaries,
                                     g_voiceStopper)) {
        std::fprintf(stderr,
                     "assignation impossible : detection ou plan invalide\n");
        refresh_browser(screen, state);
        return;
    }

    state.transientPlanActive = true;
    state.breakName = entry->name;
    // Le navigateur est reellement ferme : sans ca, la pression Enter
    // suivante serait gobe par handle_key et un appui court rechargerait le
    // fichier entier, detruisant le plan tout juste publie.
    state.browserActive = false;
    screen.showPerformance();
    std::printf("TRANSIENT : %s -> 12 plages\n", entry->name.c_str());
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        const auto range = g_assignment.plan()->range(pad);
        if (range.has_value()) {
            std::printf("  pad %2zu : [%zu, %zu)\n", pad + 1U,
                        range->startFrame, range->endFrame);
        }
    }
    std::printf("  (pads 7-12 assignes mais non jouables dans le harness PC)\n");
}

// COMMIT (plan 8) : fige les 15 dernieres secondes du mix global capturees
// en continu et en fait une nouvelle assignation atomique (boucle LOAD ->
// MUTATE -> COMMIT). Chemin de controle uniquement : la conversion
// float->int16 et les vecteurs allouent ici (le heap est interdit dans le
// callback audio, pas dans le chemin de controle). L'ancienne matiere reste
// intacte jusqu'a la validation : applyTransient valide AVANT tout echange.
void perform_commit(ScreenUi& screen, AppState& state) {
    constexpr std::size_t kWindowFrames =
        CaptureBuffer::requiredBufferFrames(
            kOutputSampleRate, CaptureBuffer::kDefaultWindowSeconds);
    std::vector<float> windowL(kWindowFrames);
    std::vector<float> windowR(kWindowFrames);
    std::size_t captured = 0;
    {
        // La lecture de l'anneau est serialisee avec l'ecriture du callback
        // (copie < 1 ms, sans effet audible).
        std::lock_guard<std::mutex> lock(g_audioMutex);
        captured = g_captureBuffer.extractWindow(windowL.data(),
                                                 windowR.data(), kWindowFrames);
    }
    if (captured < 2048) {
        std::fprintf(stderr,
                     "COMMIT impossible : capture trop courte (%zu frames)\n",
                     captured);
        return;
    }
    WavData material;
    material.sampleRate = kOutputSampleRate;
    material.channels = 2;
    material.samples.resize(captured * 2);
    for (std::size_t i = 0; i < captured; ++i) {
        const float l = std::clamp(windowL[i], -1.0f, 1.0f);
        const float r = std::clamp(windowR[i], -1.0f, 1.0f);
        material.samples[i * 2] = static_cast<std::int16_t>(l * 32767.0f);
        material.samples[i * 2 + 1] = static_cast<std::int16_t>(r * 32767.0f);
    }
    const PcmView pcm = material.view();
    const auto boundaries = detectTransientBoundaries(pcm);
    if (!boundaries.has_value() ||
        !g_assignment.applyTransient(std::move(material), *boundaries,
                                     g_voiceStopper)) {
        std::fprintf(stderr, "COMMIT impossible : detection/plan invalide\n");
        return;
    }
    state.transientPlanActive = true;
    state.breakName = "COMMIT";
    screen.showPerformance();
    std::printf("COMMIT : %zu frames capturees -> 12 plages\n", captured);
    for (std::size_t pad = 0; pad < kPadCount; ++pad) {
        const auto range = g_assignment.plan()->range(pad);
        if (range.has_value()) {
            std::printf("  pad %2zu : [%zu, %zu)\n", pad + 1U,
                        range->startFrame, range->endFrame);
        }
    }
    std::printf("  (pads 7-12 assignes mais non jouables dans le harness PC)\n");
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

// Applique la vitesse au pad donné et la mémorise pour son prochain trigger.
// La mise à jour live partage le verrou du moteur afin que pad et vitesse
// forment une seule commande indivisible face au callback audio.
void set_pad_speed_percent(int pad, int speedPercent, ScreenUi& screen,
                           UiSimulation& simulation, std::uint64_t time) {
    if (pad < 0 || pad >= kVoicePadCount) return;
    simulation.pads[pad].speedPercent = std::clamp(speedPercent, 25, 400);
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        const float speed =
            static_cast<float>(simulation.pads[pad].speedPercent) / 100.0f;
        g_voices.setPadSpeed(static_cast<VoiceManager::PadId>(pad), speed);
    }
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
    {
        // Toute rotation de mode eteint le nuage du pad : un CLOUD en LATCH
        // ne doit pas survivre a la sortie du mode (sinon il jouerait du PCM
        // emprunte indefiniment).
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_padClouds[pad].stop();
    }
    show_pad_overlay(screen, pad, mode_label(mode), 0, 0, 1, time);
    std::printf("pad %d lecture : %s\n", pad + 1, mode_label(mode));
}

const char* behavior_label(TriggerBehavior behavior) {
    return behavior == TriggerBehavior::Gate ? "GATE" : "LATCH";
}

void toggle_pad_behavior(int pad, ScreenUi& screen, UiSimulation& simulation,
                         std::uint64_t time) {
    if (pad < 0 || pad >= kVoicePadCount) return;
    PadSettings& settings = simulation.pads[pad];
    settings.behavior = settings.behavior == TriggerBehavior::Gate
                            ? TriggerBehavior::Latch
                            : TriggerBehavior::Gate;
    show_pad_overlay(screen, pad, behavior_label(settings.behavior), 0, 0, 1, time);
    std::printf("pad %d comportement : %s\n", pad + 1,
                behavior_label(settings.behavior));
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

// J15 : demarre l'enregistrement direct sur un pad. La lecture du pad est
// arretee d'abord (son bloc va etre reecrit — contrat de borrow du
// recorder), puis arm : le callback audio consignera le mix post-FX.
void start_pad_recording(int pad, ScreenUi& screen, UiSimulation& simulation,
                         std::uint64_t time) {
    if (pad < 0 || pad >= kVoicePadCount) return;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_voices.stopPad(static_cast<VoiceManager::PadId>(pad));
        g_padClouds[pad].hardStop();
        g_padRecorder.arm(static_cast<std::size_t>(pad), kOutputSampleRate);
    }
    simulation.recordingPad = pad;
    std::printf("pad %d : REC (source = mix post-FX, %zu s max)\n", pad + 1,
                kPadRecordSeconds);
    char label[24]{};
    std::snprintf(label, sizeof(label), "REC PAD %d", pad + 1);
    screen.showParameter(label, 0, 0, static_cast<int>(kPadRecordSeconds),
                         time, "S");
}

void stop_pad_recording(ScreenUi& screen, UiSimulation& simulation,
                        std::uint64_t time) {
    const int pad = simulation.recordingPad;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_padRecorder.stop();
    }
    simulation.recordingPad = -1;
    if (pad < 0 || pad >= kVoicePadCount) return;
    const std::size_t frames = g_padRecorder.framesRecorded(
        static_cast<std::size_t>(pad));
    const double seconds =
        static_cast<double>(frames) / static_cast<double>(kOutputSampleRate);
    std::printf("pad %d : REC stop, %zu frames (%.2f s)\n", pad + 1, frames,
                seconds);
    screen.showPerformance();
    (void)time;
}

PadTriggerAction apply_pad_down_audio(int pad, const UiSimulation& simulation,
                                      const AppState& state) {
    const PadSettings& settings = simulation.pads[pad];
    // Quand un plan TRANSIENT est actif, le break vit dans la session (le WAV
    // charge par appui court reste intact mais inutilise). Le pad declenche
    // alors uniquement sa plage du plan ; les onze autres plages, le mode E5,
    // le latch et la vitesse du pad restent inchanges par conception.
    const PcmView pcm = state.transientPlanActive && g_assignment.plan() != nullptr
                            ? g_assignment.currentWav()->view()
                            : state.wav.view();
    const float speed = static_cast<float>(settings.speedPercent) / 100.0f;
    std::lock_guard<std::mutex> lock(g_audioMutex);
    const auto padId = static_cast<VoiceManager::PadId>(pad);

    // Source par pad (J15) : la matiere enregistree prime sur le plan
    // partage et survit a tout chargement (seul un nouvel enregistrement
    // sur ce pad la remplace). Granulaire et voix suivent la meme regle.
    const PcmView recorded = g_padRecorder.pcm(static_cast<std::size_t>(pad));
    if (recorded.valid()) {
        if (settings.mode == PlaybackMode::Granular) {
            GrainCloud& cloud = g_padClouds[pad];
            const bool playing = cloud.active();
            const PadTriggerAction action =
                padDownAction(settings.mode, settings.behavior, playing);
            if (action == PadTriggerAction::Stop) {
                cloud.stop();
            } else {
                const std::uint32_t seed =
                    static_cast<std::uint32_t>(pad + 1U) * 2654435761U;
                cloud.setMode(static_cast<GrainMode>(settings.grainMode));
                cloud.setPitchRangeSemitones(settings.grainPitchSt);
                cloud.setDensity(
                    static_cast<float>(settings.grainDensity) * 0.25f);
                cloud.start(recorded, 0, recorded.frameCount(), speed, seed);
            }
            return action;
        }
        const PadTriggerAction action = padDownAction(
            settings.mode, settings.behavior, g_voices.isPadPlaying(padId));
        if (action == PadTriggerAction::Stop) {
            g_voices.stopPad(padId);
        } else {
            g_voices.trigger(padId, recorded, 0, recorded.frameCount(), speed,
                             settings.mode);
        }
        return action;
    }

    if (settings.mode == PlaybackMode::Granular) {
        // GRANULAR (plan 7.3 / J8) : la plage assignee devient un nuage de
        // grains (mode CLOUD/PITCH/RISE, hauteur, densite). Meme logique
        // GATE/LATCH que les autres modes, mais sans passer par
        // VoiceManager : le nuage emprunte le PCM (jamais copie) et rend dans
        // le callback audio, sous le meme verrou.
        GrainCloud& cloud = g_padClouds[pad];
        const bool playing = cloud.active();
        const PadTriggerAction action =
            padDownAction(settings.mode, settings.behavior, playing);
        if (action == PadTriggerAction::Stop) {
            cloud.stop();
        } else {
            std::size_t start = 0;
            std::size_t end = pcm.frameCount();
            if (state.transientPlanActive && g_assignment.plan() != nullptr) {
                const auto range = g_assignment.plan()->range(pad);
                if (range.has_value()) {
                    start = range->startFrame;
                    end = range->endFrame;
                }
            }
            // Graine deterministe et distincte par pad : chaque pad a son
            // propre nuage, reproductible a l'identique.
            const std::uint32_t seed =
                static_cast<std::uint32_t>(pad + 1U) * 2654435761U;
            cloud.setMode(static_cast<GrainMode>(settings.grainMode));
            cloud.setPitchRangeSemitones(settings.grainPitchSt);
            cloud.setDensity(static_cast<float>(settings.grainDensity) * 0.25f);
            cloud.start(pcm, start, end, speed, seed);
        }
        return action;
    }

    const PadTriggerAction action = padDownAction(
        settings.mode, settings.behavior, g_voices.isPadPlaying(padId));
    if (action == PadTriggerAction::Stop) {
        g_voices.stopPad(padId);
    } else if (state.transientPlanActive && g_assignment.plan() != nullptr) {
        const auto range = g_assignment.plan()->range(pad);
        if (range.has_value()) {
            g_voices.trigger(padId, pcm, range->startFrame, range->endFrame,
                             speed, settings.mode);
        } else {
            g_voices.trigger(padId, pcm, 0, pcm.frameCount(), speed,
                             settings.mode);
        }
    } else {
        g_voices.trigger(padId, pcm, 0, pcm.frameCount(), speed, settings.mode);
    }
    return action;
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
            } else if (state.browserActive &&
                       g_browserInteraction.mode() !=
                           BrowserMode::AssignmentMenu &&
                       !g_browserInteraction.pressed()) {
                if (direction > 0) {
                    if (state.browserSelection + 1 < state.browserEntries.size()) {
                        ++state.browserSelection;
                        record_browser_event(state, time);
                        refresh_browser(screen, state);
                    }
                } else if (state.browserSelection > 0) {
                    --state.browserSelection;
                    record_browser_event(state, time);
                    refresh_browser(screen, state);
                }
            } else if (simulation.heldVoicePad >= 0) {
                open_browser(screen, state, time);
                std::printf("navigateur pour pad %d (relacher pour fermer)\n",
                            simulation.heldVoicePad + 1);
            } else {
                screen.showParameter("E1 TENIR PAD", 0, 0, 0, time);
            }
            break;
        case 2:
            if (simulation.heldVoicePad >= 0 &&
                simulation.pads[simulation.heldVoicePad].mode ==
                    PlaybackMode::Granular) {
                // Pad GRANULAR tenu : E2 regle la plage de hauteur par
                // grain (+/- demi-tons), sinon FX AMOUNT global.
                PadSettings& settings =
                    simulation.pads[simulation.heldVoicePad];
                settings.grainPitchSt =
                    std::clamp(settings.grainPitchSt + direction, 0, 24);
                std::printf("pad %d pitch : +/-%d st\n",
                            simulation.heldVoicePad + 1,
                            settings.grainPitchSt);
                screen.showParameter("PITCH RANGE", settings.grainPitchSt, 0,
                                     24, time, "ST");
            } else {
                simulation.effectAmount =
                    std::clamp(simulation.effectAmount + direction * 5, 0, 100);
                g_repeatAmountPercent.store(simulation.effectAmount);
                screen.showParameter("FX AMOUNT", simulation.effectAmount, 0, 100, time, "%");
                std::printf("repeat amount : %d%%\n", simulation.effectAmount);
            }
            break;
        case 3:
            if (simulation.heldVoicePad >= 0 &&
                simulation.pads[simulation.heldVoicePad].mode ==
                    PlaybackMode::Granular) {
                // Pad GRANULAR tenu : E3 regle la densite de grains
                // (x0.25 par pas), sinon division du REPEAT.
                PadSettings& settings =
                    simulation.pads[simulation.heldVoicePad];
                settings.grainDensity =
                    std::clamp(settings.grainDensity + direction, 1, 16);
                std::printf("pad %d densite : x%.2f\n",
                            simulation.heldVoicePad + 1,
                            static_cast<float>(settings.grainDensity) * 0.25f);
                screen.showParameter("DENSITY", settings.grainDensity, 1, 16,
                                     time);
            } else {
                simulation.repeatDivision =
                    (simulation.repeatDivision + direction + 6) % 6;
                g_repeatDivision.store(simulation.repeatDivision);
                screen.showParameter(kDivisionNames[simulation.repeatDivision],
                                     simulation.repeatDivision, 0, 5, time);
                std::printf("repeat division : %s\n",
                            kDivisionNames[simulation.repeatDivision]);
            }
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
            // E6 est contextuel : mode granulaire sur pad voix GRANULAR
            // tenu (CLOUD/PITCH/RISE), mode LOOP/SHEPARD sur pad FX REPEAT
            // tenu, forme du modulateur sur pad FX PHASE DIST tenu.
            if (simulation.heldVoicePad >= 0 &&
                simulation.pads[simulation.heldVoicePad].mode ==
                    PlaybackMode::Granular) {
                PadSettings& settings =
                    simulation.pads[simulation.heldVoicePad];
                settings.grainMode = (settings.grainMode + 1) % 3;
                std::printf("pad %d granulaire : %s\n",
                            simulation.heldVoicePad + 1,
                            grain_mode_label(settings.grainMode));
                screen.showParameter(grain_mode_label(settings.grainMode),
                                     settings.grainMode, 0, 2, time);
            } else if (simulation.heldFxPad >= 0 &&
                       simulation.fxAssign[simulation.heldFxPad] == kRepeatFx) {
                const int next = (g_repeatMode.load() + 1) % 2;
                g_repeatMode.store(next);
                std::printf("repeat mode : %s\n",
                            next == 0 ? "LOOP" : "SHEPARD");
                screen.showParameter(next == 0 ? "REPEAT LOOP"
                                               : "REPEAT SHEPARD",
                                     next, 0, 1, time);
            } else if (simulation.heldFxPad >= 0 &&
                       simulation.fxAssign[simulation.heldFxPad] == kPhaseFx) {
                const int next = (g_phaseMode.load() + 1) % 4;
                g_phaseMode.store(next);
                std::printf("phase mode : %s\n", phase_mode_label(next));
                screen.showParameter(phase_mode_label(next), next, 0, 3, time);
            } else {
                screen.showParameter("E6 LFO RESERVE", 0, 0, 0, time);
            }
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
                g_reverseActive.store(simulation.fxCandidate == kReverseFx);
                g_gateActive.store(simulation.fxCandidate == kGateFx);
                g_freezeActive.store(simulation.fxCandidate == kFreezeFx);
                g_phaseActive.store(simulation.fxCandidate == kPhaseFx);
                screen.showFxPad(7 + simulation.heldFxPad,
                                 kFxNames[simulation.fxCandidate], 1);
                std::printf("pad %d : FX = %s\n", 7 + simulation.heldFxPad,
                            kFxNames[simulation.fxCandidate]);
            } else if (state.browserActive) {
                load_browser_selection(screen, state, time);
            } else if (simulation.heldVoicePad >= 0) {
                open_browser(screen, state, time);
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
                toggle_pad_behavior(simulation.heldVoicePad, screen, simulation, time);
            } else {
                screen.showParameter("E5 TENIR PAD", 0, 0, 0, time);
            }
            break;
        case 6:
            screen.showParameter("E6 LFO RESERVE", 0, 0, 0, time);
            break;
        default:
            break;
    }
}

void voice_pad_down(int pad, ScreenUi& screen, UiSimulation& simulation,
                    AppState& state) {
    const PadTriggerAction action = apply_pad_down_audio(pad, simulation, state);
    // Le pad appuyé devient la cible des encodeurs tant qu'il est tenu.
    // Règle "le dernier appuyé gagne" : l'ordre d'appui est mémorisé par pad.
    simulation.heldVoicePad = pad;
    simulation.padPressSeq[pad] = ++simulation.pressSeqCounter;
    simulation.lastPadId = pad;
    if (simulation.heldFxPad < 0) {
        screen.showPerformance();
    }
    std::printf("pad %d : %s [%s / %s] (tenir + E1 navigateur, + E4 vitesse, "
                "+ E5 lecture/clic comportement)\n",
                pad + 1, action == PadTriggerAction::Stop ? "stop" : "trigger",
                mode_label(simulation.pads[pad].mode),
                behavior_label(simulation.pads[pad].behavior));
}

// Quand le pad tenu est relâché, la cible retombe sur le dernier pad encore
// appuyé (le plus récemment pressé), sinon aucun. La fermeture du navigateur
// reste gérée par poll_numpad quand tous les pads voix sont relâchés.
void voice_pad_up(int pad, ScreenUi& screen, UiSimulation& simulation,
                  AppState& state) {
    (void)screen;
    (void)state;
    if (padUpAction(simulation.pads[pad].behavior) == PadTriggerAction::Stop) {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        if (simulation.pads[pad].mode == PlaybackMode::Granular) {
            g_padClouds[pad].stop();  // fondu ~10 ms, aucun clic
        } else {
            g_voices.stopPad(static_cast<VoiceManager::PadId>(pad));
        }
    }
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
    g_reverseActive.store(simulation.fxCandidate == kReverseFx);
    g_gateActive.store(simulation.fxCandidate == kGateFx);
    g_freezeActive.store(simulation.fxCandidate == kFreezeFx);
    g_phaseActive.store(simulation.fxCandidate == kPhaseFx);
    screen.showFxPad(7 + pad, kFxNames[simulation.fxCandidate],
                     simulation.selectedEncoder);
    std::printf("pad %d : %s\n", 7 + pad, kFxNames[simulation.fxCandidate]);
}

void fx_pad_up(int pad, ScreenUi& screen, UiSimulation& simulation,
               AppState& state) {
    if (simulation.heldFxPad != pad) return;
    g_repeatActive.store(false);
    g_reverseActive.store(false);
    g_gateActive.store(false);
    g_freezeActive.store(false);
    g_phaseActive.store(false);
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

    const bool shiftHeld = ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0) ||
                           ((GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0);
    const std::uint64_t now = now_ms();
    for (int pad = 0; pad < 6; ++pad) {
        if ((pressed & (1U << pad)) != 0) {
            if (shiftHeld) {
                // Shift + pad = enregistrement direct (J15) au lieu du
                // trigger : le press est consomme par le recorder.
                start_pad_recording(pad, screen, simulation, now);
            } else {
                voice_pad_down(pad, screen, simulation, state);
            }
        }
        if ((released & (1U << pad)) != 0) {
            if (simulation.recordingPad == pad) {
                stop_pad_recording(screen, simulation, now);
            } else {
                voice_pad_up(pad, screen, simulation, state);
            }
        }
    }
    // Relacher Shift pendant l'enregistrement l'arrete aussi.
    if (!shiftHeld && simulation.recordingPad >= 0) {
        stop_pad_recording(screen, simulation, now);
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
        // Le relachement du pad voix ferme le navigateur ; si le menu etait
        // ouvert, il est annule pour que la machine a etats ne confirme plus
        // une assignation devenue invisible.
        if (g_browserInteraction.mode() == BrowserMode::AssignmentMenu) {
            g_browserInteraction.cancel(now_ms());
        }
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
        if (state.browserActive ||
            g_browserInteraction.mode() == BrowserMode::AssignmentMenu) {
            // Enter dans le navigateur et dans le menu est pilote par la
            // detection d'appui long (GetAsyncKeyState(VK_RETURN)) de la
            // boucle principale : le clic court est decide au relachement.
        } else {
            encoder_click(screen, simulation, state);
        }
    } else if (c == 8 || c == 127) {
        if (g_browserInteraction.mode() == BrowserMode::AssignmentMenu) {
            const auto event = g_browserInteraction.cancel(now_ms());
            if (event.type == BrowserInteraction::Event::Type::CancelMenu) {
                // L'annulation conserve la chronologie de defilement du
                // navigateur (aucun record_browser_event).
                refresh_browser(screen, state);
            }
        } else if (state.browserActive && simulation.heldFxPad < 0) {
            const auto parent = state.catalog.parent(state.browserFolder);
            if (parent) {
                state.browserFolder = *parent;
                state.browserSelection = 0;
                record_browser_event(state, now_ms());
                refresh_browser(screen, state);
            }
        }
    } else if (c == ' ') {
        const int pad = simulation.lastPadId;
        const PadTriggerAction action = apply_pad_down_audio(pad, simulation, state);
        std::printf("espace pad %d : %s [%s / %s]\n", pad + 1,
                    action == PadTriggerAction::Stop ? "stop" : "trigger",
                    mode_label(simulation.pads[pad].mode),
                    behavior_label(simulation.pads[pad].behavior));
    } else if (c == 'v') {
        // COMMIT : capture retrospective du mix -> nouvelle assignation.
        perform_commit(screen, state);
    }
    // '1'-'9' ignorés : les pads numpad sont lus par poll_numpad (VK_NUMPADx).
}

void key_loop(ScreenUi& screen, ScreenPreview& preview, AppState& state) {
    UiSimulation simulation;
    bool enterHeldPrev = false;
    while (g_running.load()) {
        while (_kbhit()) {
            handle_key(_getch(), screen, simulation, state);
        }
        // Appui long E1 : VK_RETURN est lu en continu pour distinguer le clic
        // court (relachement avant 600 ms) du menu d'assignation (maintien).
        const bool enterHeld = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        const std::uint64_t time = now_ms();
        if (enterHeld && !enterHeldPrev) {
            if (g_browserInteraction.mode() == BrowserMode::AssignmentMenu) {
                const auto event = g_browserInteraction.confirm(time);
                if (event.type ==
                    BrowserInteraction::Event::Type::ConfirmTransient) {
                    perform_transient_assignment(screen, state,
                                                 event.name.data(), time);
                }
            } else if (state.browserActive && !state.browserEntries.empty()) {
                const auto& selected =
                    state.browserEntries[state.browserSelection];
                const auto kind =
                    selected.kind == SampleCatalog::EntryKind::Folder
                        ? BrowserInteraction::EntryKind::Folder
                        : BrowserInteraction::EntryKind::Wav;
                g_browserInteraction.press(time, selected.name.c_str(), kind);
            }
        }
        if (enterHeld) {
            const auto event = g_browserInteraction.hold(time);
            if (event.type == BrowserInteraction::Event::Type::EnterMenu) {
                // Option surlignee = TRANSIENT (index 1) : c'est l'action que
                // confirme Enter en V0. ALL PADS reste affiche en reserve et
                // CANCEL correspond a Retour arriere.
                screen.showAssignmentMenu(event.name.data(), 1, time);
            }
        }
        if (!enterHeld && enterHeldPrev) {
            const auto event = g_browserInteraction.release(time);
            if (event.type == BrowserInteraction::Event::Type::ShortPress) {
                load_browser_selection(screen, state, time);
            }
        }
        enterHeldPrev = enterHeld;
        poll_numpad(screen, simulation, state);
        // Overlay REC : secondes ecoulees tant que le recorder tourne ;
        // l'arret automatique (capacite) est detecte ici (chemin controle).
        if (simulation.recordingPad >= 0) {
            const int recPad = simulation.recordingPad;
            const std::size_t recFrames = g_padRecorder.framesRecorded(
                static_cast<std::size_t>(recPad));
            const int recSeconds =
                static_cast<int>(recFrames / kOutputSampleRate);
            if (g_padRecorder.recording()) {
                char label[24]{};
                std::snprintf(label, sizeof(label), "REC PAD %d", recPad + 1);
                screen.showParameter(label, recSeconds, 0,
                                     static_cast<int>(kPadRecordSeconds),
                                     now_ms(), "S");
            } else {
                std::printf("pad %d : REC capacite atteinte (%zu frames)\n",
                            recPad + 1, recFrames);
                simulation.recordingPad = -1;
                screen.showPerformance();
            }
        }
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
            open_browser(screen, state, time);
        } else {
            screen.showPerformance();
        }
    } else if (state.browserActive) {
        if (c == 'j' && state.browserSelection + 1 < state.browserEntries.size()) {
            ++state.browserSelection;
            record_browser_event(state, time);
            refresh_browser(screen, state);
        } else if (c == 'k' && state.browserSelection > 0) {
            --state.browserSelection;
            record_browser_event(state, time);
            refresh_browser(screen, state);
        } else if (c == '\r' || c == '\n') {
            load_browser_selection(screen, state, time);
        } else if (c == 8 || c == 127) {
            const auto parent = state.catalog.parent(state.browserFolder);
            if (parent) {
                state.browserFolder = *parent;
                state.browserSelection = 0;
                record_browser_event(state, time);
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
        const int pad = simulation.lastPadId;
        const PadTriggerAction action = apply_pad_down_audio(pad, simulation, state);
        std::printf("espace pad %d : %s [%s / %s]\n", pad + 1,
                    action == PadTriggerAction::Stop ? "stop" : "trigger",
                    mode_label(simulation.pads[pad].mode),
                    behavior_label(simulation.pads[pad].behavior));
    } else if (c == 'v') {
        // COMMIT : capture retrospective du mix -> nouvelle assignation.
        perform_commit(screen, state);
    } else if (c == 'r') {
        // J15 : bascule de l'enregistrement direct sur le dernier pad
        // (source = mix post-FX, 6 s max).
        if (simulation.recordingPad == simulation.lastPadId) {
            stop_pad_recording(screen, simulation, time);
        } else {
            if (simulation.recordingPad >= 0) {
                stop_pad_recording(screen, simulation, time);
            }
            start_pad_recording(simulation.lastPadId, screen, simulation,
                                time);
        }
    } else if (c == 'm') {
        set_pad_mode(simulation.lastPadId,
                     next_mode(simulation.pads[simulation.lastPadId].mode),
                     screen, simulation, time);
    } else if (c == 'p') {
        // Cycle du mode granulaire du dernier pad (CLOUD/PITCH/RISE).
        PadSettings& settings = simulation.pads[simulation.lastPadId];
        settings.grainMode = (settings.grainMode + 1) % 3;
        std::printf("pad %d granulaire : %s\n", simulation.lastPadId + 1,
                    grain_mode_label(settings.grainMode));
    } else if (c == 's') {
        // Bascule du mode REPEAT (LOOP/SHEPARD).
        const int next = (g_repeatMode.load() + 1) % 2;
        g_repeatMode.store(next);
        std::printf("repeat mode : %s\n", next == 0 ? "LOOP" : "SHEPARD");
    } else if (c == 'g') {
        toggle_pad_behavior(simulation.lastPadId, screen, simulation, time);
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
        // Overlay REC : secondes ecoulees ; arret auto (capacite) detecte.
        if (simulation.recordingPad >= 0) {
            const int recPad = simulation.recordingPad;
            const std::size_t recFrames = g_padRecorder.framesRecorded(
                static_cast<std::size_t>(recPad));
            const int recSeconds =
                static_cast<int>(recFrames / kOutputSampleRate);
            if (g_padRecorder.recording()) {
                char label[24]{};
                std::snprintf(label, sizeof(label), "REC PAD %d", recPad + 1);
                screen.showParameter(label, recSeconds, 0,
                                     static_cast<int>(kPadRecordSeconds),
                                     now_ms(), "S");
            } else {
                std::printf("pad %d : REC capacite atteinte (%zu frames)\n",
                            recPad + 1, recFrames);
                simulation.recordingPad = -1;
                screen.showPerformance();
            }
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

    static int appliedAmountPercent = -1;
    static int appliedDivision = -1;
    static int appliedBpm = -1;
    static bool appliedRepeatActive = false;
    static bool appliedGateActive = false;
    const int targetAmountPercent = g_repeatAmountPercent.load();
    if (targetAmountPercent != appliedAmountPercent) {
        const float amount = static_cast<float>(targetAmountPercent) / 100.0f;
        g_repeat.setAmount(amount);
        g_reverse.setAmount(amount);
        g_phase.setAmount(amount);
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
        g_spectralGate.setBpm(static_cast<float>(targetBpm));
        appliedBpm = targetBpm;
    }
    const bool targetRepeatActive = g_repeatActive.load();
    if (targetRepeatActive != appliedRepeatActive) {
        g_repeat.setActive(targetRepeatActive);
        appliedRepeatActive = targetRepeatActive;
    }
    static int appliedRepeatMode = 0;
    const int targetRepeatMode = g_repeatMode.load();
    if (targetRepeatMode != appliedRepeatMode) {
        g_repeat.setMode(targetRepeatMode == 0 ? RepeatMode::Loop
                                               : RepeatMode::Shepard);
        appliedRepeatMode = targetRepeatMode;
    }
    static int appliedPhaseMode = 0;
    const int targetPhaseMode = g_phaseMode.load();
    if (targetPhaseMode != appliedPhaseMode) {
        g_phase.setMode(phase_mode(targetPhaseMode));
        appliedPhaseMode = targetPhaseMode;
    }
    const bool targetReverseActive = g_reverseActive.load();
    static bool appliedReverseActive = false;
    if (targetReverseActive != appliedReverseActive) {
        g_reverse.setActive(targetReverseActive);
        appliedReverseActive = targetReverseActive;
    }
    const bool targetPhaseActive = g_phaseActive.load();
    static bool appliedPhaseActive = false;
    if (targetPhaseActive != appliedPhaseActive) {
        g_phase.setActive(targetPhaseActive);
        appliedPhaseActive = targetPhaseActive;
    }
    const bool targetGateActive = g_gateActive.load();
    if (targetGateActive != appliedGateActive) {
        g_spectralGate.setActive(targetGateActive);
        appliedGateActive = targetGateActive;
    }
    static bool appliedFreezeActive = false;
    const bool targetFreezeActive = g_freezeActive.load();
    if (targetFreezeActive != appliedFreezeActive) {
        g_spectralFreeze.setActive(targetFreezeActive);
        appliedFreezeActive = targetFreezeActive;
    }

    constexpr std::size_t kRenderBlock = 512;
    std::array<float, kRenderBlock> tmpL{};
    std::array<float, kRenderBlock> tmpR{};
    std::size_t rendered = 0;
    while (rendered < frames) {
        const std::size_t count = std::min<std::size_t>(kRenderBlock, frames - rendered);
        g_voices.render(tmpL.data(), tmpR.data(), static_cast<int>(count));
        for (int pad = 0; pad < kVoicePadCount; ++pad) {
            // Les nuages CLOUD actifs s'ajoutent au mix des voix (materiau
            // emprunte, rendu sous le verrou du callback).
            g_padClouds[pad].render(tmpL.data(), tmpR.data(),
                                    static_cast<int>(count));
        }
        g_repeat.process(tmpL.data(), tmpR.data(), static_cast<int>(count));
        g_reverse.process(tmpL.data(), tmpR.data(), static_cast<int>(count));
        g_phase.process(tmpL.data(), tmpR.data(), static_cast<int>(count));
        g_spectralGate.process(tmpL.data(), tmpR.data(), static_cast<int>(count));
        g_spectralFreeze.process(tmpL.data(), tmpR.data(), static_cast<int>(count));
        // COMMIT : le mix final (post-FX) alimente l'anneau retrospectif.
        g_captureBuffer.record(tmpL.data(), tmpR.data(), static_cast<int>(count));
        // J15 : le meme mix post-FX alimente l'enregistrement direct
        // Shift+pad (source = ce qu'on entend).
        if (g_padRecorder.recording()) {
            g_padRecorder.record(tmpL.data(), tmpR.data(),
                                 static_cast<int>(count));
        }
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

    // Slew de transition des FX : 15 ms de glissement a l'activation
    // (courbe de transition "liquide", aucun clic de commutation).
    g_repeat.setSlewFrames(720U);
    g_reverse.setSlewFrames(720U);
    g_phase.setSlewFrames(720U);
    g_phase.setRateHz(1.0f);

    std::printf("numpad 1-6 : pads voix (appui selon ONE SHOT/LOOP/GRANULAR + GATE/LATCH ; tenir + E1 navigateur, + E4 vitesse, + E5 rotation lecture/clic comportement, + E6 mode granulaire CLOUD/PITCH/RISE, + E2 pitch +/-st, + E3 densite)\n");
    std::printf("numpad 7-9 : pads FX (BLANK/REPEAT/REVERSE/TRANCE GATE/FREEZE/PHASE DIST ; E6 sur REPEAT = LOOP/SHEPARD, sur PHASE DIST = SINE/SAW/SQUARE/SELF) | F1-F7 : encodeurs | espace : simule appui dernier pad | q : quitter\n");
    std::printf("Shift + numpad 1-6 : enregistre le mix post-FX sur ce pad (6 s max, relacher pour stopper) | Linux : r = REC du dernier pad\n");
    std::printf("entree : clic | tenir entree 600 ms sur un WAV : menu ALL PADS / TRANSIENT / CANCEL (entree confirme, retour arriere annule)\n");
    key_loop(screen, preview, state);

    ma_device_uninit(&device);
    return 0;
}
