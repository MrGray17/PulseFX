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
// protects low-frequency anchoring and centered content, and adds a small,
// spectrally damped binaural early-reflection field. HRTF changes use a fixed-
// storage crossfade so calibration/headphone switches do not hard-reset the
// audible spatial field.
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
    static constexpr std::size_t kHrtfBankCount = 3;

    struct HrtfBank {
        FirConvolver leftToLeft{};
        FirConvolver leftToRight{};
        FirConvolver rightToLeft{};
        FirConvolver rightToRight{};

        void reset() noexcept {
            leftToLeft.reset();
            leftToRight.reset();
            rightToLeft.reset();
            rightToRight.reset();
        }
    };

    void loadProfile(std::size_t bankIndex, const HrtfProfile& profile) noexcept;
    void startProfileTransition(std::size_t targetBank) noexcept;
    void processHrtfBank(
        std::size_t bankIndex,
        float dryLeft,
        float dryRight,
        float& wetLeft,
        float& wetRight) noexcept;
    float readReflection(const std::array<float, kReflectionBufferSize>& buffer, std::size_t delay) const noexcept;

    std::array<HrtfBank, kHrtfBankCount> hrtfBanks_{};
    std::size_t activeHrtfBank_{0};
    std::size_t targetHrtfBank_{1};
    std::size_t pendingHrtfBank_{2};
    bool profileInitialized_{false};
    bool profileTransitionActive_{false};
    bool pendingProfileReady_{false};
    float profileTransition_{1.0f};
    float profileTransitionStep_{1.0f};

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
