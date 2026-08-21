#pragma once

namespace pulsefx {

class Limiter {
public:
    void prepare(float sampleRate) noexcept;
    void setCeilingDb(float db) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;
    float gainReductionDb() const noexcept { return gainReductionDb_; }

private:
    float sampleRate_{48000.0f};
    float ceilingLinear_{0.89125094f}; // -1 dBFS
    float envelope_{1.0f};
    float gainReductionDb_{0.0f};
};

} // namespace pulsefx
