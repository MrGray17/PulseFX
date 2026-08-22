#pragma once

namespace pulsefx {

// Psychoacoustic bass support for transducers with limited low-frequency
// capability. The generated component is mono/centered and uses restrained
// harmonics of the extracted bass band rather than direct sub-bass gain.
class VirtualBassEnhancer {
public:
    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void setBassCapability(float capability) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

    float amount() const noexcept { return amountTarget_; }
    float bassCapability() const noexcept { return bassCapabilityTarget_; }

private:
    float processLowBand(float mono) noexcept;
    float processHarmonics(float lowBand) noexcept;
    void smoothControls() noexcept;

    float sampleRate_{48000.0f};
    float amountTarget_{0.0f};
    float amountCurrent_{0.0f};
    float bassCapabilityTarget_{1.0f};
    float bassCapabilityCurrent_{1.0f};
    float controlSmoothingCoeff_{1.0f};

    float low110_{0.0f};
    float low42_{0.0f};
    float squareDc_{0.0f};
    float harmonicLow90_{0.0f};
    float harmonicLow360_{0.0f};
    float envelope_{0.0f};

    float low110Coeff_{0.0f};
    float low42Coeff_{0.0f};
    float dcCoeff_{0.0f};
    float hp90Coeff_{0.0f};
    float lp360Coeff_{0.0f};
    float envelopeRelease_{0.0f};
};

} // namespace pulsefx
