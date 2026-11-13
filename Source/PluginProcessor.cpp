#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/TruePeak.h"
#include <numeric>

namespace
{
    constexpr int maxCaptureSamples = 180 * 48000;      // ~3 min @ 48k (90 s @ 96k)
    constexpr double minCaptureSeconds = 5.0;
    constexpr double maxReferenceSeconds = 15.0 * 60.0; // Matchering's max_length
    const juce::StringArray slotNames { "A", "B", "C" };
    constexpr float clipOffDb = 24.0f;    // clip point far above any signal: clipper is a pure delay
    constexpr float clipSoftStart = 0.9549925859f; // ClipOnly2 starts softening here (-0.2 dB)
}

MatchMasterProcessor::MatchMasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();
    pMatch = apvts.getRawParameterValue ("match");
    pMidEq = apvts.getRawParameterValue ("midEq");
    pSideEq = apvts.getRawParameterValue ("sideEq");
    pLoudness = apvts.getRawParameterValue ("loudness");
    pSmoothing = apvts.getRawParameterValue ("smoothing");
    pMaxCorr = apvts.getRawParameterValue ("maxCorr");
    pEqMode = apvts.getRawParameterValue ("eqMode");
    pLimiter = apvts.getRawParameterValue ("limiter");
    pTruePeak = apvts.getRawParameterValue ("truePeak");
    pCeiling = apvts.getRawParameterValue ("ceiling");
    pClip = apvts.getRawParameterValue ("clip");
    pLevelMatch = apvts.getRawParameterValue ("levelMatch");
    pProfile = apvts.getRawParameterValue ("profile");
    for (auto& b : slotBusy)
        b = false;
    startTimerHz (30);
    builder.startThread (juce::Thread::Priority::high);
}

MatchMasterProcessor::~MatchMasterProcessor()
{
    stopTimer();
    builder.stopThread (4000);
    pool.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout MatchMasterProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using Ch = juce::AudioParameterChoice;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; });
    auto db = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (v, 1) + " dB"; });
    const juce::NormalisableRange<float> percent (0.0f, 100.0f, 0.1f);

    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<F> (juce::ParameterID { "match", 1 }, "Match", percent, 100.0f, pct));
    l.add (std::make_unique<Ch> (juce::ParameterID { "profile", 1 }, "Profile", slotNames, 0));
    // EQ
    l.add (std::make_unique<F> (juce::ParameterID { "midEq", 1 }, "Mid EQ", percent, 100.0f, pct));
    l.add (std::make_unique<F> (juce::ParameterID { "sideEq", 1 }, "Side EQ", percent, 100.0f, pct));
    l.add (std::make_unique<F> (juce::ParameterID { "smoothing", 1 }, "EQ Smoothing", percent, 0.0f, pct));
    l.add (std::make_unique<F> (juce::ParameterID { "maxCorr", 1 }, "Max Correction", juce::NormalisableRange<float> (3.0f, 24.0f, 0.1f), 12.0f, db));
    l.add (std::make_unique<Ch> (juce::ParameterID { "eqMode", 1 }, "EQ Mode", juce::StringArray { "Linear", "Natural" }, 0));
    // loudness + limiter
    l.add (std::make_unique<F> (juce::ParameterID { "loudness", 1 }, "Loudness", percent, 100.0f, pct));
    l.add (std::make_unique<F> (juce::ParameterID { "clip", 1 }, "Clip", juce::NormalisableRange<float> (0.0f, 6.0f, 0.1f), 0.0f, db));
    l.add (std::make_unique<B> (juce::ParameterID { "limiter", 1 }, "Limiter", true));
    // monitoring switch: hear the EQ match only, at the original track's loudness (not automatable)
    l.add (std::make_unique<B> (juce::ParameterID { "levelMatch", 1 }, "Level Match", false,
                                juce::AudioParameterBoolAttributes().withAutomatable (false)));
    l.add (std::make_unique<B> (juce::ParameterID { "truePeak", 1 }, "True Peak", true));
    l.add (std::make_unique<F> (juce::ParameterID { "ceiling", 1 }, "Ceiling", juce::NormalisableRange<float> (-6.0f, 0.0f, 0.1f), -1.0f, db));
    return l;
}

bool MatchMasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MatchMasterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);
    firSize = mm::firSizeForRate (sampleRate);

    midBuf.setSize (1, maxBlock);
    sideBuf.setSize (1, maxBlock);

    limiter.prepare (sampleRate, 2);
    clipper.prepare (sampleRate);
    clipThresholdDb.reset (sampleRate, 0.05);
    clipThresholdDb.setCurrentAndTargetValue (clipOffDb);
    clipGainCache = -1.0f;
    loudGain.reset (sampleRate, 0.05);
    finalGain.reset (sampleRate, 0.05);
    loudGain.setCurrentAndTargetValue (1.0f);
    finalGain.setCurrentAndTargetValue (1.0f);

    {
        const juce::ScopedLock sl (builderLock);
        midConv.prepare (convBlock, firSize);
        sideConv.prepare (convBlock, firSize);
        enginePrepared = true;
        buildAndOffer (true);
    }
    updateLatency();
}

