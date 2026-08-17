#include "granular.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

void GrainCloud::start(PcmView pcm, std::size_t rangeStart,
                       std::size_t rangeEnd, float userSpeed,
                       std::uint32_t seed) {
    const std::size_t frameCount = pcm.frameCount();
    rangeStart_ = std::min(rangeStart, frameCount);
    rangeEnd_ = std::min(rangeEnd, frameCount);
    if (rangeEnd_ < rangeStart_ + 2) {
        // Plage vide : le nuage reste inactif et silencieux.
        rangeEnd_ = rangeStart_;
        pcm_ = pcm;
        active_ = false;
        fadingOut_ = false;
        for (Grain& grain : grains_) grain.active = false;
        return;
    }
    pcm_ = pcm;
    userSpeed_ = std::clamp(userSpeed, kMinUserSpeed, kMaxUserSpeed);
    lcg_ = seed == 0 ? 0x9E3779B9U : seed;
    for (Grain& grain : grains_) {
        grain.active = false;
    }
    // Un grain toutes les ~22 ms a la frequence d'echantillonnage source
    // (densite bornee) ; echantillonnage degenere => 1 frame.
    spawnEveryFrames_ =
        std::max<std::size_t>(1, pcm.sampleRate > 0 ? pcm.sampleRate / 45U : 1);
    framesSinceSpawn_ = 0;
    masterFade_ = 1.0f;
    fadingOut_ = false;
    active_ = true;
    // Fondu de sortie ~10 ms, applique par frame de sortie. Plafonne a 1
    // pour les frequences d'echantillonnage degenerees (sinon le fondu
    // s'effondrerait en une frame et cliquerait).
    const float fadeFrames =
        0.010f * static_cast<float>(pcm.sampleRate > 0 ? pcm.sampleRate : 44100);
    fadeStep_ =
        std::min(1.0f, fadeFrames > 0.0f ? 1.0f / fadeFrames : 1.0f);
}

void GrainCloud::stop() {
    if (active_ && !fadingOut_) {
        fadingOut_ = true;
    }
}

std::size_t GrainCloud::activeGrainCount() const {
    std::size_t count = 0;
    for (const Grain& grain : grains_) {
        if (grain.active) ++count;
    }
    return count;
}

std::uint32_t GrainCloud::nextLcg(std::uint32_t& state) {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

float GrainCloud::hann(std::size_t index, std::size_t size) {
    if (size == 0) return 1.0f;
    return static_cast<float>(
        0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(index) /
                              static_cast<double>(size))));
}

void GrainCloud::spawnGrain() {
    if (rangeEnd_ <= rangeStart_) return;
    const std::size_t rangeLength = rangeEnd_ - rangeStart_;
    // Longueur de grain bornee : 30 a 150 ms de la source, jamais plus que
    // la plage elle-meme.
    const std::size_t minLen =
        std::max<std::size_t>(2, pcm_.sampleRate > 0 ? pcm_.sampleRate / 33U : 1);
    const std::size_t maxLen =
        std::max<std::size_t>(minLen, pcm_.sampleRate > 0
                                          ? static_cast<std::size_t>(
                                                pcm_.sampleRate) *
                                                3U / 20U
                                          : 1U);
    const std::size_t length = std::min(
        rangeLength, minLen + nextLcg(lcg_) % (maxLen - minLen + 1U));
    // Dispersion : position uniforme dans [rangeStart, rangeEnd - length).
    const std::size_t span = rangeLength - length;
    const std::size_t offset = span == 0 ? 0 : nextLcg(lcg_) % span;
    // Enveloppe : ~10 % de chaque cote, au moins 4 frames, au plus 2000.
    const std::size_t envelope =
        std::clamp<std::size_t>(length / 10U, 4, 2000);

    for (Grain& grain : grains_) {
        if (!grain.active) {
            grain.sourcePosF = static_cast<float>(offset);
            grain.length = length;
            grain.played = 0;
            grain.envelope = envelope;
            grain.active = true;
            return;
        }
    }
    // Les kMaxGrains slots sont occupes : le grain est abandonne (densite
    // plafonnee, jamais plus de kMaxGrains simultanes).
}

void GrainCloud::render(float* outLeft, float* outRight, int numFrames) {
    if (outLeft == nullptr || outRight == nullptr || numFrames <= 0) return;
    if (!active_ || pcm_.samples == nullptr || pcm_.sampleRate == 0) return;

    for (int frame = 0; frame < numFrames; ++frame) {
        if (!fadingOut_) {
            ++framesSinceSpawn_;
            if (framesSinceSpawn_ >= spawnEveryFrames_) {
                framesSinceSpawn_ = 0;
                spawnGrain();
            }
        }

        float mix = 0.0f;
        bool anyActive = false;
        const std::size_t channels = pcm_.channels > 0 ? pcm_.channels : 1U;
        for (Grain& grain : grains_) {
            if (!grain.active) continue;
            if (grain.played >= grain.length) {
                grain.active = false;
                continue;
            }
            anyActive = true;
            // Position source plafonnee a la plage assignee : a vitesse > 1
            // (E4 jusqu'a 400 %), le grain ne doit JAMAIS lire au-dela de
            // la fin de la plage (les slices voisines ne sont pas sa matiere).
            const std::size_t rangeLength = rangeEnd_ - rangeStart_;
            std::size_t position = static_cast<std::size_t>(grain.sourcePosF);
            position = std::min(position, rangeLength - 1U);
            const std::size_t source =
                rangeStart_ + position;
            const float sample = static_cast<float>(
                pcm_.samples[source * channels]);
            const float envelope =
                grain.played < grain.envelope
                    ? hann(grain.played, grain.envelope * 2U)
                    : (grain.played > grain.length - grain.envelope
                           ? hann(grain.length - grain.played,
                                  grain.envelope * 2U)
                           : 1.0f);
            mix += sample * envelope;
            grain.sourcePosF += userSpeed_;
            ++grain.played;
        }

        if (fadingOut_) {
            masterFade_ -= fadeStep_;
            if (masterFade_ <= 0.0f) {
                masterFade_ = 0.0f;
                fadingOut_ = false;
                active_ = false;
                for (Grain& grain : grains_) grain.active = false;
                anyActive = false;
            }
        }
        // Le nuage tourne tant que stop() n'a pas ete appele : les grains
        // continuent de naitre a densite bornee (les slots vides sont
        // reutilises). Seul le fondu de sortie l'eteint.
        (void)anyActive;
        outLeft[frame] += mix * masterFade_;
        outRight[frame] += mix * masterFade_;
    }
}
