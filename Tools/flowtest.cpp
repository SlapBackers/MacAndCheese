/*
    End-to-end test of the plugin code (no DAW): load reference -> Learn -> matched playback,
    then renders the plugin window to PNGs.   usage: flowtest <output-folder>
*/
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <random>

namespace
{
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
        const float env = level * (0.35f + 0.65f * (std::fmod (t, 20.0) > 8.0 ? 1.0f : 0.3f));
        const float c = nd (rng), s = nd (rng) * width;
        lpL += tilt * ((c + s) - lpL);
        lpR += tilt * ((c - s) - lpR);
        b.setSample (0, i, lpL * env);
        b.setSample (1, i, lpR * env);
    }
    return b;
}

bool waitFor (MatchMasterProcessor& p, MatchMasterProcessor::Phase want, int ms)
{
    for (int t = 0; t < ms; t += 50)
    {
        if (p.getPhase() == want) return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
    return p.getPhase() == want;
}

void playThrough (MatchMasterProcessor& p, const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>* out)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> block (2, 512);
    for (int pos = 0; pos + 512 <= in.getNumSamples(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, in, ch, pos, 512);
        p.processBlock (block, midi);
        if (out != nullptr)
            for (int ch = 0; ch < 2; ++ch) out->copyFrom (ch, pos, block, ch, 0, 512);
        if ((pos / 512) % 64 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
}

double rmsDb (const juce::AudioBuffer<float>& b, int start, int len)
{
    double acc = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = start; i < start + len; ++i) acc += b.getSample (ch, i) * b.getSample (ch, i);
    return 10 * std::log10 (acc / (2.0 * len) + 1e-30);
}

void snapshot (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << "  image " << f.getFullPathName() << "\n";
}
} // namespace

