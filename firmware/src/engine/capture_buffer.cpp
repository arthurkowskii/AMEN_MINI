#include "capture_buffer.h"

#include <algorithm>
#include <cstring>

CaptureBuffer::CaptureBuffer(std::uint32_t sampleRate, float* storageLeft,
                             float* storageRight, std::size_t capacityFrames)
    : sampleRate_(sampleRate),
      storageL_(storageLeft),
      storageR_(storageRight),
      capacityFrames_(capacityFrames) {}

void CaptureBuffer::record(const float* left, const float* right,
                           int numFrames) {
    if (left == nullptr || right == nullptr || numFrames <= 0) return;
    if (storageL_ == nullptr || storageR_ == nullptr || capacityFrames_ == 0) {
        return;
    }
    for (int i = 0; i < numFrames; ++i) {
        storageL_[writePos_] = left[i];
        storageR_[writePos_] = right[i];
        ++writePos_;
        if (writePos_ >= capacityFrames_) {
            writePos_ = 0;
        }
        if (recordedFrames_ < capacityFrames_) {
            ++recordedFrames_;
        }
    }
}

std::size_t CaptureBuffer::extractWindow(float* outLeft, float* outRight,
                                         std::size_t windowFrames) {
    if (outLeft == nullptr || outRight == nullptr) return 0;
    const std::size_t available = std::min(recordedFrames_, capacityFrames_);
    if (available == 0) return 0;
    const std::size_t count = std::min(windowFrames, available);
    // L'anneau se lit en ordre chronologique : la frame la plus ancienne
    // retenue commence a writePos_ - count (mod capacityFrames_) lorsque
    // l'anneau est plein ; sinon en position 0.
    std::size_t readPos;
    if (recordedFrames_ >= capacityFrames_) {
        readPos = (writePos_ + capacityFrames_ - count) % capacityFrames_;
    } else {
        // Anneau pas encore plein : le contenu vit en position 0..n-1, la
        // fenetre des dernieres count frames commence en n - count.
        readPos = recordedFrames_ - count;
    }
    for (std::size_t i = 0; i < count; ++i) {
        outLeft[i] = storageL_[readPos];
        outRight[i] = storageR_[readPos];
        ++readPos;
        if (readPos >= capacityFrames_) readPos = 0;
    }
    return count;
}
