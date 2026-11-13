#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/TruePeak.h"

namespace
{
    constexpr int maxCaptureSamples = 180 * 48000;      // ~3 min @ 48k (90 s @ 96k)
    constexpr double minCaptureSeconds = 5.0;
    constexpr double maxReferenceSeconds = 15.0 * 60.0; // Matchering's max_length
    const juce::StringArray slotNames { "A", "B", "C" };
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
    pProfile = apvts.getRawParameterValue ("profile");
    for (auto& b : slotBusy)
        b = false;
    startTimerHz (30);
}

MatchMasterProcessor::~MatchMasterProcessor()
{
    stopTimer();
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
    l.add (std::make_unique<B> (juce::ParameterID { "limiter", 1 }, "Limiter", true));
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

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 1 };
    midConv.prepare (spec);
    sideConv.prepare (spec);
    midBuf.setSize (1, maxBlock);
    sideBuf.setSize (1, maxBlock);

    limiter.prepare (sampleRate, 2);
    loudGain.reset (sampleRate, 0.05);
    finalGain.reset (sampleRate, 0.05);
    loudGain.setCurrentAndTargetValue (1.0f);
    finalGain.setCurrentAndTargetValue (1.0f);

    builtMode = -1;
    rebuildFirs (true);
}

void MatchMasterProcessor::updateLatency()
{
    const bool natural = pEqMode->load() > 0.5f;
    setLatencySamples ((natural ? 0 : firSize / 2) + limiter.getLatencySamples());
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

    float preDb = activeGainDb.load() * match * loud;
    float postDb = activeRefDb.load() * match;
    if (! limOn)
    {
        // no limiter: keep the output under the ceiling, report what's missing
        const float targetDb = preDb + postDb;
        const float eqAmt = match * pMidEq->load() * 0.01f;
        const float peakDb = exactPeakValid.load()
                                 ? exactPeakDb.load()
                                 : activeTrackPeakDb.load() + (activeEqPeakDb.load() - activeTrackPeakDb.load()) * eqAmt + 2.0f; // estimate + margin until measured
        const float applied = matched ? juce::jmin (targetDb, ceilingDb - peakDb) : 0.0f;
        suggestionDb.store (matched ? juce::jmax (0.0f, targetDb - applied) : 0.0f);
        preDb = applied;
        postDb = 0.0f;
    }
    else
        suggestionDb.store (0.0f);
    loudGain.setTargetValue (juce::Decibels::decibelsToGain (preDb));
    finalGain.setTargetValue (juce::Decibels::decibelsToGain (postDb));

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

        juce::dsp::AudioBlock<float> mb (midBuf), sb (sideBuf);
        auto mSub = mb.getSubBlock (0, (size_t) n);
        auto sSub = sb.getSubBlock (0, (size_t) n);
        midConv.process (juce::dsp::ProcessContextReplacing<float> (mSub));
        sideConv.process (juce::dsp::ProcessContextReplacing<float> (sSub));

        for (int i = 0; i < n; ++i) // dsp.ms_to_lr + loudness
        {
            const float g = loudGain.getNextValue();
            l[i] = (m[i] + s[i]) * g;
            if (r != nullptr)
                r[i] = (m[i] - s[i]) * g;
        }

        float* chans[2] = { l, r };
        limiter.process (chans, r != nullptr ? 2 : 1, n, limOn);
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
                s.flavour = mm::describeReference (*ref, config);
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

void MatchMasterProcessor::setFlavour (int slot, const juce::String& word)
{
    const juce::ScopedLock sl (dataLock);
    if (slot >= 0 && slot < numSlots)
        slots[(size_t) slot].flavour = word.trim();
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
        info.flavour = slot.flavour;
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
        s.message = "Listening - play the loudest part of your track, then press Stop";
    else if (learnState == LearnState::analysing)
        s.message = "Matching your track to the references...";
    else if (! anyRef && track == nullptr)
        s.message = "Drop up to 3 mastered songs into A, B, C - then press Learn and play your track";
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

    const int active = getActiveSlot();
    if (resultsChanged.exchange (false) || active != shownSlot)
        applyActiveSlot (true);
    else
        rebuildFirs (false);

    if (pLimiter->load() < 0.5f)
        updateHeadroom();
}

void MatchMasterProcessor::updateHeadroom()
{
    auto src = firSource;
    std::shared_ptr<mm::TargetProfile> t;
    {
        const juce::ScopedLock sl (dataLock);
        t = track;
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
        if (key == lastPeakKey)
        {
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
        maxGrDb = 0.0f;
    shownSlot = active;
    {
        const juce::ScopedLock sl (dataLock);
        firSource = slots[(size_t) active].result;
    }
    if (firSource != nullptr)
    {
        activeGainDb = (float) juce::Decibels::gainToDecibels (firSource->totalGain(), -100.0);
        activeRefDb = (float) juce::Decibels::gainToDecibels (firSource->finalAmplitudeCoef, -100.0);
        activeTrackPeakDb = (float) firSource->trackPeakDb;
        activeEqPeakDb = (float) firSource->eqPeakDb;
    }
    activeMatched = firSource != nullptr;
    rebuildFirs (forceFirs);
}

void MatchMasterProcessor::rebuildFirs (bool force)
{
    const float match = pMatch->load() * 0.01f;
    const float midAmt = firSource != nullptr ? match * pMidEq->load() * 0.01f : 0.0f;
    const float sideAmt = firSource != nullptr ? match * pSideEq->load() * 0.01f : 0.0f;
    const float maxCorr = pMaxCorr->load();
    const int mode = pEqMode->load() > 0.5f ? 1 : 0;

    const bool modeChanged = mode != builtMode;
    const bool common = force || modeChanged || std::abs (maxCorr - builtMaxCorr) > 0.05f;
    if (modeChanged)
        updateLatency();

    auto load = [this, maxCorr, mode] (juce::dsp::Convolution& conv, const std::vector<float>* curve, float amt)
    {
        static const std::vector<float> flat { 1.0f, 1.0f };
        const auto& c = curve != nullptr ? *curve : flat;
        const double cr = firSource != nullptr ? firSource->curveSampleRate : config.sampleRate;
        const int cf = firSource != nullptr ? firSource->curveFftSize : config.fftSize;
        auto fir = mm::designFir (c, cr, cf, hostRate, firSize, curve != nullptr ? amt : 0.0f, maxCorr, mode == 1);
        juce::AudioBuffer<float> ir (1, (int) fir.size());
        ir.copyFrom (0, 0, fir.data(), (int) fir.size());
        conv.loadImpulseResponse (std::move (ir), hostRate, juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    };

    if (common || std::abs (midAmt - builtMid) > 0.004f)
    {
        load (midConv, firSource != nullptr ? &firSource->midCurve : nullptr, midAmt);
        builtMid = midAmt;
    }
    if (common || std::abs (sideAmt - builtSide) > 0.004f)
    {
        load (sideConv, firSource != nullptr ? &firSource->sideCurve : nullptr, sideAmt);
        builtSide = sideAmt;
    }
    builtMaxCorr = maxCorr;
    builtMode = mode;
}

// ------------------------------------------------------------------ state (parameters only)

void MatchMasterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MatchMasterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MatchMasterProcessor::createEditor() { return new MatchMasterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MatchMasterProcessor(); }