void MatchMasterProcessor::updateLatency()
{
    const bool natural = pEqMode->load() > 0.5f;
    setLatencySamples ((natural ? 0 : firSize / 2) + convBlock + clipper.getLatencySamples() + limiter.getLatencySamples());
}

void MatchMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0)
        return;

    // ---- Learn: record the incoming (unprocessed) track
    if (capturing.load (std::memory_order_acquire))
    {
        const int pos = capturePos.load();
        const int n = juce::jmin (total, capture.getNumSamples() - pos);
        if (n > 0)
        {
            capture.copyFrom (0, pos, buffer, 0, 0, n);
            capture.copyFrom (1, pos, buffer, numCh > 1 ? 1 : 0, 0, n);
            capturePos.store (pos + n);
        }
        if (pos + n >= capture.getNumSamples())
            capturing.store (false); // buffer full: the timer finishes learning
    }

    // ---- gain staging
    const bool matched = activeMatched.load();
    const float match = matched ? pMatch->load() * 0.01f : 0.0f;
    const float loud = pLoudness->load() * 0.01f;
    const bool limOn = pLimiter->load() > 0.5f;
    const float ceilingDb = pCeiling->load();
    limiter.setCeiling (juce::Decibels::decibelsToGain (ceilingDb));
    limiter.setTruePeak (pTruePeak->load() > 0.5f);

    // Clip knob = dB the clipper shaves off the loudest peaks before the limiter (scaled by Match).
    const float clipDb = matched ? pClip->load() * match : 0.0f;
    const bool clipOn = clipDb > 0.01f;
    const float eqAmt = match * pMidEq->load() * 0.01f;
    const float estPeakDb = activeTrackPeakDb.load() + (activeEqPeakDb.load() - activeTrackPeakDb.load()) * eqAmt; // EQ'd track, unity gain

    float preDb = activeGainDb.load() * match * loud;
    float postDb = activeRefDb.load() * match;
    float clipPointDb = clipOffDb;
    if (! limOn)
    {
        // no limiter: keep the output under the ceiling, report what's missing.
        // With the clipper on, it clips 1 dB under the ceiling (room for its inter-sample overs).
        const float targetDb = preDb + postDb;
        const float peakDb = exactPeakValid.load() ? exactPeakDb.load() : estPeakDb + 2.0f; // estimate + margin until measured
        const float topDb = clipOn ? ceilingDb - 1.0f : ceilingDb;
        const float applied = matched ? juce::jmin (targetDb, topDb - peakDb + clipDb) : 0.0f;
        suggestionDb.store (matched ? juce::jmax (0.0f, targetDb - applied) : 0.0f);
        preDb = applied;
        postDb = 0.0f;
        if (clipOn)
            clipPointDb = topDb;
    }
    else
    {
        suggestionDb.store (0.0f);
        if (clipOn) // clip the top clipDb of the peaks, never below the limiter's ceiling
            clipPointDb = juce::jmax (ceilingDb, estPeakDb + preDb - clipDb);
    }
    const bool levelMatch = pLevelMatch->load() > 0.5f;
    if (levelMatch)
    {
        // EQ match only, kept at the track's own loudness: undo the level change the EQ causes (measured at Learn)
        preDb = ! matched ? 0.0f : exactEqLevelValid.load() ? -exactEqLevelDb.load() : -activeEqLevelDb.load() * eqAmt;
        postDb = 0.0f;
        clipPointDb = clipOffDb;
        suggestionDb.store (0.0f);
    }
    // Clipper -> clean gain -> limiter: clipping at T then adding G is identical to adding G then clipping at T+G,
    // which is how it's computed here; preDb is the clean gain the user sees between the two stages.
    gainIntoLimiterDb.store (levelMatch ? 0.0f : preDb);
    loudGain.setTargetValue (juce::Decibels::decibelsToGain (preDb));
    finalGain.setTargetValue (juce::Decibels::decibelsToGain (postDb));
    clipThresholdDb.setTargetValue (clipPointDb);

    float peakL = 0.0f, peakR = 0.0f;
    for (int off = 0; off < total; off += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - off);
        float* l = buffer.getWritePointer (0, off);
        float* r = numCh > 1 ? buffer.getWritePointer (1, off) : nullptr;
        float* m = midBuf.getWritePointer (0);
        float* s = sideBuf.getWritePointer (0);

        for (int i = 0; i < n; ++i) // dsp.lr_to_ms
        {
            const float li = l[i], ri = r != nullptr ? r[i] : li;
            m[i] = (li + ri) * 0.5f;
            s[i] = m[i] - ri;
        }

        midConv.process (m, n);
        sideConv.process (s, n);

        for (int i = 0; i < n; ++i) // dsp.ms_to_lr + loudness + clipper
        {
            const float g = loudGain.getNextValue();
            if (clipThresholdDb.isSmoothing() || clipGainCache < 0.0f)
            {
                clipGainCache = juce::Decibels::decibelsToGain (clipThresholdDb.getNextValue());
                clipInvCache = 1.0f / clipGainCache;
            }
            const float xl = (m[i] + s[i]) * g * clipInvCache;
            clipOver = juce::jmax (clipOver, std::abs (xl));
            l[i] = clipper.process (0, xl) * clipGainCache;
            if (r != nullptr)
            {
                const float xr = (m[i] - s[i]) * g * clipInvCache;
                clipOver = juce::jmax (clipOver, std::abs (xr));
                r[i] = clipper.process (1, xr) * clipGainCache;
            }
        }

        float* chans[2] = { l, r };
        limiter.process (chans, r != nullptr ? 2 : 1, n, limOn && ! levelMatch);
        for (int i = 0; i < n; ++i)
        {
            const float fg = finalGain.getNextValue();
            l[i] *= fg;
            peakL = juce::jmax (peakL, std::abs (l[i]));
            if (r != nullptr)
            {
                r[i] *= fg;
                peakR = juce::jmax (peakR, std::abs (r[i]));
            }
        }
    }

    meterL.store (juce::jmax (peakL, meterL.load() * 0.92f));
    meterR.store (juce::jmax (numCh > 1 ? peakR : peakL, meterR.load() * 0.92f));
    const float shaved = clipOver > clipSoftStart ? juce::Decibels::gainToDecibels (clipOver / clipSoftStart) : 0.0f;
    clipOver = 0.0f;
    clipDbNow.store (juce::jmax (shaved, clipDbNow.load() * 0.92f));
    maxClipDb.store (juce::jmax (maxClipDb.load(), shaved));
    const float g = limiter.popMinGain();
    meterGr.store (juce::jmin (meterGr.load(), g));
    maxGrDb.store (juce::jmax (maxGrDb.load(), -juce::Decibels::gainToDecibels (g, -60.0f)));
}

