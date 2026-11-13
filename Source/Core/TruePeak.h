/*
    MatchMaster - true-peak detector (4x oversampled, ITU-R BS.1770 style).
    Estimates the analogue peak between samples. Output is delayed by getDelaySamples().
*/
#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace mm
{

class TruePeakDetector
{
public:
    static constexpr int factor = 4, tapsPerPhase = 12, delay = tapsPerPhase / 2;

    TruePeakDetector()
    {
        // windowed-sinc low-pass at the original Nyquist, designed at 4x rate (odd length, symmetric)
        constexpr int len = factor * tapsPerPhase + 1;
        constexpr double centre = (len - 1) / 2.0, pi = 3.14159265358979323846;
        for (int p = 0; p < factor; ++p)
            for (int k = 0; k < tapsPerPhase; ++k)
            {
                const int j = p + factor * k;
                const double t = (j - centre) / factor;
                const double sinc = t == 0.0 ? 1.0 : std::sin (pi * t) / (pi * t);
                const double w = 0.42 - 0.5 * std::cos (2 * pi * j / (len - 1)) + 0.08 * std::cos (4 * pi * j / (len - 1));
                coef[(size_t) p][(size_t) k] = (float) (sinc * w);
            }
        reset();
    }

    static constexpr int getDelaySamples() { return delay; }

    void reset() { hist.fill (0.0f); pos = 0; }

    // Push one sample; returns the peak estimate (|x| and 3 in-between points) for the sample `delay` ago.
    float process (float x) noexcept
    {
        hist[(size_t) pos] = x;
        hist[(size_t) (pos + tapsPerPhase)] = x;
        pos = (pos + 1) % tapsPerPhase;
        const float* h = hist.data() + pos; // oldest .. newest
        float peak = 0.0f;
        for (int p = 0; p < factor; ++p)
        {
            float acc = 0.0f;
            for (int k = 0; k < tapsPerPhase; ++k)
                acc += coef[(size_t) p][(size_t) k] * h[tapsPerPhase - 1 - k];
            peak = std::max (peak, std::abs (acc));
        }
        samplePeak = std::abs (h[tapsPerPhase - 1 - delay]);
        return std::max (peak, samplePeak);
    }

    // |x| of the delayed sample from the last process() call (sample-peak mode).
    float lastSamplePeak() const noexcept { return samplePeak; }

    static float measure (const float* x, int n)
    {
        TruePeakDetector d;
        float peak = 0.0f;
        for (int i = 0; i < n + delay; ++i)
            peak = std::max (peak, d.process (i < n ? x[i] : 0.0f));
        return peak;
    }

private:
    std::array<std::array<float, tapsPerPhase>, factor> coef {};
    std::array<float, tapsPerPhase * 2> hist {};
    int pos = 0;
    float samplePeak = 0.0f;
};

} // namespace mm
