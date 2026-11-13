#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int maxCaptureSamples = 180 * 48000;   // ~3 min @ 48k (90 s @ 96k)
    constexpr double minCaptureSeconds = 5.0;
    constexpr double maxReferenceSeconds = 15.0 * 60.0; // Matchering's max_length
    constexpr float maxEqBoostDb = 24.0f;               // real-time safety net

    juce::String phaseMessage (MatchMasterProcessor::Phase p)
    {
        using P = MatchMasterProcessor::Phase;
        switch (p)
        {
            case P::needReference:      return "Step 1 - drop a well-mastered reference song";
            case P::analysingReference: return "Analysing the reference...";
            case P::readyToLearn:       return "Step 2 - press Learn and play your track";
            case P::listening:          return "Listening - play the loudest part, then press Stop";
            case P::analysingTrack:     return "Matching your track to the reference...";
            case P::matched:            return "Matched - the result is locked. Re-learn after big mix changes";
        }
        return {};
    }
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
    message = phaseMessage (Phase::needReference);
    startTimerHz (30);
}

MatchMasterProcessor::~MatchMasterProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout MatchMasterProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; });

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<P> (juce::ParameterID { "match", 1 }, "Match", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    layout.add (std::make_unique<P> (juce::ParameterID { "midEq", 1 }, "Mid EQ", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    layout.add (std::make_unique<P> (juce::ParameterID { "sideEq", 1 }, "Side EQ", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    layout.add (std::make_unique<P> (juce::ParameterID { "loudness", 1 }, "Loudness", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    return layout;
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

    setLatencySamples (firSize / 2 + limiter.getLatencySamples());
    rebuildFirs (true);
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

    const float match = pMatch->load() * 0.01f;
    loudGain.setTargetValue (juce::Decibels::decibelsToGain (matchGainDb.load() * match * pLoudness->load() * 0.01f));
    finalGain.setTargetValue (juce::Decibels::decibelsToGain (refLevelDb.load() * match));

    float peakL = 0.0f, peakR = 0.0f;
    for (int off = 0; off < total; off += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - off);
        float* l = buffer.getWritePointer (0, off);
        float* r = numCh > 1 ? buffer.getWritePointer (1, off) : nullptr;
        float* m = midBuf.getWritePointer (0);
        float* s = sideBuf.getWritePointer (0);

        // dsp.lr_to_ms
        for (int i = 0; i < n; ++i)
        {
            const float li = l[i], ri = r != nullptr ? r[i] : li;
            m[i] = (li + ri) * 0.5f;
            s[i] = m[i] - ri;
        }

        // matching EQ (linear-phase FIR, separate for mid and side)
        juce::dsp::AudioBlock<float> mb (midBuf), sb (sideBuf);
        auto mSub = mb.getSubBlock (0, (size_t) n);
        auto sSub = sb.getSubBlock (0, (size_t) n);
        midConv.process (juce::dsp::ProcessContextReplacing<float> (mSub));
        sideConv.process (juce::dsp::ProcessContextReplacing<float> (sSub));

        // dsp.ms_to_lr + loudness match
        for (int i = 0; i < n; ++i)
        {
            const float g = loudGain.getNextValue();
            l[i] = (m[i] + s[i]) * g;
            if (r != nullptr)
                r[i] = (m[i] - s[i]) * g;
        }

        // Hyrax brickwall limiter, then the reference's peak level
        float* chans[2] = { l, r };
        limiter.process (chans, r != nullptr ? 2 : 1, n, true);
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
    meterGr.store (juce::jmin (meterGr.load(), limiter.popMinGain()));
}

// ------------------------------------------------------------------ reference / learn

void MatchMasterProcessor::loadReference (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    const auto prev = phase.load();
    phase = Phase::analysingReference;
    setMessage (phaseMessage (Phase::analysingReference), false);

    pool.addJob ([this, file, prev]
    {
        try
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr)
                throw std::runtime_error ("This file format can't be read.");

            const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                            (juce::int64) (maxReferenceSeconds * reader->sampleRate));
            juce::AudioBuffer<float> raw ((int) juce::jmax (1u, reader->numChannels), len);
            reader->read (&raw, 0, len, 0, true, true);

            auto ref = std::make_shared<mm::ReferenceProfile> (
                mm::analyzeReference (mm::toStereoAtRate (raw, reader->sampleRate, config.sampleRate), config));

            bool haveTrack;
            {
                const juce::ScopedLock sl (dataLock);
                reference = ref;
                referenceName = file.getFileNameWithoutExtension();
                haveTrack = captureCopy.getNumSamples() > 0;
            }

            if (haveTrack)
            {
                phase = Phase::analysingTrack; // re-match the already learned track
                computeMatch();
            }
            else
            {
                phase = Phase::readyToLearn;
                setMessage (phaseMessage (Phase::readyToLearn), false);
            }
        }
        catch (const std::exception& e)
        {
            phase = prev == Phase::analysingReference ? Phase::needReference : prev;
            setMessage (juce::String ("Reference: ") + e.what(), true);
        }
    });
}

void MatchMasterProcessor::startLearning()
{
    const auto p = phase.load();
    if (capturing.load() || (p != Phase::readyToLearn && p != Phase::matched))
        return;

    if (capture.getNumSamples() != maxCaptureSamples)
        capture.setSize (2, maxCaptureSamples); // message thread; audio thread only writes while capturing
    captureRate = hostRate;
    capturePos = 0;
    phase = Phase::listening;
    setMessage (phaseMessage (Phase::listening), false);
    capturing.store (true, std::memory_order_release);
}

void MatchMasterProcessor::stopLearning()
{
    if (phase != Phase::listening)
        return;
    capturing.store (false, std::memory_order_release);

    const int n = capturePos.load();
    if ((double) n / captureRate < minCaptureSeconds)
    {
        phase = Phase::readyToLearn;
        setMessage ("Too short - play at least " + juce::String ((int) minCaptureSeconds) + " seconds of your track", true);
        return;
    }

    {
        const juce::ScopedLock sl (dataLock);
        captureCopy.setSize (2, n);
        captureCopy.copyFrom (0, 0, capture, 0, 0, n);
        captureCopy.copyFrom (1, 0, capture, 1, 0, n);
    }
    phase = Phase::analysingTrack;
    setMessage (phaseMessage (Phase::analysingTrack), false);
    pool.addJob ([this] { computeMatch(); });
}

void MatchMasterProcessor::computeMatch()
{
    try
    {
        std::shared_ptr<mm::ReferenceProfile> ref;
        juce::AudioBuffer<float> track;
        double rate;
        {
            const juce::ScopedLock sl (dataLock);
            ref = reference;
            track.makeCopyOf (captureCopy);
            rate = captureRate;
        }
        if (ref == nullptr)
            throw std::runtime_error ("No reference loaded.");

        const auto tgt = mm::analyzeTarget (mm::toStereoAtRate (track, rate, config.sampleRate), config);
        auto res = std::make_shared<mm::MatchResult> (mm::match (tgt, *ref, config));
        {
            const juce::ScopedLock sl (dataLock);
            pendingResult = res;
        }
        resultReady = true;
    }
    catch (const std::exception& e)
    {
        phase = Phase::readyToLearn;
        setMessage (juce::String ("Learn: ") + e.what(), true);
    }
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
    const juce::ScopedLock sl (dataLock);
    Snapshot s;
    if (result != nullptr)
    {
        s.midCurve = result->midCurve;
        s.sideCurve = result->sideCurve;
        s.curveRate = result->curveSampleRate;
        s.curveFft = result->curveFftSize;
    }
    s.gainDb = matchGainDb.load();
    s.refLevelDb = refLevelDb.load();
    s.referenceName = referenceName;
    s.message = message;
    s.messageIsError = messageIsError;
    return s;
}

// ------------------------------------------------------------------ message-thread housekeeping

void MatchMasterProcessor::timerCallback()
{
    if (phase == Phase::listening && ! capturing.load())
        stopLearning(); // capture buffer filled up

    if (resultReady.exchange (false))
    {
        {
            const juce::ScopedLock sl (dataLock);
            result = pendingResult;
        }
        matchGainDb = (float) juce::Decibels::gainToDecibels (result->totalGain(), -100.0);
        refLevelDb = (float) juce::Decibels::gainToDecibels (result->finalAmplitudeCoef, -100.0);
        phase = Phase::matched;
        setMessage (phaseMessage (Phase::matched), false);
        rebuildFirs (true);
        return;
    }
    rebuildFirs (false);
}

void MatchMasterProcessor::rebuildFirs (bool force)
{
    const float match = pMatch->load() * 0.01f;
    const float midAmt = result != nullptr ? match * pMidEq->load() * 0.01f : 0.0f;
    const float sideAmt = result != nullptr ? match * pSideEq->load() * 0.01f : 0.0f;

    auto load = [this] (juce::dsp::Convolution& conv, const std::vector<float>& curve, float amt)
    {
        static const std::vector<float> flat { 1.0f, 1.0f };
        const auto& c = curve.empty() ? flat : curve;
        const double cr = result != nullptr ? result->curveSampleRate : config.sampleRate;
        const int cf = result != nullptr ? result->curveFftSize : config.fftSize;
        auto fir = mm::designFir (c, cr, cf, hostRate, firSize, curve.empty() ? 0.0f : amt, maxEqBoostDb);
        juce::AudioBuffer<float> ir (1, (int) fir.size());
        ir.copyFrom (0, 0, fir.data(), (int) fir.size());
        conv.loadImpulseResponse (std::move (ir), hostRate, juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    };

    static const std::vector<float> none;
    if (force || std::abs (midAmt - builtMidAmt) > 0.004f)
    {
        load (midConv, result != nullptr ? result->midCurve : none, midAmt);
        builtMidAmt = midAmt;
    }
    if (force || std::abs (sideAmt - builtSideAmt) > 0.004f)
    {
        load (sideConv, result != nullptr ? result->sideCurve : none, sideAmt);
        builtSideAmt = sideAmt;
    }
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