// ------------------------------------------------------------------ references / learn

mm::Config MatchMasterProcessor::configFor (double frac) const
{
    auto c = config;
    c.lowessFrac = frac;
    return c;
}

double MatchMasterProcessor::currentFrac() const
{
    return mm::lowessFracForSmoothing (pSmoothing->load() * 0.01);
}

void MatchMasterProcessor::loadReference (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= numSlots || ! file.existsAsFile() || slotBusy[(size_t) slot])
        return;
    slotBusy[(size_t) slot] = true;
    setMessage ({}, false);

    pool.addJob ([this, slot, file]
    {
        try
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr)
                throw std::runtime_error ("this file format can't be read.");

            const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                            (juce::int64) (maxReferenceSeconds * reader->sampleRate));
            juce::AudioBuffer<float> raw ((int) juce::jmax (1u, reader->numChannels), len);
            reader->read (&raw, 0, len, 0, true, true);

            auto ref = std::make_shared<mm::ReferenceProfile> (
                mm::analyzeReference (mm::toStereoAtRate (raw, reader->sampleRate, config.sampleRate), config));
            {
                const juce::ScopedLock sl (dataLock);
                auto& s = slots[(size_t) slot];
                s.ref = ref;
                s.result = nullptr;
                s.name = file.getFileNameWithoutExtension();
            }
            resultsChanged = true;
            matchSlots ({ slot });
        }
        catch (const std::exception& e)
        {
            setMessage ("Reference " + slotNames[slot] + ": " + e.what(), true);
        }
        slotBusy[(size_t) slot] = false;
    });

    selectSlot (slot);
}

void MatchMasterProcessor::selectSlot (int slot)
{
    if (auto* p = apvts.getParameter ("profile"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) slot));
}

int MatchMasterProcessor::getActiveSlot() const
{
    return juce::jlimit (0, numSlots - 1, (int) std::lround (pProfile->load()));
}

