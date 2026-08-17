#pragma once

#include <cstddef>
#include <cstdint>

// Tampon de capture retrospective du mix global (plan 8, COMMIT / Skip Back).
//
// Le callback audio appelle record() en permanence : le mix recent est garde
// dans un anneau de `capacityFrames` frames stereo fourni par l'appelant
// (jamais alloue, jamais copie : dimensionne avec requiredBufferFrames,
// pret a vivre en PSRAM sur Teensy). Au moment du COMMIT (chemin de
// controle), extractWindow() fige une fenetre des dernieres `windowFrames`
// frames et la recopie en ordre continu dans les buffers de sortie fournis
// par l'appelant : le resultat devient une nouvelle matiere assignable.
//
// Non reentrant : record() et extractWindow() doivent etre serialises par
// l'appelant (le harness passe par le mutex du moteur audio).
class CaptureBuffer {
public:
    static constexpr std::size_t kDefaultWindowSeconds = 15;

    // Frames stereo necessaires pour garder windowSeconds d'historique.
    static constexpr std::size_t requiredBufferFrames(
        std::uint32_t sampleRate, std::size_t windowSeconds) {
        return static_cast<std::size_t>(sampleRate) * windowSeconds;
    }

    CaptureBuffer(std::uint32_t sampleRate, float* storageLeft,
                  float* storageRight, std::size_t capacityFrames);

    // Consigne `numFrames` frames du mix dans l'anneau. Aucune allocation,
    // aucune attente. Les pointeurs nuls et les tailles non positives sont
    // ignores.
    void record(const float* left, const float* right, int numFrames);

    // Fige les dernieres `windowFrames` frames (plafonnees a la capacite et
    // a l'historique reel) et les recopie en ordre chronologique dans
    // outLeft/outRight (capacite >= windowFrames attendue). Retourne le
    // nombre de frames copiees ; 0 si rien n'a ete enregistre ou si un
    // pointeur est nul.
    std::size_t extractWindow(float* outLeft, float* outRight,
                              std::size_t windowFrames);

    std::size_t recordedFrames() const { return recordedFrames_; }
    std::size_t capacityFrames() const { return capacityFrames_; }

private:
    std::uint32_t sampleRate_;
    float* storageL_;
    float* storageR_;
    std::size_t capacityFrames_;
    std::size_t writePos_ = 0;
    std::size_t recordedFrames_ = 0;  // plafonne a capacityFrames_
};
