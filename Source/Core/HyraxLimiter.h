/*
    Mac and Cheese - real-time port of Matchering's "Hyrax" brickwall limiter (GPLv3).
    The original (limiter/hyrax.py) is non-causal (centered max filter + filtfilt);
    here the same gain envelope is computed with a fixed lookahead, reported as latency.
*/
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "TruePeak.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mm
{

struct HyraxSettings
{
    double attackMs = 1.0, holdMs = 1.0, releaseMs = 3000.0;
    double attackFilterCoef = -2.0, holdFilterHz = 7.0, releaseFilterCoef = 800.0;
    float threshold = (32768.0f - 61.0f) / 32768.0f;
};

class HyraxLimiter
{
public:
    using Settings = HyraxSettings;

    void prepare (double sampleRate, int numChannels, Settings s = Settings())
    {
        settings = s;
        const int attack = std::max (1, (int) (sampleRate * s.attackMs * 1e-3));
        const int attackOdd = (attack & 1) ? attack : attack + 1;      // utils.make_odd
        window = 2 * attackOdd - 1;                                    // maximum_filter1d size
        half = attackOdd - 1;
        hold = std::max (1, (int) (sampleRate * s.holdMs * 1e-3));

        // filtfilt of a one-pole == symmetric exponential kernel
        const double c = std::exp (s.attackFilterCoef / (double) attack);
        kHalf = (int) std::ceil (std::log (1e-5) / std::log (c));
        kernel.assign ((size_t) (2 * kHalf + 1), 0.0f);
        double sum = 0.0;
        for (int k = -kHalf; k <= kHalf; ++k)
            sum += kernel[(size_t) (k + kHalf)] = (float) std::pow (c, std::abs (k));
        for (auto& v : kernel)
            v = (float) (v / sum);

        latency = half + kHalf;
        totalLatency = latency + TruePeakDetector::getDelaySamples();

        holdLp.design (s.holdFilterHz, sampleRate);
        releaseLp.design (s.releaseFilterCoef / s.releaseMs, sampleRate);

        ghc.init (std::max (latency + tpReach + 1, window));
        sRing.init (std::max (2 * kHalf + 1, kHalf + hold));
        audio.resize ((size_t) numChannels);
        for (auto& a : audio)
            a.init (totalLatency + 1);
        detectors.assign ((size_t) numChannels, TruePeakDetector());
        reset();
    }

    void reset()
    {
        ghc.clear();
        sRing.clear();
        for (auto& a : audio)
            a.clear();
        for (auto& d : detectors)
            d.reset();
        holdLp.reset();
        releaseLp.reset();
        minGain = 1.0f;
    }

    int getLatencySamples() const noexcept { return totalLatency; }

    // Ceiling (linear) and detection mode can change at any time.
    void setCeiling (float linear) noexcept { settings.threshold = linear; }
    void setTruePeak (bool on) noexcept { truePeak = on; }

    // Processes in place. Always delays by getLatencySamples(); when !enabled the gain is 1.
    void process (float* const* ch, int numCh, int n, bool enabled)
    {
        numCh = std::min (numCh, (int) audio.size());
        const float thr = settings.threshold;
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                auto& det = detectors[(size_t) c];
                const float tp = det.process (ch[c][i]);
                peak = std::max (peak, truePeak ? tp : det.lastSamplePeak());
                audio[(size_t) c].push (ch[c][i]);
            }
            // dsp.rectify + flip(1/x): 0 = no reduction
            ghc.push (peak > thr ? 1.0f - thr / peak : 0.0f);

            // attack stage: centered max filter (centre = now - half)
            sRing.push (juce::FloatVectorOperations::findMaximum (ghc.last (window), window));

            // time being output: t = now - half - kHalf
            const float* sw = sRing.last (2 * kHalf + 1);
            float att = 0.0f;
            for (int k = 0; k < 2 * kHalf + 1; ++k)
                att += kernel[(size_t) k] * sw[k];

            // hold / release stage (causal in hyrax.py)
            const float held = juce::FloatVectorOperations::findMaximum (sRing.last (kHalf + hold), hold);
            const float holdOut = holdLp.process (held);
            const float relOut = releaseLp.process (std::max (held, holdOut));
            const float release = std::max (holdOut, relOut);

            // true peak: the waveform between samples depends on the gain of the neighbours too, so take the
            // largest reduction needed within the true-peak filter's reach around the output sample
            float need = 0.0f;
            for (int k = latency - tpReach; k <= latency + tpReach; ++k)
                need = std::max (need, ghc.ago (k));
            const float g = enabled ? 1.0f - std::max ({ need, att, release }) : 1.0f;
            minGain = std::min (minGain, g);
            for (int c = 0; c < numCh; ++c)
                ch[c][i] = audio[(size_t) c].ago (totalLatency) * g;
        }
    }

    // Lowest gain since the last call (for metering).
    float popMinGain() noexcept
    {
        const float g = minGain;
        minGain = 1.0f;
        return g;
    }

private:
    struct MirrorRing
    {
        std::vector<float> buf;
        int size = 0, pos = 0;
        void init (int n) { size = n; buf.assign ((size_t) n * 2, 0.0f); pos = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        void push (float v) noexcept
        {
            buf[(size_t) pos] = v;
            buf[(size_t) (pos + size)] = v;
            if (++pos == size)
                pos = 0;
        }
        const float* last (int len) const noexcept
        {
            int start = pos - len;
            if (start < 0)
                start += size;
            return buf.data() + start;
        }
        float ago (int k) const noexcept
        {
            int i = pos - 1 - k;
            while (i < 0)
                i += size;
            return buf[(size_t) i];
        }
    };

    // scipy.signal.butter(1, fc, fs=sr) + lfilter
    struct OnePoleButter
    {
        float b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
        void design (double fc, double sr)
        {
            const double k = std::tan (juce::MathConstants<double>::pi * fc / sr);
            b0 = b1 = (float) (k / (1.0 + k));
            a1 = (float) ((k - 1.0) / (k + 1.0));
        }
        void reset() { x1 = y1 = 0; }
        float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    Settings settings;
    int window = 1, half = 0, hold = 1, kHalf = 0, latency = 0, totalLatency = 0;
    static constexpr int tpReach = 6; // = TruePeakDetector delay (half its interpolation filter)
    bool truePeak = true;
    std::vector<TruePeakDetector> detectors;
    std::vector<float> kernel;
    MirrorRing ghc, sRing;
    std::vector<MirrorRing> audio;
    OnePoleButter holdLp, releaseLp;
    float minGain = 1.0f;
};

} // namespace mm