void MatchMasterProcessor::startLearning()
{
    if (capturing.load() || learnState != LearnState::idle)
        return;
    if (capture.getNumSamples() != maxCaptureSamples)
        capture.setSize (2, maxCaptureSamples); // audio thread only writes while capturing
    captureRate = hostRate;
    capturePos = 0;
    learnState = LearnState::listening;
    learnDeviationDb = -1.0f;
    learnEnough = false;
    lastLearnCheck = 0.0;
    if (! learnCheckPending.load())
    {
        lastLearnBands.clear();
        learnStableChecks = 0;
    }
    setMessage ({}, false);
    capturing.store (true, std::memory_order_release);
}

void MatchMasterProcessor::stopLearning()
{
    if (learnState != LearnState::listening)
        return;
    capturing.store (false, std::memory_order_release);

    const int n = capturePos.load();
    if ((double) n / captureRate < minCaptureSeconds)
    {
        learnState = LearnState::idle;
        setMessage ("Too short - play at least " + juce::String ((int) minCaptureSeconds) + " seconds of your track", true);
        return;
    }

    juce::AudioBuffer<float> copy (2, n);
    copy.copyFrom (0, 0, capture, 0, 0, n);
    copy.copyFrom (1, 0, capture, 1, 0, n);
    learnState = LearnState::analysing;

    pool.addJob ([this, buf = std::move (copy), rate = captureRate]
    {
        try
        {
            auto t = std::make_shared<mm::TargetProfile> (
                mm::analyzeTarget (mm::toStereoAtRate (buf, rate, config.sampleRate), config));
            {
                const juce::ScopedLock sl (dataLock);
                track = t;
            }
            matchSlots ({ 0, 1, 2 });
        }
        catch (const std::exception& e)
        {
            setMessage (juce::String ("Learn: ") + e.what(), true);
        }
        learnState = LearnState::idle;
    });
}

// Learn quality: re-run the track analysis on what has been captured so far and see whether the
// spectrum of the loudest parts (what the match is built from) has stopped changing.
void MatchMasterProcessor::checkLearnQuality()
{
    constexpr double checkEvery = 4.0, minSeconds = 30.0;
    constexpr float stableDb = 0.5f;
    const double secs = getCaptureSeconds();
    if (secs < 8.0 || secs - lastLearnCheck < checkEvery || learnCheckPending.load())
        return;
    lastLearnCheck = secs;
    const int n = capturePos.load();
    learnCheckPending = true;

    pool.addJob ([this, n, secs, rate = captureRate]
    {
        try
        {
            // the audio thread only writes beyond capturePos, so [0, n) is stable while listening
            juce::AudioBuffer<float> view (capture.getArrayOfWritePointers(), 2, n);
            const auto t = mm::analyzeTarget (mm::toStereoAtRate (view, rate, config.sampleRate), config);

            // 1/3-octave bands, 31.5 Hz - 16 kHz, shape only (mean removed)
            std::vector<float> bands;
            const double binHz = config.sampleRate / config.fftSize;
            for (int k = -15; k <= 12; ++k)
            {
                const double fc = 1000.0 * std::pow (2.0, k / 3.0);
                const int lo = juce::jmax (1, (int) (fc / std::pow (2.0, 1.0 / 6.0) / binHz));
                const int hi = juce::jmin ((int) t.midSpectrum.size() - 1, juce::jmax (lo, (int) (fc * std::pow (2.0, 1.0 / 6.0) / binHz)));
                double p = 0;
                for (int b = lo; b <= hi; ++b)
                    p += t.midSpectrum[(size_t) b] * t.midSpectrum[(size_t) b];
                bands.push_back ((float) (10.0 * std::log10 (p / (hi - lo + 1) + 1e-20)));
            }
            const float mean = std::accumulate (bands.begin(), bands.end(), 0.0f) / (float) bands.size();
            for (auto& b : bands)
                b -= mean;
            const float loudDb = (float) juce::Decibels::gainToDecibels (t.matchRms, -100.0);

            float dev = -1.0f;
            if (lastLearnBands.size() == bands.size())
            {
                dev = std::abs (loudDb - lastLearnLoudDb);
                for (size_t i = 0; i < bands.size(); ++i)
                    dev = juce::jmax (dev, std::abs (bands[i] - lastLearnBands[i]));
            }
            lastLearnBands = bands;
            lastLearnLoudDb = loudDb;
            learnStableChecks = dev >= 0.0f && dev < stableDb ? learnStableChecks + 1 : 0;
            if (learnState == LearnState::listening)
            {
                learnDeviationDb = dev;
                learnEnough = learnStableChecks >= 2 && secs >= minSeconds;
            }
        }
        catch (const std::exception&) {}
        learnCheckPending = false;
    });
}

