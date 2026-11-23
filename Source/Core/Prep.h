/*
    Mac and Cheese - PREP: fix the mix before it is matched.

    Unclump ("dynamic match"): mid and side are split into 5 bands (Linkwitz-Riley, phase-compensated so the
    bands sum flat). Each band's level is followed *relative to the rest of the mix* (everything but that band),
    so song dynamics are left alone and a swelling band isn't hidden by the mix swelling with it.
    Learning stores where that relative level usually sits (percentiles) in the track and in a reference;
    when the track's band climbs further above its usual level than the reference's ever does (mud building
    up, harsh cymbal hits), it is pulled down to the reference's spread. Cut-only unless Max boost (0-6 dB) allows lifting a band.

    Mono base: side high-pass (24 dB/oct) = mono low end below the chosen frequency.
    The same class runs live and offline (to analyse, and to match the master on the prepped track).
*/
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace mm
{

constexpr int prepBands = 5, prepPoints = 5;
constexpr std::array<float, prepBands - 1> prepCrossovers { 120.0f, 500.0f, 2500.0f, 5000.0f };
constexpr std::array<float, prepPoints> prepPercentiles { 0.5f, 0.75f, 0.9f, 0.95f, 0.99f };

// band level relative to the mix (dB) at prepPercentiles, for [mid/side][band]
struct BandStats
{
    std::array<std::array<std::array<float, prepPoints>, prepBands>, 2> q {};
    bool valid = false;
};

struct PrepSettings
{
    float amount = 0.0f;                      // Unclump 0..1
    std::array<float, prepBands> bands { 1, 1, 1, 1, 1 }; // 1 = match the reference, up to 2
    float maxBoostDb = 0.0f;                  // 0 = cut only
    float monoHz = 0.0f;                      // < 20 = off
    bool active() const noexcept { return amount > 0.0f || monoHz >= 20.0f; }
};

class Prep
{
public:
    void prepare (double rate, int maxBlock)
    {
        sr = rate;
        const juce::dsp::ProcessSpec spec { rate, (juce::uint32) juce::jmax (1, maxBlock), 2 };
        for (size_t i = 0; i < split.size(); ++i)
        {
            split[i].prepare (spec);
            split[i].setCutoffFrequency (prepCrossovers[i]);
        }
        for (auto& a : align) { a.prepare (spec); a.setType (juce::dsp::LinkwitzRileyFilterType::allpass); }
        // band k is aligned with the splits above it: k=0 -> f2,f3,f4 ; k=1 -> f3,f4 ; k=2 -> f4
        const int f[6] = { 1, 2, 3, 2, 3, 3 };
        for (int i = 0; i < 6; ++i) align[(size_t) i].setCutoffFrequency (prepCrossovers[(size_t) f[i]]);
        for (auto& h : monoHp) { h.prepare ({ rate, spec.maximumBlockSize, 1 }); h.setType (juce::dsp::StateVariableTPTFilterType::highpass); }
        mix.reset (rate, 0.02);
        mix.setCurrentAndTargetValue (0.0f);
        env = {};
        mixEnv = 0.0f;
        gainTarget = {};
        gainLin.fill ({ 1, 1, 1, 1, 1 });
        reduction = {};
        boost = {};
    }

    // track and reference statistics -> mapping tables (call before processing; not real time)
    void setStats (const BandStats& track, const BandStats& ref)
    {
        has = track.valid && ref.valid;
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < prepBands; ++b)
                for (int p = 0; p < prepPoints; ++p)
                {
                    auto& t = track.q[(size_t) c][(size_t) b];
                    auto& r = ref.q[(size_t) c][(size_t) b];
                    x[(size_t) c][(size_t) b][(size_t) p] = t[(size_t) p] + 1e-3f * (float) p; // strictly increasing
                    y[(size_t) c][(size_t) b][(size_t) p] = t[0] + juce::jmax (0.0f, r[(size_t) p] - r[0]);
                }
    }

    // in place, mid and side
    void process (float* m, float* s, int n, const PrepSettings& set) noexcept
    {
        mix.setTargetValue (set.active() ? 1.0f : 0.0f);
        if (! mix.isSmoothing() && mix.getTargetValue() == 0.0f)
            return;
        for (auto& h : monoHp) h.setCutoffFrequency (juce::jmax (20.0f, set.monoHz));
        const bool mono = set.monoHz >= 20.0f;
        std::array<std::array<float, 2>, prepBands> coef; // attack, release per band (same as analyse())
        for (int b = 0; b < prepBands; ++b)
            coef[(size_t) b] = { tc (attackMs[(size_t) b]), tc (releaseMs[(size_t) b]) };
        const float mixA = tc (50.0f), mixR = tc (400.0f);

        for (int i = 0; i < n; ++i)
        {
            float bands[2][prepBands];
            float in[2] = { m[i], s[i] };
            if (mono)
                in[1] = monoHp[1].processSample (0, monoHp[0].processSample (0, in[1]));
            const float p = in[0] * in[0] + in[1] * in[1];
            mixEnv += (p > mixEnv ? mixA : mixR) * (p - mixEnv);
            for (int c = 0; c < 2; ++c)
                splitSample (c, in[c], bands[c]);

            if (++ctl >= 16) // control rate: detectors -> target gains
            {
                ctl = 0;
                for (int c = 0; c < 2; ++c)
                    for (int b = 0; b < prepBands; ++b)
                    {
                        float g = 0.0f;
                        if (has && set.amount > 0.0f)
                        {
                            const float rel = relDb (env[(size_t) c][(size_t) b], mixEnv);
                            g = juce::jlimit (-18.0f, set.maxBoostDb, map (c, b, rel) * set.amount * set.bands[(size_t) b]);
                        }
                        gainTarget[(size_t) c][(size_t) b] = juce::Decibels::decibelsToGain (g);
                        reduction[(size_t) b] = juce::jmax (reduction[(size_t) b], -g);
                        boost[(size_t) b] = juce::jmax (boost[(size_t) b], g);
                    }
            }
            float out[2] = { 0, 0 };
            for (int c = 0; c < 2; ++c)
                for (int b = 0; b < prepBands; ++b)
                {
                    const float v = bands[c][b];
                    auto& e = env[(size_t) c][(size_t) b];
                    e += (v * v > e ? coef[(size_t) b][0] : coef[(size_t) b][1]) * (v * v - e);
                    auto& gl = gainLin[(size_t) c][(size_t) b];
                    gl += 0.01f * (gainTarget[(size_t) c][(size_t) b] - gl); // de-zipper
                    out[c] += v * gl;
                }
            const float w = mix.getNextValue();
            m[i] += w * (out[0] - m[i]);
            s[i] += w * (out[1] - s[i]);
        }
    }

    // max reduction per band (dB) since the last call
    std::array<float, prepBands> popReduction() noexcept { auto r = reduction; reduction = {}; return r; }
    std::array<float, prepBands> popBoost() noexcept { auto r = boost; boost = {}; return r; }

    // offline: band-level statistics of a mid/side signal (same detectors as live)
    static BandStats analyse (const std::vector<float>& mid, const std::vector<float>& side, double rate)
    {
        Prep p;
        p.prepare (rate, 512);
        const int hop = juce::jmax (1, (int) (rate * 0.01));
        std::vector<float> mixDb;
        std::array<std::array<std::vector<float>, prepBands>, 2> rel;
        for (size_t i = 0; i < mid.size(); ++i)
        {
            float bands[2][prepBands];
            const float in[2] = { mid[i], i < side.size() ? side[i] : 0.0f };
            const float pw = in[0] * in[0] + in[1] * in[1];
            p.mixEnv += (pw > p.mixEnv ? p.tc (50.0f) : p.tc (400.0f)) * (pw - p.mixEnv);
            for (int c = 0; c < 2; ++c)
            {
                p.splitSample (c, in[c], bands[c]);
                for (int b = 0; b < prepBands; ++b)
                {
                    auto& e = p.env[(size_t) c][(size_t) b];
                    const float v2 = bands[c][b] * bands[c][b];
                    e += (v2 > e ? p.tc (attackMs[(size_t) b]) : p.tc (releaseMs[(size_t) b])) * (v2 - e);
                }
            }
            if ((int) i % hop == 0)
            {
                mixDb.push_back (10.0f * std::log10 (p.mixEnv + 1e-12f));
                for (int c = 0; c < 2; ++c)
                    for (int b = 0; b < prepBands; ++b)
                        rel[(size_t) c][(size_t) b].push_back (relDb (p.env[(size_t) c][(size_t) b], p.mixEnv));
            }
        }
        BandStats st;
        if (mixDb.empty())
            return st;
        const float gate = *std::max_element (mixDb.begin(), mixDb.end()) - 40.0f; // ignore silence
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < prepBands; ++b)
            {
                std::vector<float> v;
                for (size_t k = 0; k < mixDb.size(); ++k)
                    if (mixDb[k] > gate) v.push_back (rel[(size_t) c][(size_t) b][k]);
                if (v.empty()) return st;
                std::sort (v.begin(), v.end());
                for (int q = 0; q < prepPoints; ++q)
                    st.q[(size_t) c][(size_t) b][(size_t) q] = v[(size_t) ((v.size() - 1) * prepPercentiles[(size_t) q])];
            }
        st.valid = true;
        return st;
    }

