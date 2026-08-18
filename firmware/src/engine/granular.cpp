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
    // Un grain toutes les ~22 ms / densite a la frequence d'echantillonnage
    // source (cadence bornee) ; echantillonnage degenere => 1 frame. La
    // cadence est recalculee a CHAQUE spawn : setDensity() est donc un
    // reglage live, sans redemarrer le nuage.
    spawnEveryFrames_ = 1;
    framesSinceSpawn_ = 0;
    spawnCount_ = 0;
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

void GrainCloud::hardStop() {
    active_ = false;
    fadingOut_ = false;
    masterFade_ = 1.0f;
    for (Grain& grain : grains_) {
        grain.active = false;
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

float GrainCloud::semitoneMul(float semitones) {
    return static_cast<float>(std::pow(2.0, static_cast<double>(semitones) / 12.0));
}

void GrainCloud::spawnGrain() {
    if (rangeEnd_ <= rangeStart_) return;
    const std::size_t rangeLength = rangeEnd_ - rangeStart_;
    // Longueur de grain bornee : 30 a 150 ms de la source (echelle
    // reglable), jamais plus que la plage elle-meme, jamais moins de 2.
    const std::size_t minLen =
        std::max<std::size_t>(2, pcm_.sampleRate > 0 ? pcm_.sampleRate / 33U : 1);
    const std::size_t maxLen =
        std::max<std::size_t>(minLen, pcm_.sampleRate > 0
                                          ? static_cast<std::size_t>(
                                                pcm_.sampleRate) *
                                                3U / 20U
                                          : 1U);
    std::size_t length = minLen + nextLcg(lcg_) % (maxLen - minLen + 1U);
    length = static_cast<std::size_t>(
        std::round(static_cast<float>(length) * grainSizeScale_));
    length = std::clamp<std::size_t>(length, 2, rangeLength);
    // Dispersion : position uniforme dans [rangeStart, rangeEnd - length).
    const std::size_t span = rangeLength - length;
    const std::size_t offset = span == 0 ? 0 : nextLcg(lcg_) % span;
    // Enveloppe : ~10 % de chaque cote, au moins 4 frames, au plus 2000.
    const std::size_t envelope =
        std::clamp<std::size_t>(length / 10U, 4, 2000);

    // Hauteur du grain selon le mode. Les tirages LCG supplementaires de
    // PITCH sont conditionnes au mode : le determinisme du mode Cloud
    // (comportement historique) reste byte-identique.
    float pitchStart = 1.0f;
    float pitchEnd = 1.0f;
    switch (mode_) {
        case GrainMode::Pitch: {
            const float range = static_cast<float>(pitchRangeSemitones_);
            if (range > 0.0f) {
                const std::uint32_t steps =
                    static_cast<std::uint32_t>(2.0f * range) + 1U;
                const float semitones =
                    static_cast<float>(nextLcg(lcg_) % steps) - range;
                pitchStart = semitoneMul(semitones);
                pitchEnd = pitchStart;
            }
            break;
        }
        case GrainMode::Rise: {
            const float range = static_cast<float>(pitchRangeSemitones_);
            if (range > 0.0f) {
                pitchStart = semitoneMul(-range);
                pitchEnd = semitoneMul(range);
            }
            break;
        }
        case GrainMode::Cloud:
        default:
            break;
    }

    for (Grain& grain : grains_) {
        if (!grain.active) {
            grain.sourcePosF = static_cast<float>(offset);
            grain.length = length;
            grain.played = 0;
            grain.envelope = envelope;
            grain.pitchMul = pitchStart;
            grain.pitchMulStart = pitchStart;
            grain.pitchMulEnd = pitchEnd;
            grain.active = true;
            ++spawnCount_;
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
                // Cadence recalculee a chaque spawn : la densite est un
                // reglage live (~22 ms / densite, bornee).
                const float rate = static_cast<float>(
                    pcm_.sampleRate > 0 ? pcm_.sampleRate : 1U);
                spawnEveryFrames_ = std::max<std::size_t>(
                    1, static_cast<std::size_t>(rate / (45.0f * density_)));
            }
        }

        float mix = 0.0f;
        bool anyActive = false;
        const std::size_t channels = pcm_.channels > 0 ? pcm_.channels : 1U;
        const std::size_t rangeLength = rangeEnd_ - rangeStart_;
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
            const float clampedPos =
                std::min(grain.sourcePosF, static_cast<float>(rangeLength - 1U));
            // Lecture interpolee (necessaire pour les hauteurs par grain) :
            // i0 dans [0, rangeLength-2], i1 = i0+1 dans la plage.
            const std::size_t i0 =
                std::min(static_cast<std::size_t>(clampedPos), rangeLength - 2U);
            const float frac = clampedPos - static_cast<float>(i0);
            const float s0 = static_cast<float>(
                pcm_.samples[(rangeStart_ + i0) * channels]);
            const float s1 = static_cast<float>(
                pcm_.samples[(rangeStart_ + i0 + 1U) * channels]);
            const float sample = s0 + (s1 - s0) * frac;
            const float envelope =
                grain.played < grain.envelope
                    ? hann(grain.played, grain.envelope * 2U)
                    : (grain.played > grain.length - grain.envelope
                           ? hann(grain.length - grain.played,
                                  grain.envelope * 2U)
                           : 1.0f);
            mix += sample * envelope;
            // Glide de hauteur (mode Rise) : interpolation lineaire de
            // pitchMulStart -> pitchMulEnd sur la vie du grain.
            if (grain.length > 1U &&
                grain.pitchMulStart != grain.pitchMulEnd) {
                grain.pitchMul =
                    grain.pitchMulStart +
                    (grain.pitchMulEnd - grain.pitchMulStart) *
                        (static_cast<float>(grain.played) /
                         static_cast<float>(grain.length - 1U));
            }
            grain.sourcePosF += userSpeed_ * grain.pitchMul;
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
