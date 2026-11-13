/*
    End-to-end test of the plugin code (no DAW): load reference -> Learn -> matched playback,
    then renders the plugin window to PNGs.   usage: flowtest <output-folder>
*/
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/TruePeak.h"
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

    using LS = MatchMasterProcessor::LearnState;
    const double rate = 48000;
    auto proc = std::make_unique<MatchMasterProcessor>();
    proc->setPlayConfigDetails (2, 2, rate, 512);
    proc->prepareToPlay (rate, 512);
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    pump (200);
    int latency = proc->getLatencySamples();
    std::cout << "  info  latency (linear EQ) " << latency << " samples (" << latency * 1000.0 / rate << " ms)\n";

    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc->createEditor());
    snapshot (*editor, outDir.getChildFile ("1-start.png"));

    auto writeWav = [&] (const juce::AudioBuffer<float>& b, double r, const juce::String& name)
    {
        auto f = outDir.getChildFile (name);
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), r, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return f;
    };
    auto refBright = makeSong (44100, 45, 0.6f, 0.30f, 0.5f, 2);
    auto refWarm = makeSong (44100, 45, 0.15f, 0.25f, 0.1f, 3);
    proc->loadReference (0, writeWav (refBright, 44100, "bright-ref.wav"));
    proc->loadReference (1, writeWav (refWarm, 44100, "warm-ref.wav"));
    auto waitSlots = [&] { for (int t = 0; t < 400; ++t) { auto s = proc->getSnapshot(); if (s.slots[0].loaded && s.slots[1].loaded && ! s.slots[0].busy && ! s.slots[1].busy) return true; pump (50); } return false; };
    check (waitSlots(), "two references analysed into A and B");
    const auto snap0 = proc->getSnapshot();
    std::cout << "  info  flavours: A = " << snap0.slots[0].flavour << ", B = " << snap0.slots[1].flavour << "\n";

    auto track = makeSong (rate, 40, 0.08f, 0.05f, 0.2f, 1);
    auto transparency = [&]
    {
        juce::AudioBuffer<float> out (2, track.getNumSamples());
        out.clear();
        playThrough (*proc, track, nullptr);
        playThrough (*proc, track, &out);
        double err = 0;
        for (int i = latency + 1000; i < track.getNumSamples() - 1000; ++i)
            err = std::max (err, (double) std::abs (out.getSample (0, i) - track.getSample (0, i - latency)));
        return err;
    };
    check (transparency() < 1e-3, "unmatched plugin is transparent");

    proc->selectSlot (0);
    proc->startLearning();
    check (proc->getLearnState() == LS::listening, "Learn starts listening");
    playThrough (*proc, track, nullptr);
    pump (100);
    snapshot (*editor, outDir.getChildFile ("2-listening.png"));
    proc->stopLearning();
    bool done = false;
    for (int t = 0; t < 600 && ! done; ++t) { auto s = proc->getSnapshot(); done = proc->getLearnState() == LS::idle && s.slots[0].matched && s.slots[1].matched; pump (50); }
    check (done, "one Learn matched both profiles");
    pump (500);

    auto measure = [&] (const juce::String& label)
    {
        playThrough (*proc, track, nullptr);
        juce::AudioBuffer<float> out (2, track.getNumSamples());
        playThrough (*proc, track, &out);
        juce::AudioBuffer<float> aligned (2, out.getNumSamples() - latency);
        for (int ch = 0; ch < 2; ++ch) aligned.copyFrom (ch, 0, out, ch, latency, aligned.getNumSamples());
        float tp = 0;
        for (int ch = 0; ch < 2; ++ch) tp = std::max (tp, mm::TruePeakDetector::measure (aligned.getReadPointer (ch), aligned.getNumSamples()));
        mm::Config cfg;
        const double loudDb = 20 * std::log10 (mm::analyzeTarget (mm::toStereoAtRate (aligned, rate, 44100), cfg).matchRms);
        std::cout << "  info  " << label << ": loudness " << juce::String (loudDb, 2) << " dB, true peak " << juce::String (juce::Decibels::gainToDecibels (tp), 2) << " dBTP\n";
        return std::pair<double, float> (loudDb, juce::Decibels::gainToDecibels (tp));
    };
    mm::Config cfg;
    auto refLoud = [&] (const juce::AudioBuffer<float>& r) { auto p = mm::analyzeReference (r, cfg); return 20 * std::log10 (p.matchRms * p.finalAmplitudeCoef); };

    auto a = measure ("profile A (bright)");
    check (std::abs (a.first - refLoud (refBright)) < 1.5, "A matches its reference loudness (" + juce::String (refLoud (refBright), 2) + " dB)");
    check (a.second <= -0.9f, "limiter holds the -1 dBTP ceiling");
    proc->selectSlot (1);
    pump (400);
    auto b = measure ("profile B (warm)");
    check (std::abs (b.first - refLoud (refWarm)) < 1.5, "B matches its reference loudness (" + juce::String (refLoud (refWarm), 2) + " dB)");

    // limiter off (on the loud profile A): stays under the ceiling, suggests the missing gain
    proc->selectSlot (0);
    proc->apvts.getParameter ("limiter")->setValueNotifyingHost (0.0f);
    pump (400);
    auto off = measure ("limiter off");
    std::cout << "  info  suggestion: +" << proc->suggestionDb.load() << " dB into your limiter\n";
    check (off.second < 0.5f && proc->suggestionDb.load() > 0.0f, "limiter off keeps headroom and suggests gain");
    proc->apvts.getParameter ("limiter")->setValueNotifyingHost (1.0f);

    // Natural EQ: low latency
    proc->apvts.getParameter ("eqMode")->setValueNotifyingHost (1.0f);
    pump (400);
    latency = proc->getLatencySamples();
    std::cout << "  info  latency (natural EQ) " << latency << " samples (" << latency * 1000.0 / rate << " ms)\n";
    std::pair<double, float> nat;
    proc->selectSlot (1);
    pump (400);
    nat = measure ("profile B, natural EQ");
    check (latency < 1000 && std::abs (nat.first - b.first) < 1.0, "Natural EQ: low latency, same loudness");

    proc->apvts.getParameter ("match")->setValueNotifyingHost (0.0f);
    pump (400);
    check (transparency() < 1e-3, "Match knob at 0% is transparent");
    proc->apvts.getParameter ("match")->setValueNotifyingHost (1.0f);
    proc->apvts.getParameter ("smoothing")->setValueNotifyingHost (0.6f);
    proc->apvts.getParameter ("eqMode")->setValueNotifyingHost (0.0f);
    proc->selectSlot (0);
    pump (1500);
    playThrough (*proc, track, nullptr);
    pump (300);
    snapshot (*editor, outDir.getChildFile ("3-matched.png"));
    for (auto* c : editor->getChildren())
        if (auto* btn = dynamic_cast<juce::TextButton*> (c); btn != nullptr && btn->getButtonText() == "Advanced")
            btn->triggerClick();
    pump (300);
    snapshot (*editor, outDir.getChildFile ("4-advanced.png"));

    editor.reset();
    proc.reset();
    std::cout << (failures == 0 ? "ALL FLOW TESTS PASSED\n" : "SOME FLOW TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
