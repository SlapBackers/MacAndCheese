/*
    Mac and Cheese - clipper before the limiter.
    Port of ClipOnly2 by Chris Johnson / Airwindows (MIT licence, https://github.com/airwindows/airwindows).
    The clipping code is ClipOnly2's own "prefab chunk", unchanged; Mac and Cheese only scales the signal
    so the clip point lands where the Clip knob asks for it. Latency: one 44.1 kHz sample.
*/
#pragma once

#include <algorithm>
#include <cmath>

namespace mm
{

class ClipOnly2
{
public:
    void prepare (double sampleRate)
    {
        spacing = std::clamp ((int) std::floor (sampleRate / 44100.0), 1, 16);
        for (auto& c : ch)
            c = Channel();
    }

    int getLatencySamples() const noexcept { return spacing; }

    // x is normalised so the clip point is 1.0 (ClipOnly2 starts softening at -0.2 dB of it).
    float process (int channel, float x) noexcept
    {
        auto& c = ch[channel];
        double s = x;
        if (s > 4.0) s = 4.0;
        if (s < -4.0) s = -4.0;
        if (c.wasPosClip) { // current will be over
            if (s < c.lastSample) c.lastSample = 0.7058208 + (s * 0.2609148);
            else c.lastSample = 0.2491717 + (c.lastSample * 0.7390851);
        }
        c.wasPosClip = false;
        if (s > 0.9549925859) { c.wasPosClip = true; s = 0.7058208 + (c.lastSample * 0.2609148); }
        if (c.wasNegClip) { // current will be -over
            if (s > c.lastSample) c.lastSample = -0.7058208 + (s * 0.2609148);
            else c.lastSample = -0.2491717 + (c.lastSample * 0.7390851);
        }
        c.wasNegClip = false;
        if (s < -0.9549925859) { c.wasNegClip = true; s = -0.7058208 + (c.lastSample * 0.2609148); }
        c.intermediate[spacing] = s;
        s = c.lastSample; // latency is however many samples equals one 44.1k sample
        for (int i = spacing; i > 0; --i)
            c.intermediate[i - 1] = c.intermediate[i];
        c.lastSample = c.intermediate[0];
        return (float) s;
    }

private:
    struct Channel
    {
        double lastSample = 0.0;
        double intermediate[17] {}; // the original declares 16 but writes index `spacing` (up to 16)
        bool wasPosClip = false, wasNegClip = false;
    };
    Channel ch[2];
    int spacing = 1;
};

} // namespace mm
