#pragma once
#include "FirConvolver.h"
#include <array>
#include <cstddef>

namespace pulsefx {

struct HrtfProfile {
    static constexpr std::size_t kMaxTaps = FirConvolver::kMaxTaps;
    std::array<float, kMaxTaps> leftToLeft{};
    std::array<float, kMaxTaps> leftToRight{};
    std::array<float, kMaxTaps> rightToLeft{};
    std::array<float, kMaxTaps> rightToRight{};
    std::size_t taps{1};
};

// PulseFX perceptual externalizer. It retains the measured/analytic HRTF core,
// then protects low-frequency anchoring and centered content while adding a
// small, spectrally damped binaural early-reflection field. All storage is
// fixed-size and processStereo() performs no allocation or blocking work.
class SpatialSurround {
public:
    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void setProfile(const HrtfProfile& profile) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

    // Safe fallback only. Production-quality spatialization should load
    // measured HRIR data into HrtfProfile outside the realtime callback.
    static HrtfProfile makeDefaultProfile(float sampleRate) noexcept;

private:
    static constexpr std::size_t kReflectionBufferSize = 8192;
    static constexpr std::size_t kReflectionCount = 3;

    float readReflection(const std::array<float, kReflectionBufferSize>& buffer, std::size_t delay) const noexcept;

    FirConvolver leftToLeft_{};
    FirConvolver leftToRight_{};
    FirConvolver rightToLeft_{};
    FirConvolver rightToRight_{};

    std::array<float, kReflectionBufferSize> reflectionLeft_{};
    std::array<float, kReflectionBufferSize> reflectionRight_{};
    std::array<std::size_t, kReflectionCount> reflectionDelays_{};
    std::size_t reflectionWriteIndex_{0};

    float dryLowLeft_{0.0f};
    float dryLowRight_{0.0f};
    float wetLowLeft_{0.0f};
    float wetLowRight_{0.0f};
    float reflectionDampedLeft_{0.0f};
    float reflectionDampedRight_{0.0f};

    float lowAnchorCoeff_{0.0f};
    float reflectionDampingCoeff_{0.0f};
    float amountTarget_{0.0f};
    float amountCurrent_{0.0f};
    float smoothing_{0.002f};
};

} // namespace pulsefx