void MatchMasterProcessor::matchSlots (std::vector<int> which)
{
    std::shared_ptr<mm::TargetProfile> t;
    double frac;
    {
        const juce::ScopedLock sl (dataLock);
        t = track;
        frac = requestedFrac;
    }
    if (t == nullptr)
        return;

    for (int i : which)
    {
        std::shared_ptr<mm::ReferenceProfile> ref;
        {
            const juce::ScopedLock sl (dataLock);
            ref = slots[(size_t) i].ref;
        }
        if (ref == nullptr)
            continue;
        try
        {
            auto res = std::make_shared<mm::MatchResult> (mm::match (*t, *ref, configFor (frac)));
            const juce::ScopedLock sl (dataLock);
            if (slots[(size_t) i].ref == ref)
                slots[(size_t) i].result = res;
        }
        catch (const std::exception& e)
        {
            setMessage ("Profile " + slotNames[i] + ": " + e.what(), true);
        }
    }
    resultsChanged = true;
}

void MatchMasterProcessor::resmooth (double frac)
{
    for (int i = 0; i < numSlots; ++i)
    {
        std::shared_ptr<mm::MatchResult> old;
        {
            const juce::ScopedLock sl (dataLock);
            old = slots[(size_t) i].result;
        }
        if (old == nullptr)
            continue;
        auto res = std::make_shared<mm::MatchResult> (*old);
        const auto cfg = configFor (frac);
        res->midCurve = mm::smoothMatchingCurve (res->midMatching, cfg);
        res->sideCurve = mm::smoothMatchingCurve (res->sideMatching, cfg);
        const juce::ScopedLock sl (dataLock);
        if (slots[(size_t) i].result == old)
            slots[(size_t) i].result = res;
    }
    resultsChanged = true;
}

bool MatchMasterProcessor::hasLearnedTrack() const
{
    const juce::ScopedLock sl (dataLock);
    return track != nullptr;
}

double MatchMasterProcessor::getCaptureSeconds() const noexcept { return capturePos.load() / juce::jmax (1.0, captureRate); }
double MatchMasterProcessor::getMaxCaptureSeconds() const noexcept { return maxCaptureSamples / juce::jmax (1.0, hostRate); }

void MatchMasterProcessor::setMessage (const juce::String& m, bool isError)
{
    const juce::ScopedLock sl (dataLock);
    message = m;
    messageIsError = isError;
}

MatchMasterProcessor::Snapshot MatchMasterProcessor::getSnapshot() const
{
    const int active = getActiveSlot();
    const juce::ScopedLock sl (dataLock);
    Snapshot s;
    bool anyRef = false;
    for (int i = 0; i < numSlots; ++i)
    {
        const auto& slot = slots[(size_t) i];
        auto& info = s.slots[(size_t) i];
        info.loaded = slot.ref != nullptr;
        info.busy = slotBusy[(size_t) i];
        info.matched = slot.result != nullptr;
        info.name = slot.name;
        anyRef |= info.loaded || info.busy;
    }
    if (const auto& r = slots[(size_t) active].result)
    {
        s.midCurve = r->midCurve;
        s.sideCurve = r->sideCurve;
        s.curveRate = r->curveSampleRate;
        s.curveFft = r->curveFftSize;
        s.gainDb = juce::Decibels::gainToDecibels (r->totalGain(), -100.0);
        s.refLevelDb = juce::Decibels::gainToDecibels (r->finalAmplitudeCoef, -100.0);
    }

    s.messageIsError = messageIsError && message.isNotEmpty();
    if (s.messageIsError)
        s.message = message;
    else if (learnState == LearnState::listening)
    {
        const float dev = learnDeviationDb.load();
        if (learnEnough.load())
            s.message = "Enough - the sound of your track is stable, you can press Stop";
        else if (dev < 0.0f)
            s.message = "Listening - play the loudest part of your track";
        else if (dev < 0.5f)
            s.message = "Almost there - keep playing (at least 30 s, loudest part included)";
        else
            s.message = "Keep playing - the sound is still changing (" + juce::String (dev, 1) + " dB). Include the loudest part";
    }
    else if (learnState == LearnState::analysing)
        s.message = "Matching your track to the references...";
    else if (! anyRef && track == nullptr)
        s.message = "Drop up to 3 mastered songs into A, B, C - then press Learn and play your track";
    else if (track == nullptr && restoredFromProject && s.slots[(size_t) active].matched)
        s.message = "Restored from the project - press Learn again before loading a new reference";
    else if (track == nullptr)
        s.message = "Press Learn and play your track";
    else if (! s.slots[(size_t) active].loaded)
        s.message = "Drop a reference into slot " + slotNames[active];
    else if (s.slots[(size_t) active].matched)
        s.message = "Profile " + slotNames[active] + " matched - switch A / B / C to compare";
    return s;
}

// ------------------------------------------------------------------ message-thread housekeeping

