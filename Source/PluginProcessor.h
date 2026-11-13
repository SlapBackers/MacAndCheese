/*
    MatchMaster - semi-real-time Matchering as a VST3 (GPLv3).
    Flow: load reference -> analyse -> Learn (listen to the live track once) -> match is locked and applied live.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Core/Matcher.h"
#include "Core/HyraxLimiter.h"

class MatchMasterProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    enum class Phase { needReference, analysingReference, readyToLearn, listening, analysingTrack, matched };

    MatchMasterProcessor();
    ~MatchMasterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MatchMaster"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- GUI API (message thread)
    void loadReference (const juce::File&);
    void startLearning();
    void stopLearning();

    Phase getPhase() const noexcept { return phase.load(); }
    double getCaptureSeconds() const noexcept;
    double getMaxCaptureSeconds() const noexcept;

    struct Snapshot
    {
        std::vector<float> midCurve, sideCurve;
        double curveRate = 44100.0;
        int curveFft = 4096;
        double gainDb = 0.0, refLevelDb = 0.0;
        juce::String referenceName, message;
        bool messageIsError = false;
    };
    Snapshot getSnapshot() const;

    std::atomic<float> meterL { 0.0f }, meterR { 0.0f }, meterGr { 1.0f };

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void computeMatch();           // worker thread
    void rebuildFirs (bool force); // message thread
    void setMessage (const juce::String&, bool isError);

    mm::Config config;
    juce::AudioFormatManager formats;
    juce::ThreadPool pool { 1 };

    mutable juce::CriticalSection dataLock;
    std::shared_ptr<mm::ReferenceProfile> reference;
    std::shared_ptr<mm::MatchResult> result, pendingResult;
    juce::String referenceName, message;
    bool messageIsError = false;
    std::atomic<bool> resultReady { false };
    std::atomic<Phase> phase { Phase::needReference };

    // capture of the live track during Learn
    juce::AudioBuffer<float> capture, captureCopy;
    std::atomic<int> capturePos { 0 };
    std::atomic<bool> capturing { false };
    double captureRate = 44100.0;

    // real-time engine
    double hostRate = 44100.0;
    int maxBlock = 512, firSize = 4096;
    juce::dsp::Convolution midConv, sideConv;
    juce::AudioBuffer<float> midBuf, sideBuf;
    mm::HyraxLimiter limiter;
    juce::SmoothedValue<float> loudGain, finalGain;
    std::atomic<float> matchGainDb { 0.0f }, refLevelDb { 0.0f };
    float builtMidAmt = -1.0f, builtSideAmt = -1.0f;

    std::atomic<float>* pMatch = nullptr;
    std::atomic<float>* pMidEq = nullptr;
    std::atomic<float>* pSideEq = nullptr;
    std::atomic<float>* pLoudness = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchMasterProcessor)
};
