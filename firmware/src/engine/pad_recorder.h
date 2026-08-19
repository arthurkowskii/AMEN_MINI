#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pcm_view.h"

// Enregistrement direct par pad (J15, Shift + pad maintenu).
//
// Le callback audio appelle record() tant qu'un pad est arme : le signal
// est somme en mono (L+R)/2, converti en int16 (round-half-up, asymetrie
// documentee pour les valeurs negatives) et ecrit dans le bloc statique du
// pad. Le stockage est FOURNI par l'appelant (contigu, kMaxPads blocs de
// capacityFrames samples mono) : aucune allocation, aucune attente, pret a
// vivre en PSRAM sur Teensy (pattern CaptureBuffer).
//
// arm(pad) demarre (ou redemarre) l'enregistrement du pad : son compteur
// repart de 0 et son bloc est reecrit depuis le debut. Un seul pad
// enregistre a la fois : armer un autre pad arrete le precedent (ses frames
// restent intactes). La capacite atteinte arrete automatiquement.
//
// CONTRAT DE BORROW (comme GrainCloud) : pcm(pad) emprunte le bloc du pad.
// Un nouvel arm(pad) invalide la vue precedente de CE pad ; l'appelant doit
// avoir arrete toute lecture de ce pad avant d'armer (le harness passe par
// stopPad/cloud.stop sous le meme verrou). Les vues des autres pads ne
// sont jamais affectees.
class PadRecorder {
public:
    static constexpr std::size_t kMaxPads = 12;

    static constexpr std::size_t requiredSamples(
        std::size_t padCount, std::size_t capacityFrames) {
        return padCount * capacityFrames;
    }

    PadRecorder(std::size_t padCount, std::int16_t* storage,
                std::size_t capacityFrames);

    // Demarre l'enregistrement sur `pad` au sampleRate donne (chemin de
    // controle uniquement, jamais dans le callback audio). Arrete tout
    // enregistrement en cours. Hors bornes = no-op.
    void arm(std::size_t pad, std::uint32_t sampleRate);

    // Arrete l'enregistrement courant (les frames restent).
    void stop();

    // Consigne `numFrames` frames stereo dans le bloc du pad actif.
    // Aucune allocation. Pointeurs nuls / taille <= 0 / inactif = no-op.
    void record(const float* left, const float* right, int numFrames);

    bool recording() const { return recording_; }
    int activePad() const { return activePad_; }
    std::size_t framesRecorded(std::size_t pad) const {
        return pad < padCount_ ? frames_[pad] : 0;
    }
    std::size_t padCount() const { return padCount_; }
    std::size_t capacityFrames() const { return capacityFrames_; }

    // Vue empruntee de la matiere enregistree du pad (mono, channels=1).
    // Valide jusqu'au prochain arm de CE pad.
    PcmView pcm(std::size_t pad) const;

private:
    std::size_t padCount_;
    std::int16_t* storage_;
    std::size_t capacityFrames_;
    bool recording_ = false;
    int activePad_ = -1;
    std::array<std::size_t, kMaxPads> frames_{};
    std::array<std::uint32_t, kMaxPads> sampleRates_{};
};