void MatchMasterProcessor::timerCallback()
{
    if (learnState == LearnState::listening && ! capturing.load())
        stopLearning(); // capture buffer filled up
    if (learnState == LearnState::listening)
        checkLearnQuality();

    const int active = getActiveSlot();
    if (resultsChanged.exchange (false) || active != shownSlot)
        applyActiveSlot (true);

    const int mode = pEqMode->load() > 0.5f ? 1 : 0;
    if (mode != latencyMode)
    {
        latencyMode = mode;
        updateLatency();
    }

    if (pLimiter->load() < 0.5f || pLevelMatch->load() > 0.5f)
        updateHeadroom();
}

void MatchMasterProcessor::updateHeadroom()
{
    std::shared_ptr<mm::MatchResult> src;
    std::shared_ptr<mm::TargetProfile> t;
    {
        const juce::ScopedLock sl (dataLock);
        t = track;
        src = firSource;
    }
    if (src == nullptr || t == nullptr)
        return;

    const float match = pMatch->load() * 0.01f;
    const float midAmt = match * pMidEq->load() * 0.01f, sideAmt = match * pSideEq->load() * 0.01f;
    const float maxCorr = pMaxCorr->load();
    const bool natural = pEqMode->load() > 0.5f;
    const auto key = juce::String::toHexString ((juce::pointer_sized_int) src.get()) + ":" + juce::String (midAmt, 3) + ":"
                     + juce::String (sideAmt, 3) + ":" + juce::String (maxCorr, 1) + ":" + juce::String ((int) natural);
    if (key == lastPeakKey || peakJobPending.load())
        return;
    lastPeakKey = key;
    exactPeakValid = false;
    exactEqLevelValid = false;
    peakJobPending = true;

    pool.addJob ([this, src, t, midAmt, sideAmt, maxCorr, natural, key]
    {
        // same EQ as the live chain, designed at the analysis rate, run over the learned track
        const auto& c = config;
        auto midFir = mm::designFir (src->midCurve, src->curveSampleRate, src->curveFftSize, c.sampleRate, c.fftSize, midAmt, maxCorr, natural);
        auto sideFir = mm::designFir (src->sideCurve, src->curveSampleRate, src->curveFftSize, c.sampleRate, c.fftSize, sideAmt, maxCorr, natural);
        auto m = mm::convolveSame (t->mid, midFir);
        auto sd = mm::convolveSame (t->side, sideFir);
        if (natural) // minimum-phase FIR has no centre delay: undo convolveSame's centring
        {
            const int shift = (c.fftSize - 1) / 2;
            auto fullM = std::vector<float> (m.size(), 0.0f), fullS = fullM;
            for (size_t i = (size_t) shift; i < m.size(); ++i) { fullM[i] = m[i - (size_t) shift]; fullS[i] = sd[i - (size_t) shift]; }
            m.swap (fullM);
            sd.swap (fullS);
        }
        std::vector<float> l (m.size()), r (m.size());
        for (size_t i = 0; i < m.size(); ++i) { l[i] = m[i] + sd[i]; r[i] = m[i] - sd[i]; }
        const float pk = std::max (mm::TruePeakDetector::measure (l.data(), (int) l.size()),
                                   mm::TruePeakDetector::measure (r.data(), (int) r.size()));
        const float levelDb = (float) juce::Decibels::gainToDecibels (
            mm::loudestPiecesRms (m, t->pieceSize, t->divisions) / juce::jmax (1e-9, t->matchRms), -100.0);
        if (key == lastPeakKey)
        {
            exactEqLevelDb = levelDb;
            exactEqLevelValid = true;
            exactPeakDb = juce::Decibels::gainToDecibels (pk, -120.0f) + 0.8f; // margin: measured at 44.1 kHz, plays at the host rate
            exactPeakValid = true;
        }
        peakJobPending = false;
    });
}

void MatchMasterProcessor::applyActiveSlot (bool forceFirs)
{
    const int active = getActiveSlot();
    if (active != shownSlot)
    {
        maxGrDb = 0.0f;
        maxClipDb = 0.0f;
    }
    shownSlot = active;
    std::shared_ptr<mm::MatchResult> src;
    {
        const juce::ScopedLock sl (dataLock);
        firSource = slots[(size_t) active].result;
        src = firSource;
    }
    if (src != nullptr)
    {
        activeGainDb = (float) juce::Decibels::gainToDecibels (src->totalGain(), -100.0);
        activeEqLevelDb = (float) src->eqLevelDb;
        activeRefDb = (float) juce::Decibels::gainToDecibels (src->finalAmplitudeCoef, -100.0);
        activeTrackPeakDb = (float) src->trackPeakDb;
        activeEqPeakDb = (float) src->eqPeakDb;
    }
    activeMatched = src != nullptr;
    if (forceFirs)
        forceRebuild = true;
}