private:
    static constexpr std::array<float, prepBands> attackMs { 50, 30, 20, 10, 5 }, releaseMs { 200, 150, 120, 100, 80 };

    // band level relative to the rest of the mix (floored at 5 % of the mix: the envelopes have different speeds)
    static float relDb (float band, float mixPower) noexcept
    {
        return 10.0f * std::log10 ((band + 1e-12f) / (juce::jmax (mixPower - band, 0.05f * mixPower) + 1e-12f));
    }

    float tc (float ms) const noexcept { return 1.0f - std::exp (-1000.0f / (ms * (float) sr)); }

    void splitSample (int c, float x, float* out) noexcept
    {
        float lo, hi = x;
        for (int k = 0; k < prepBands - 1; ++k)
        {
            split[(size_t) k].processSample (c, hi, lo, hi);
            out[k] = lo;
        }
        out[prepBands - 1] = hi;
        out[0] = align[2].processSample (c, align[1].processSample (c, align[0].processSample (c, out[0])));
        out[1] = align[4].processSample (c, align[3].processSample (c, out[1]));
        out[2] = align[5].processSample (c, out[2]);
    }

    // track relative level -> gain (dB): pull the track's spread above its median down to the reference's
    float map (int c, int b, float rel) const noexcept
    {
        const auto& xs = x[(size_t) c][(size_t) b];
        const auto& ys = y[(size_t) c][(size_t) b];
        if (rel <= xs[0]) return 0.0f;
        for (int p = 1; p < prepPoints; ++p)
            if (rel <= xs[(size_t) p])
            {
                const float t = (rel - xs[(size_t) p - 1]) / (xs[(size_t) p] - xs[(size_t) p - 1]);
                return ys[(size_t) p - 1] + t * (ys[(size_t) p] - ys[(size_t) p - 1]) - rel;
            }
        return ys[prepPoints - 1] - xs[prepPoints - 1];
    }

    double sr = 44100.0;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, prepBands - 1> split;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 6> align;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> monoHp;
    juce::SmoothedValue<float> mix;
    std::array<std::array<float, prepBands>, 2> env {}, gainTarget {}, gainLin {};
    std::array<std::array<std::array<float, prepPoints>, prepBands>, 2> x {}, y {};
    std::array<float, prepBands> reduction {}, boost {};
    float mixEnv = 0.0f;
    int ctl = 0;
    bool has = false;
};

} // namespace mm
