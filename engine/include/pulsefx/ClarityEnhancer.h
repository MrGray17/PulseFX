#pragma once

namespace pulsefx {

// Content-aware clarity enhancement. The detector is stereo-linked so the
// effect cannot pull the image sideways, and the enhancement backs off when
// the presence band is already dominant or during sharp transients.
class ClarityEnhancer {
public:
    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

private:
    void updateEnvelope(float level, float& envelope, float attack, float release) noexcept;

    float sampleRate_{48000.0f};
    float amountTarget_{0.0f};
    float amountCurrent_{0.0f};
    float amountSmoothing_{0.0f};

    float low250Coeff_{0.0f};
    float low900Coeff_{0.0f};
    float low5800Coeff_{0.0f};
    float detectorAttack_{0.0f};
    float detectorRelease_{0.0f};
    float fastAttack_{0.0f};
    float fastRelease_{0.0f};
    float slowAttack_{0.0f};
    float slowRelease_{0.0f};

    float leftLow250_{0.0f};
    float rightLow250_{0.0f};
    float leftLow900_{0.0f};
    float rightLow900_{0.0f};
    float leftLow5800_{0.0f};
    float rightLow5800_{0.0f};

    float broadbandEnvelope_{0.0f};
    float presenceEnvelope_{0.0f};
    float mudEnvelope_{0.0f};
    float fastEnvelope_{0.0f};
    float slowEnvelope_{0.0f};
};

} // namespace pulsefx