void MatchMasterProcessor::builderTick()
{
    // EQ smoothing knob -> re-smooth the stored matching curves in the background
    const double frac = currentFrac();
    if (std::abs (frac - requestedFrac) > 1e-5 && ! resmoothPending.load())
    {
        {
            const juce::ScopedLock sl (dataLock);
            requestedFrac = frac;
        }
        resmoothPending = true;
        pool.addJob ([this, frac] { resmooth (frac); resmoothPending = false; });
    }

    const juce::ScopedLock sl (builderLock);
    midConv.collectGarbage();
    sideConv.collectGarbage();
    if (enginePrepared)
        buildAndOffer (false);
}

void MatchMasterProcessor::buildAndOffer (bool immediate)
{
    std::shared_ptr<mm::MatchResult> src;
    {
        const juce::ScopedLock sl (dataLock);
        src = firSource;
    }
    const float match = pMatch->load() * 0.01f;
    const float midAmt = src != nullptr ? match * pMidEq->load() * 0.01f : 0.0f;
    const float sideAmt = src != nullptr ? match * pSideEq->load() * 0.01f : 0.0f;
    const float maxCorr = pMaxCorr->load();
    const int mode = pEqMode->load() > 0.5f ? 1 : 0;

    const bool force = immediate || forceRebuild.exchange (false) || src != builtSource;
    const bool common = force || mode != builtMode || std::abs (maxCorr - builtMaxCorr) > 0.05f;

    auto design = [&] (const std::vector<float>* curve, float amt)
    {
        static const std::vector<float> flat { 1.0f, 1.0f };
        const auto& c = curve != nullptr ? *curve : flat;
        const double cr = src != nullptr ? src->curveSampleRate : config.sampleRate;
        const int cf = src != nullptr ? src->curveFftSize : config.fftSize;
        return mm::designFir (c, cr, cf, hostRate, firSize, curve != nullptr ? amt : 0.0f, maxCorr, mode == 1);
    };
    auto install = [immediate] (mm::PartitionedConvolver& conv, const std::vector<float>& fir)
    {
        auto k = conv.makeKernel (fir);
        if (immediate) conv.setImmediately (std::move (k));
        else conv.offer (std::move (k));
    };

    if (common || std::abs (midAmt - builtMid) > 0.002f)
    {
        install (midConv, design (src != nullptr ? &src->midCurve : nullptr, midAmt));
        builtMid = midAmt;
    }
    if (common || std::abs (sideAmt - builtSide) > 0.002f)
    {
        install (sideConv, design (src != nullptr ? &src->sideCurve : nullptr, sideAmt));
        builtSide = sideAmt;
    }
    builtMaxCorr = maxCorr;
    builtMode = mode;
    builtSource = src;
}

// ------------------------------------------------------------------ state (parameters only)

namespace
{
    // arrays are stored as float32 blobs (plenty of precision for spectra and curves)
    template <typename T>
    juce::MemoryBlock toBlob (const std::vector<T>& v)
    {
        std::vector<float> f (v.begin(), v.end());
        return juce::MemoryBlock (f.data(), f.size() * sizeof (float));
    }
    template <typename T>
    std::vector<T> fromBlob (const juce::var& v)
    {
        std::vector<T> out;
        if (auto* mb = v.getBinaryData())
        {
            const auto* f = static_cast<const float*> (mb->getData());
            out.assign (f, f + mb->getSize() / sizeof (float));
        }
        return out;
    }
    const juce::Identifier idState ("MatchMasterState"), idSlot ("Slot"), idRef ("Reference"), idResult ("Result");
    constexpr int stateVersion = 1;
}

void MatchMasterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root (idState);
    root.setProperty ("version", stateVersion, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    {
        const juce::ScopedLock sl (dataLock);
        for (int i = 0; i < numSlots; ++i)
        {
            const auto& slot = slots[(size_t) i];
            juce::ValueTree st (idSlot);
            st.setProperty ("index", i, nullptr);
            st.setProperty ("name", slot.name, nullptr);
            if (const auto& r = slot.ref)
            {
                juce::ValueTree rt (idRef);
                rt.setProperty ("midSpectrum", toBlob (r->midSpectrum), nullptr);
                rt.setProperty ("sideSpectrum", toBlob (r->sideSpectrum), nullptr);
                rt.setProperty ("matchRms", r->matchRms, nullptr);
                rt.setProperty ("finalAmplitudeCoef", r->finalAmplitudeCoef, nullptr);
                rt.setProperty ("peak", r->peak, nullptr);
                st.appendChild (rt, nullptr);
            }
            if (const auto& m = slot.result)
            {
                juce::ValueTree mt (idResult);
                mt.setProperty ("midCurve", toBlob (m->midCurve), nullptr);
                mt.setProperty ("sideCurve", toBlob (m->sideCurve), nullptr);
                mt.setProperty ("midMatching", toBlob (m->midMatching), nullptr);
                mt.setProperty ("sideMatching", toBlob (m->sideMatching), nullptr);
                mt.setProperty ("trackPeakDb", m->trackPeakDb, nullptr);
                mt.setProperty ("eqPeakDb", m->eqPeakDb, nullptr);
                mt.setProperty ("eqLevelDb", m->eqLevelDb, nullptr);
                mt.setProperty ("rmsCoefficient", m->rmsCoefficient, nullptr);
                mt.setProperty ("correctionGain", m->correctionGain, nullptr);
                mt.setProperty ("finalAmplitudeCoef", m->finalAmplitudeCoef, nullptr);
                mt.setProperty ("curveSampleRate", m->curveSampleRate, nullptr);
                mt.setProperty ("curveFftSize", m->curveFftSize, nullptr);
                st.appendChild (mt, nullptr);
            }
            root.appendChild (st, nullptr);
        }
    }
    juce::MemoryOutputStream raw;
    root.writeToStream (raw);
    juce::MemoryOutputStream out (dest, false);
    out.writeInt (0x4d4d5354); // "MMST"
    juce::GZIPCompressorOutputStream gz (out, 6);
    gz.write (raw.getData(), raw.getDataSize());
    gz.flush();
}

void MatchMasterProcessor::setStateInformation (const void* data, int size)
{
    // v2.0 projects: parameters only, as XML
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        return;
    }
    if (size < 4)
        return;
    juce::MemoryInputStream in (data, (size_t) size, false);
    if (in.readInt() != 0x4d4d5354)
        return;
    juce::GZIPDecompressorInputStream gz (in);
    juce::MemoryBlock rawBlock;
    gz.readIntoMemoryBlock (rawBlock);
    const auto root = juce::ValueTree::readFromData (rawBlock.getData(), rawBlock.getSize());
    if (! root.hasType (idState))
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params.createCopy());

    std::array<Slot, numSlots> restored;
    for (const auto& st : root)
    {
        if (! st.hasType (idSlot))
            continue;
        const int i = st.getProperty ("index", -1);
        if (i < 0 || i >= numSlots)
            continue;
        auto& slot = restored[(size_t) i];
        slot.name = st.getProperty ("name").toString();
        const auto rt = st.getChildWithName (idRef);
        if (rt.isValid())
        {
            auto r = std::make_shared<mm::ReferenceProfile>();
            r->midSpectrum = fromBlob<double> (rt["midSpectrum"]);
            r->sideSpectrum = fromBlob<double> (rt["sideSpectrum"]);
            r->matchRms = rt["matchRms"];
            r->finalAmplitudeCoef = rt["finalAmplitudeCoef"];
            r->peak = rt["peak"];
            if (r->isValid())
                slot.ref = r;
        }
        const auto mt = st.getChildWithName (idResult);
        if (mt.isValid() && slot.ref != nullptr)
        {
            auto m = std::make_shared<mm::MatchResult>();
            m->midCurve = fromBlob<float> (mt["midCurve"]);
            m->sideCurve = fromBlob<float> (mt["sideCurve"]);
            m->midMatching = fromBlob<double> (mt["midMatching"]);
            m->sideMatching = fromBlob<double> (mt["sideMatching"]);
            m->trackPeakDb = mt["trackPeakDb"];
            m->eqPeakDb = mt["eqPeakDb"];
            m->eqLevelDb = mt.getProperty ("eqLevelDb", 0.0);
            m->rmsCoefficient = mt["rmsCoefficient"];
            m->correctionGain = mt["correctionGain"];
            m->finalAmplitudeCoef = mt["finalAmplitudeCoef"];
            m->curveSampleRate = mt["curveSampleRate"];
            m->curveFftSize = mt["curveFftSize"];
            if (m->isValid())
                slot.result = m;
        }
    }
    {
        const juce::ScopedLock sl (dataLock);
        slots = restored;
        track = nullptr;       // the learned audio itself is not stored in the project
        restoredFromProject = true;
        requestedFrac = currentFrac(); // stored curves are already smoothed with the saved setting
    }
    lastPeakKey = {};
    exactPeakValid = false;
    exactEqLevelValid = false;
    resultsChanged = true;
}

juce::AudioProcessorEditor* MatchMasterProcessor::createEditor() { return new MatchMasterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MatchMasterProcessor(); }
