/*
    matchmaster-cli: tests the MatchMaster engine outside a DAW.
      matchmaster-cli selftest
      matchmaster-cli match <your-track> <reference> <output.wav>
    Runs the same chain as the plugin: analysis -> M/S FIR EQ -> loudness -> Hyrax limiter -> reference level.
*/
#include <juce_audio_formats/juce_audio_formats.h>
#include "Core/Matcher.h"
#include "Core/HyraxLimiter.h"
#include <iostream>
#include <random>

namespace
{
mm::Config cfg;

// The plugin's real-time chain, run offline at `rate`.
juce::AudioBuffer<float> processChain (const juce::AudioBuffer<float>& in, double rate, const mm::MatchResult& r)
{
    const int n = in.getNumSamples();
    const int fs = mm::firSizeForRate (rate);
    auto midFir = mm::designFir (r.midCurve, r.curveSampleRate, r.curveFftSize, rate, fs, 1.0f, 24.0f);
    auto sideFir = mm::designFir (r.sideCurve, r.curveSampleRate, r.curveFftSize, rate, fs, 1.0f, 24.0f);

    std::vector<float> mid ((size_t) n), side ((size_t) n);
    const float* l = in.getReadPointer (0);
    const float* rr = in.getReadPointer (in.getNumChannels() > 1 ? 1 : 0);
    for (int i = 0; i < n; ++i)
    {
        mid[(size_t) i] = (l[i] + rr[i]) * 0.5f;
        side[(size_t) i] = mid[(size_t) i] - rr[i];
    }
    mid = mm::convolveSame (mid, midFir);
    side = mm::convolveSame (side, sideFir);

    mm::HyraxLimiter lim;
    lim.prepare (rate, 2);
    const int lat = lim.getLatencySamples();
    juce::AudioBuffer<float> out (2, n + lat);
    out.clear();
    const float g = (float) r.totalGain();
    for (int i = 0; i < n; ++i)
    {
        out.setSample (0, i, (mid[(size_t) i] + side[(size_t) i]) * g);
        out.setSample (1, i, (mid[(size_t) i] - side[(size_t) i]) * g);
    }
    lim.process (out.getArrayOfWritePointers(), 2, n + lat, true);

    juce::AudioBuffer<float> res (2, n);
    for (int ch = 0; ch < 2; ++ch)
        res.copyFrom (ch, 0, out, ch, lat, n);
    res.applyGain ((float) r.finalAmplitudeCoef);
    return res;
}

mm::MatchResult analyse (const juce::AudioBuffer<float>& track, double trackRate,
                         const juce::AudioBuffer<float>& ref, double refRate, mm::ReferenceProfile& refOut)
{
    refOut = mm::analyzeReference (mm::toStereoAtRate (ref, refRate, cfg.sampleRate), cfg);
    const auto tgt = mm::analyzeTarget (mm::toStereoAtRate (track, trackRate, cfg.sampleRate), cfg);
    return mm::match (tgt, refOut, cfg);
}

// Coloured, level-modulated stereo noise ("song" with quiet and loud sections).
juce::AudioBuffer<float> makeSong (double rate, double seconds, float tilt, float level, float width, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd;
    const int n = (int) (rate * seconds);
    juce::AudioBuffer<float> b (2, n);
    float lpL = 0, lpR = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        const float env = level * (0.35f + 0.65f * (std::fmod (t, 20.0) > 8.0 ? 1.0f : 0.3f)); // verse/chorus
        const float c = nd (rng), s = nd (rng) * width;
        lpL += tilt * ((c + s) - lpL);
        lpR += tilt * ((c - s) - lpR);
        b.setSample (0, i, lpL * env);
        b.setSample (1, i, lpR * env);
    }
    return b;
}

// Average |FFT| of the mid of the loudest pieces, in octave bands (dB).
std::vector<double> octaveBands (const juce::AudioBuffer<float>& b, double rate)
{
    auto st = mm::toStereoAtRate (b, rate, cfg.sampleRate);
    const auto t = mm::analyzeTarget (st, cfg);
    std::vector<double> bands;
    for (double f = 63; f < 16000; f *= 2)
    {
        double acc = 0;
        int cnt = 0;
        for (size_t k = 1; k < t.midSpectrum.size(); ++k)
        {
            const double fk = k * cfg.sampleRate / cfg.fftSize;
            if (fk >= f / std::sqrt (2.0) && fk < f * std::sqrt (2.0)) { acc += t.midSpectrum[k] * t.midSpectrum[k]; ++cnt; }
        }
        bands.push_back (10 * std::log10 (acc / std::max (1, cnt) + 1e-30));
    }
    return bands;
}

double loudRmsDb (const juce::AudioBuffer<float>& b, double rate)
{
    auto st = mm::toStereoAtRate (b, rate, cfg.sampleRate);
    return 20 * std::log10 (mm::analyzeTarget (st, cfg).matchRms);
}