int loadVst3 (const juce::String& path)
{
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> found;
    fmt.findAllTypesForFile (found, path);
    if (found.isEmpty()) { std::cout << "  FAIL  no VST3 plugin found in " << path << "\n"; return 1; }
    std::cout << "  PASS  found VST3: " << found[0]->name << " by " << found[0]->manufacturerName << " (" << found[0]->category << ")\n";
    juce::String err;
    auto inst = fmt.createInstanceFromDescription (*found[0], 48000, 512, err);
    if (inst == nullptr) { std::cout << "  FAIL  could not instantiate: " << err << "\n"; return 1; }
    inst->prepareToPlay (48000, 512);
    juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
    for (int i = 0; i < 200; ++i) { for (int ch = 0; ch < 2; ++ch) for (int j = 0; j < 512; ++j) b.setSample (ch, j, 0.1f * std::sin (j * 0.05f)); inst->processBlock (b, m); }
    std::cout << "  PASS  instantiated, processed audio, latency reported to host: " << inst->getLatencySamples() << " samples\n";
    std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
    std::cout << (ed != nullptr ? "  PASS  editor opens " : "  FAIL  no editor ") << (ed ? ed->getWidth() : 0) << "x" << (ed ? ed->getHeight() : 0) << "\n";
    ed.reset(); inst->releaseResources(); inst.reset();
    return 0;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--load-vst3")
        return loadVst3 (argv[2]);
    const juce::File outDir (argc > 1 ? argv[1] : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n"; if (! ok) ++failures; };

    using Phase = MatchMasterProcessor::Phase;
    const double rate = 48000;
    auto proc = std::make_unique<MatchMasterProcessor>();
    proc->setPlayConfigDetails (2, 2, rate, 512);
    proc->prepareToPlay (rate, 512);
    const int latency = proc->getLatencySamples();
    std::cout << "  info  latency " << latency << " samples (" << latency * 1000.0 / rate << " ms)\n";

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc->createEditor());
    snapshot (*editor, outDir.getChildFile ("1-start.png"));

    // Step 1-2: reference file
    auto ref = makeSong (44100, 45, 0.6f, 0.30f, 0.5f, 2);
    const auto refFile = outDir.getChildFile ("test-reference.wav");
    {
        refFile.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (refFile), 44100, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (ref, 0, ref.getNumSamples());
    }
    proc->loadReference (refFile);
    check (waitFor (*proc, Phase::readyToLearn, 20000), "reference analysed -> ready to learn");

    // Before matching: transparent apart from the delay
    auto track = makeSong (rate, 40, 0.08f, 0.05f, 0.2f, 1);
    {
        juce::AudioBuffer<float> out (2, track.getNumSamples());
        out.clear();
        playThrough (*proc, track, &out);
        double err = 0;
        for (int i = latency + 1000; i < track.getNumSamples() - 1000; ++i)
            err = std::max (err, (double) std::abs (out.getSample (0, i) - track.getSample (0, i - latency)));
        check (err < 1e-3, "unmatched plugin is transparent (max diff " + juce::String (err, 6) + ")");
    }

    // Step 3: Learn
    proc->startLearning();
    check (proc->getPhase() == Phase::listening, "Learn starts listening");
    playThrough (*proc, track, nullptr);
    editor->setSize (editor->getWidth(), editor->getHeight());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    snapshot (*editor, outDir.getChildFile ("2-listening.png"));
    proc->stopLearning();
    check (waitFor (*proc, Phase::matched, 30000), "track learned -> matched");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500); // FIR swap + crossfade

    // Matched playback
    {
        playThrough (*proc, track, nullptr); // let gain smoothing / IR crossfade settle
        juce::AudioBuffer<float> out (2, track.getNumSamples());
        out.clear();
        playThrough (*proc, track, &out);
        const int s = (int) (rate * 10), len = (int) (rate * 8); // a loud section
        const double refDb = rmsDb (ref, (int) (44100 * 10), (int) (44100 * 8));
        const double inDb = rmsDb (track, s, len), outDb = rmsDb (out, s + latency, len);
        std::cout << "  info  loud section RMS: track " << inDb << " dB -> plugin output " << outDb << " dB (reference " << refDb << " dB)\n";
        {
            mm::Config cfg;
            juce::AudioBuffer<float> aligned (2, out.getNumSamples() - latency);
            for (int ch = 0; ch < 2; ++ch) aligned.copyFrom (ch, 0, out, ch, latency, aligned.getNumSamples());
            const auto o = mm::analyzeTarget (mm::toStereoAtRate (aligned, rate, 44100), cfg);
            const auto r = mm::analyzeReference (mm::toStereoAtRate (ref, 44100, 44100), cfg);
            const double oDb = 20 * std::log10 (o.matchRms), rDb = 20 * std::log10 (r.matchRms * r.finalAmplitudeCoef);
            auto msDb = [] (const juce::AudioBuffer<float>& b, int st, int n, bool side) {
                double a = 0; for (int i = st; i < st + n; ++i) { const double m = 0.5 * (b.getSample (0, i) + b.getSample (1, i)); const double v = side ? m - b.getSample (1, i) : m; a += v * v; }
                return 10 * std::log10 (a / n + 1e-30); };
            std::cout << "  info  section mid/side dB: ref " << msDb (ref, 441000, 352800, false) << " / " << msDb (ref, 441000, 352800, true)
                      << "   out " << msDb (out, s + latency, len, false) << " / " << msDb (out, s + latency, len, true) << "\n";
            std::cout << "  info  Matchering loudness measure: output " << oDb << " dB, reference " << rDb << " dB\n";
            check (std::abs (oDb - rDb) < 1.0, "live output loudness matches the reference (Matchering measure)");
        }
        float peak = 0;
        for (int ch = 0; ch < 2; ++ch) peak = std::max (peak, out.getMagnitude (ch, 0, out.getNumSamples()));
        check (peak <= 1.0f, "no output above 0 dBFS (peak " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");

        // Match knob at 0 -> transparent again
        proc->apvts.getParameter ("match")->setValueNotifyingHost (0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        playThrough (*proc, track, nullptr);
        juce::AudioBuffer<float> dry (2, track.getNumSamples());
        playThrough (*proc, track, &dry);
        double err = 0;
        for (int i = latency + 1000; i < track.getNumSamples() - 1000; ++i)
            err = std::max (err, (double) std::abs (dry.getSample (0, i) - track.getSample (0, i - latency)));
        check (err < 1e-3, "Match knob at 0% is transparent (max diff " + juce::String (err, 6) + ")");
        proc->apvts.getParameter ("match")->setValueNotifyingHost (0.85f);
        playThrough (*proc, track, nullptr);
    }

    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    snapshot (*editor, outDir.getChildFile ("3-matched.png"));
    for (auto* c : editor->getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Advanced")
            b->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    snapshot (*editor, outDir.getChildFile ("4-advanced.png"));

    editor.reset();
    proc.reset();
    std::cout << (failures == 0 ? "ALL FLOW TESTS PASSED\n" : "SOME FLOW TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
