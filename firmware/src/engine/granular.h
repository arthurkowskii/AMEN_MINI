#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pcm_view.h"

// Nuage granulaire par pad (plan 7.3, V0/V1).
//
// Transforme la plage assignee d'un pad en nuage de grains : des grains de
// 30 a 150 ms, un toutes les ~22 ms (densite bornee), position et longueur
// tirees d'un LCG seme par l'appelant (dispersion bornee par la plage).
// Chaque grain est fenetre par une enveloppe de Hann aux deux extremites :
// aucun clic a la naissance ni a la mort d'un grain. Le PCM est EMPRUNTE
// (PcmView), jamais copie : le proprietaire doit garder le buffer vivant
// tant que le nuage tourne (contrat identique a PadAssignmentPlan).
//
// Memoire fixe : kMaxGrains grains (8), aucune allocation, aucune attente.
// Non reentrant : demarrer/arreter et rendre doivent etre serialises par
// l'appelant (le harness passe par le mutex du moteur).
class GrainCloud {
public:
    static constexpr std::size_t kMaxGrains = 8;
    static constexpr float kMinUserSpeed = 0.25f;
    static constexpr float kMaxUserSpeed = 4.0f;

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
    // La coupure franche est inaudible en pratique : ces chemins coupent
    // deja tout l'audio de la machine.
    void hardStop();

    // Ajoute la sortie du nuage dans outLeft/outRight (non remplace).
    void render(float* outLeft, float* outRight, int numFrames);

    bool active() const { return active_; }
    std::size_t activeGrainCount() const;  // observable pour les tests

private:
    struct Grain {
        float sourcePosF = 0.0f;  // position source fractionnaire
        std::size_t length = 0;   // longueur du grain en frames source
        std::size_t played = 0;   // frames source parcourues
        std::size_t envelope = 0; // longueur d'enveloppe Hann en frames
        bool active = false;
    };

    static std::uint32_t nextLcg(std::uint32_t& state);
    static float hann(std::size_t index, std::size_t size);
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
};
