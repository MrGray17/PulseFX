# PulseFX

PulseFX is an experimental Windows system-wide audio enhancement project focused on transparent DSP, high-quality dynamics, headphone correction, and spatial rendering.

> Status: early architecture bootstrap. The DSP core and desktop controller are being built first; the Windows APO integration is tracked separately and must be validated on a Windows SDK/WDK environment before it is considered production-ready.

## Principles

- Audio quality before feature count.
- Transparent bypass.
- No allocations or blocking locks in the realtime audio path.
- Loudness-matched A/B testing for every enhancement.
- Original implementation and visual identity; no proprietary Boom 3D code, assets, or DSP are copied.
