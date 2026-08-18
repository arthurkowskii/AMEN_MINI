#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "pcm_view.h"

// Moteur granulaire par pad (plan 7.3 / J8, evolution P0 "GRANULAR").
//
// Transforme la plage assignee d'un pad en nuage de grains : des grains de
// 30 a 150 ms (echelle reglable), un toutes les ~22 ms (densite reglable),
// position et longueur tirees d'un LCG seme par l'appelant (dispersion
// bornee par la plage). Chaque grain est fenetre par une enveloppe de Hann
// aux deux extremites : aucun clic a la naissance ni a la mort d'un grain.
// Le PCM est EMPRUNTE (PcmView), jamais copie.
//
// Modes (GrainMode) :
//  - Cloud : lecture naturelle du materiau (comportement historique).
//  - Pitch : chaque grain lit a une hauteur fixe tiree au sort dans
//    +/- pitchRangeSemitones (grains transposes, machine a vocal chops).
//  - Rise  : chaque grain glisse de -range a +range demi-tons sur sa vie
//    (mini-risers en serie, la "montee infinie" du nuage).
//
// Memoire fixe : kMaxGrains grains (8), aucune allocation, aucune attente.
// Non reentrant : demarrer/arreter et rendre doivent etre serialises par
// l'appelant (le harness passe par le mutex du moteur).
enum class GrainMode : std::uint8_t {
    Cloud,
    Pitch,
    Rise,
};

class GrainCloud {
public:
    static constexpr std::size_t kMaxGrains = 8;
    static constexpr float kMinUserSpeed = 0.25f;
    static constexpr float kMaxUserSpeed = 4.0f;
    static constexpr int kMaxPitchRangeSemitones = 24;
    static constexpr float kMinDensity = 0.25f;
    static constexpr float kMaxDensity = 4.0f;
    static constexpr float kMinGrainSizeScale = 0.5f;
    static constexpr float kMaxGrainSizeScale = 2.0f;

    GrainCloud() = default;

    // Demarre (ou redemarre) le nuage sur [rangeStart, rangeEnd) du PCM
    // emprunte. seed fixe la sequence de grains : determinisme des tests.
    void start(PcmView pcm, std::size_t rangeStart, std::size_t rangeEnd,
               float userSpeed, std::uint32_t seed);

    // Arret progressif (~10 ms de fondu) : aucun clic, puis inactif.
    void stop();

    // Arret synchrone immediat : reserve aux chemins qui DETRUISENT le PCM
    // emprunte juste apres (echange atomique d'assignation, chargement d'un
    // nouveau WAV). Le fondu de stop() continuerait a lire le buffer libere.
    void hardStop();

    // Reglages live : appliques aux prochains grains engendres, jamais a un
    // grain deja ne (aucun changement d'etat ne peut cliquer).
    void setMode(GrainMode mode) { mode_ = mode; }
    void setPitchRangeSemitones(int semitones) {
        pitchRangeSemitones_ = std::clamp(semitones, 0, kMaxPitchRangeSemitones);
    }
    void setDensity(float density) {
        density_ = std::clamp(density, kMinDensity, kMaxDensity);
    }
    void setGrainSizeScale(float scale) {
        grainSizeScale_ = std::clamp(scale, kMinGrainSizeScale, kMaxGrainSizeScale);
    }

    // Ajoute la sortie du nuage dans outLeft/outRight (non remplace).
    void render(float* outLeft, float* outRight, int numFrames);

    bool active() const { return active_; }
    std::size_t activeGrainCount() const;  // observable pour les tests
    std::size_t totalSpawned() const { return spawnCount_; }  // tests densite
    GrainMode mode() const { return mode_; }
    int pitchRangeSemitones() const { return pitchRangeSemitones_; }
    float density() const { return density_; }
    float grainSizeScale() const { return grainSizeScale_; }

private:
    struct Grain {
        float sourcePosF = 0.0f;  // position source fractionnaire
        std::size_t length = 0;   // longueur du grain en frames source
        std::size_t played = 0;   // frames source parcourues
        std::size_t envelope = 0; // longueur d'enveloppe Hann en frames
        float pitchMul = 1.0f;    // taux de lecture courant du grain
        float pitchMulStart = 1.0f;  // hauteur au debut (Rise : basse)
        float pitchMulEnd = 1.0f;    // hauteur a la fin (Rise : haute)
        bool active = false;
    };

    static std::uint32_t nextLcg(std::uint32_t& state);
    static float hann(std::size_t index, std::size_t size);
    static float semitoneMul(float semitones);  // 2^(st/12)
    float pitchMulForNewGrain(float start, float end, std::size_t played,
                              std::size_t length) const;
    void spawnGrain();

    PcmView pcm_{};
    std::size_t rangeStart_ = 0;
    std::size_t rangeEnd_ = 0;
    float userSpeed_ = 1.0f;
    std::uint32_t lcg_ = 0;
    std::size_t spawnEveryFrames_ = 1;
    std::size_t framesSinceSpawn_ = 0;
    std::array<Grain, kMaxGrains> grains_{};
    float masterFade_ = 1.0f;
    float fadeStep_ = 0.0f;
    bool fadingOut_ = false;
    bool active_ = false;
    std::size_t spawnCount_ = 0;  // grains reellement engendres (tests)

    GrainMode mode_ = GrainMode::Cloud;
    int pitchRangeSemitones_ = 0;
    float density_ = 1.0f;
    float grainSizeScale_ = 1.0f;
};
