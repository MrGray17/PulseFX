#pragma once

namespace pulsefx {

class FidelityEnhancer {
public:
    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

private:
    float amountTarget_{0.0f};
    float amountCurrent_{0.0f};
    float smoothing_{0.002f};
    float lowpassCoeff_{0.1f};
    float envelopeAttack_{0.01f};
    float envelopeRelease_{0.001f};
    float lowpassLeft_{0.0f};
    float lowpassRight_{0.0f};
    float envelope_{0.0f};
};

} // namespace pulsefx
