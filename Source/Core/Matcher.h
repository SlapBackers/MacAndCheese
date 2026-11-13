/*
    MatchMaster - reference matching mastering plugin
    C++ port of the Matchering algorithm (https://github.com/sergree/matchering)
    Matchering: Copyright (C) 2016-2022 Sergree, GPLv3.
    This file is part of MatchMaster and is distributed under the GNU GPL v3.
*/
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace mm
{

// Mirrors matchering.defaults.Config
struct Config
{
    double sampleRate        = 44100.0;                      // internal_sample_rate
    double maxPieceSeconds   = 15.0;                         // max_piece_size
    float  threshold         = (32768.0f - 61.0f) / 32768.0f; // ~ -0.016 dBFS
    float  minValue          = 1.0e-6f;                      // min_value (epsilon)
    int    fftSize           = 4096;
    int    linLogOversampling = 4;
    int    rmsCorrectionSteps = 4;
    double lowessFrac        = 0.0375;
    double lowessDelta       = 0.001;

    int numBins() const noexcept { return fftSize / 2 + 1; }
};

// Reference analysis (independent from the target).
struct ReferenceProfile
{
    std::vector<double> midSpectrum, sideSpectrum; // averaged |FFT| of the loudest pieces
    double matchRms = 0.0;                          // RMS of the loudest pieces (mid)
    double finalAmplitudeCoef = 1.0;                // reference peak / threshold (<= 1)
    double peak = 0.0;
    bool isValid() const noexcept { return ! midSpectrum.empty(); }
};

// Target analysis (keeps the mid signal for the RMS-correction passes).
struct TargetProfile
{
    std::vector<float> mid;                          // mid channel @ config rate
    std::vector<double> midSpectrum, sideSpectrum;   // raw (un-amplified) averaged |FFT|
    double matchRms = 0.0;
    int pieceSize = 0, divisions = 0;
    bool isValid() const noexcept { return ! midSpectrum.empty(); }
};

// Final result of matching: everything the real-time engine needs.
struct MatchResult
{
    std::vector<float> midCurve, sideCurve; // smoothed linear EQ magnitude, numBins() points over 0..sr/2
    double rmsCoefficient = 1.0;            // loudness match (stage 1)
    double correctionGain = 1.0;            // product of RMS correction steps (stage 3)
    double finalAmplitudeCoef = 1.0;        // reference peak level, applied after the limiter
    double curveSampleRate = 44100.0;
    int    curveFftSize = 4096;

    double totalGain() const noexcept { return rmsCoefficient * correctionGain; }
    bool isValid() const noexcept { return ! midCurve.empty(); }
};

// Throws std::runtime_error with a user-facing message on invalid input.
ReferenceProfile analyzeReference (const juce::AudioBuffer<float>& stereoAtConfigRate, const Config& cfg);
TargetProfile    analyzeTarget    (const juce::AudioBuffer<float>& stereoAtConfigRate, const Config& cfg);
MatchResult      match            (const TargetProfile& target, const ReferenceProfile& reference, const Config& cfg);

// Design a linear-phase FIR (length firSize, peak at firSize/2) from a matching curve.
// amount scales the curve in the log domain (0 = flat, 1 = exact Matchering curve).
// maxBoostDb limits boosts/cuts as a real-time safety net (<= 0 disables the limit).
std::vector<float> designFir (const std::vector<float>& curve, double curveSampleRate, int curveFftSize,
                              double firSampleRate, int firSize, float amount, float maxBoostDb);

// Linear-phase FIR length used at a given host rate (>= Matchering's 4096 @ 44.1k resolution).
int firSizeForRate (double sampleRate);

// Resample (with anti-alias filtering when downsampling) and force stereo.
juce::AudioBuffer<float> toStereoAtRate (const juce::AudioBuffer<float>& in, double inRate, double outRate);

// Same-mode FFT convolution (scipy.signal.fftconvolve(..., "same")).
std::vector<float> convolveSame (const std::vector<float>& x, const std::vector<float>& h);

// Exposed for tests.
std::vector<double> lowess (const std::vector<double>& x, const std::vector<double>& y, double frac, double delta);
std::vector<float>  smoothMatchingCurve (const std::vector<double>& matching, const Config& cfg);

} // namespace mm
