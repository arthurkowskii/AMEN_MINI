#pragma once

#include <algorithm>
#include <cstdint>

// Rampe lineaire de gain dry/wet partagee par les FX (plan slew P0).
//
// Le changement d'etat d'un FX ne commute jamais sec : la valeur glisse
// vers la cible en `slewFrames` frames (courbe de transition a la helisert,
// nuance "liquide" de filtre sur le trigger). Par defaut 128 frames (le
// kRampFrames historique de LiveRepeat) : les comportements existants sont
// preserves bit a bit tant que setSlewFrames() n'est pas appele.
//
// Deterministe et sans allocation : utilisable dans le callback audio.
class RampGain {
public:
    static constexpr std::uint32_t kDefaultFrames = 128;

    // Regle la duree des rampes suivantes (1..48000 frames, soit ~1 s max
    // a 48 kHz). Ne touche jamais une rampe en cours.
    void setSlewFrames(std::uint32_t frames) {
        slewFrames_ = std::clamp<std::uint32_t>(frames, 1U, 48000U);
    }

    std::uint32_t slewFrames() const { return slewFrames_; }

    // Lance une rampe de la valeur courante vers `target`, sans saut.
    void setTarget(float target) {
        target_ = std::clamp(target, 0.0f, 1.0f);
        step_ = (target_ - value_) / static_cast<float>(slewFrames_);
        remaining_ = slewFrames_;
    }

    // Avance la rampe d'une frame et retourne la valeur courante.
    // Ordre identique a l'ancien code LiveRepeat : on avance PUIS on lit.
    float tick() {
        if (remaining_ > 0) {
            value_ += step_;
            --remaining_;
            if (remaining_ == 0) value_ = target_;
        }
        return value_;
    }

    float get() const { return value_; }

private:
    std::uint32_t slewFrames_ = kDefaultFrames;
    float value_ = 0.0f;
    float target_ = 0.0f;
    float step_ = 0.0f;
    std::uint32_t remaining_ = 0;
};