int selftest()
{
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n";
        if (! ok) ++failures;
    };

    // 1. FIR design is an identity when the curve is flat (amount 0)
    {
        auto fir = mm::designFir ({ 1.0f, 1.0f }, 44100, 4096, 48000, 8192, 0.0f, 24.0f);
        float sum = 0; for (auto v : fir) sum += std::abs (v);
        check (std::abs (fir[4096] - 1.0f) < 1e-3f && sum < 1.01f, "flat FIR is a delayed impulse");
    }

    // 2. LOWESS reproduces a straight line
    {
        std::vector<double> x (500), y (500);
        for (int i = 0; i < 500; ++i) { x[(size_t) i] = i / 499.0; y[(size_t) i] = 3 * x[(size_t) i] + 1; }
        auto f = mm::lowess (x, y, 0.0375, 0.001);
        double err = 0; for (int i = 0; i < 500; ++i) err = std::max (err, std::abs (f[(size_t) i] - y[(size_t) i]));
        check (err < 1e-6, "LOWESS fits a line exactly (err " + juce::String (err, 9) + ")");
    }

    // 3. Full match: dark, quiet track at 48 kHz vs bright, loud reference at 44.1 kHz
    const double trackRate = 48000, refRate = 44100;
    auto track = makeSong (trackRate, 40, 0.08f, 0.05f, 0.2f, 1);
    auto ref = makeSong (refRate, 45, 0.6f, 0.30f, 0.5f, 2);

    mm::ReferenceProfile refProf;
    auto res = analyse (track, trackRate, ref, refRate, refProf);
    std::cout << "  info  loudness gain " << juce::Decibels::gainToDecibels (res.totalGain()) << " dB, reference level "
              << juce::Decibels::gainToDecibels (res.finalAmplitudeCoef) << " dB\n";

    auto out = processChain (track, trackRate, res);

    const double refDb = loudRmsDb (ref, refRate), outDb = loudRmsDb (out, trackRate), inDb = loudRmsDb (track, trackRate);
    std::cout << "  info  loud-part RMS: track " << inDb << " dB -> output " << outDb << " dB (reference " << refDb << " dB)\n";
    check (std::abs (outDb - refDb) < 1.0, "output loudness within 1 dB of the reference");

    const auto bRef = octaveBands (ref, refRate), bIn = octaveBands (track, trackRate), bOut = octaveBands (out, trackRate);
    double before = 0, after = 0;
    std::cout << "  info  octave   ref-in   ref-out (dB, level-normalised)\n";
    const double offIn = bRef[3] - bIn[3], offOut = bRef[3] - bOut[3];
    for (size_t i = 0; i < bRef.size(); ++i)
    {
        const double dIn = bRef[i] - bIn[i] - offIn, dOut = bRef[i] - bOut[i] - offOut;
        before = std::max (before, std::abs (dIn));
        after = std::max (after, std::abs (dOut));
        std::cout << "        " << juce::String (63 << i).paddedLeft (' ', 6) << "  " << juce::String (dIn, 1).paddedLeft (' ', 7)
                  << "  " << juce::String (dOut, 1).paddedLeft (' ', 7) << "\n";
    }
    check (after < 2.0 && after < before * 0.25, "tonal balance matches the reference (worst band "
                                                    + juce::String (before, 1) + " dB -> " + juce::String (after, 1) + " dB)");

    float peak = 0;
    for (int ch = 0; ch < 2; ++ch) peak = std::max (peak, out.getMagnitude (ch, 0, out.getNumSamples()));
    check (peak <= cfg.threshold * (float) res.finalAmplitudeCoef + 1e-4f, "limiter keeps peaks under the ceiling (peak "
                                                                       + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");

    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}

bool readFile (juce::AudioFormatManager& fm, const juce::File& f, juce::AudioBuffer<float>& b, double& rate)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return false;
    b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    rate = r->sampleRate;
    return true;
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::String cmd = argc > 1 ? argv[1] : "";
    try
    {
        if (cmd == "selftest")
            return selftest();

        if (cmd == "match" && argc == 5)
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            juce::AudioBuffer<float> track, ref;
            double tr = 0, rr = 0;
            const auto cwd = juce::File::getCurrentWorkingDirectory();
            if (! readFile (fm, cwd.getChildFile (argv[2]), track, tr)) { std::cerr << "can't read track\n"; return 1; }
            if (! readFile (fm, cwd.getChildFile (argv[3]), ref, rr)) { std::cerr << "can't read reference\n"; return 1; }
            mm::ReferenceProfile rp;
            auto res = analyse (track, tr, ref, rr, rp);
            auto out = processChain (track, tr, res);
            auto outFile = cwd.getChildFile (argv[4]);
            outFile.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (outFile), tr, 2, 24, {}, 0));
            w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            std::cout << "wrote " << outFile.getFullPathName() << "  (loudness " << juce::Decibels::gainToDecibels (res.totalGain())
                      << " dB, reference level " << juce::Decibels::gainToDecibels (res.finalAmplitudeCoef) << " dB)\n";
            return 0;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cout << "usage:\n  matchmaster-cli selftest\n  matchmaster-cli match <track> <reference> <output.wav>\n";
    return 2;
}
