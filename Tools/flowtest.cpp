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

void playThrough (MacAndCheeseProcessor& p, const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>* out)
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

// Plays a track through the plugin on its own thread, paced in real time like a DAW audio thread,
// and records a brightness indicator (HF / total energy, dB) per block.
struct RealtimeRunner : juce::Thread
{
    RealtimeRunner (MacAndCheeseProcessor& p, const juce::AudioBuffer<float>& t, double r)
        : juce::Thread ("rt-audio"), proc (p), track (t), rate (r) {}

    void run() override
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        int pos = 0;
        const double blockMs = 512.0 * 1000.0 / rate;
        auto next = juce::Time::getMillisecondCounterHiRes();
        while (! threadShouldExit())
        {
            if (pos + 512 > track.getNumSamples()) pos = 0;
            for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, track, ch, pos, 512);
            pos += 512;
            proc.processBlock (block, midi);
            double hf = 0, all = 0;
            for (int i = 1; i < 512; ++i)
            {
                const float x = block.getSample (0, i), d = x - block.getSample (0, i - 1);
                hf += d * d; all += x * x;
            }
            {
                const juce::ScopedLock sl (lock);
                times.push_back (juce::Time::getMillisecondCounterHiRes());
                values.push_back (10 * std::log10 ((hf + 1e-20) / (all + 1e-20)));
            }
            next += blockMs;
            const auto wait = next - juce::Time::getMillisecondCounterHiRes();
            if (wait > 0) juce::Thread::sleep ((int) wait);
        }
    }
    double average (double fromMs, double toMs)
    {
        const juce::ScopedLock sl (lock);
        double acc = 0; int n = 0;
        for (size_t i = 0; i < times.size(); ++i)
            if (times[i] >= fromMs && times[i] < toMs) { acc += values[i]; ++n; }
        return n > 0 ? acc / n : 0.0;
    }
    MacAndCheeseProcessor& proc;
    const juce::AudioBuffer<float>& track;
    double rate;
    juce::CriticalSection lock;
    std::vector<double> times, values;
};

void snapshot (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << "  image " << f.getFullPathName() << "\n";
}
} // namespace

// PREP / Unclump on a synthetic bad mix: a mud band (~250 Hz) that swells +8 dB for 1 s every 4 s,
// against a reference whose mud is steady. Returns failures.
int prepTest()
{
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n"; if (! ok) ++failures; };
    const double sr = 44100;
    const int n = (int) (sr * 30);
    auto make = [&] (bool swell, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::normal_distribution<float> nd;
        juce::dsp::IIR::Filter<float> bp (juce::dsp::IIR::Coefficients<float>::makeBandPass (sr, 250.0, 1.0));
        std::vector<float> m ((size_t) n), sd ((size_t) n);
        float lp = 0;
        for (int i = 0; i < n; ++i)
        {
            lp += 0.3f * (nd (rng) - lp);
            const float mudGain = swell && std::fmod (i / sr, 4.0) < 1.0 ? 2.5f : 1.0f;
            m[(size_t) i] = 0.05f * lp + 0.08f * mudGain * bp.processSample (nd (rng));
            sd[(size_t) i] = 0.01f * nd (rng);
        }
        return std::pair<std::vector<float>, std::vector<float>> (m, sd);
    };
    auto ref = make (false, 7), bad = make (true, 8);
    const auto rs = mm::Prep::analyse (ref.first, ref.second, sr);
    const auto ts = mm::Prep::analyse (bad.first, bad.second, sr);
    auto spread = [] (const mm::BandStats& st) { return st.q[0][1][3] - st.q[0][1][0]; }; // mid, Mud band: p95 - p50
    auto run = [&] (std::pair<std::vector<float>, std::vector<float>> x, const mm::BandStats& t, float amount)
    {
        mm::Prep p;
        p.prepare (sr, 512);
        p.setStats (t, rs);
        mm::PrepSettings set;
        set.amount = amount;
        p.process (x.first.data(), x.second.data(), n, set);
        const auto red = p.popReduction();
        return std::make_pair (mm::Prep::analyse (x.first, x.second, sr), red);
    };
    const auto fixed = run (bad, ts, 1.0f);
    const auto same = run (ref, rs, 1.0f);
    std::cout << "  info  Unclump, Mud band swell (p95-p50): reference " << juce::String (spread (rs), 1) << " dB, bad mix "
              << juce::String (spread (ts), 1) << " dB -> " << juce::String (spread (fixed.first), 1) << " dB after Unclump (max cut "
              << juce::String (fixed.second[1], 1) << " dB)\n";
    check (spread (fixed.first) < spread (ts) - 1.5f, "Unclump evens out a swelling mud band");
    float maxSame = 0;
    for (auto r : same.second) maxSame = std::max (maxSame, r);
    check (maxSame < 1.5f, "Unclump leaves a mix that already matches the reference (max cut " + juce::String (maxSame, 1) + " dB)");
    check (fixed.second[3] < 1.5f && fixed.second[4] < 1.5f, "Unclump leaves healthy bands alone (Bite/Fizz)");
    return failures;
}

int loadVst3 (const juce::String& path)
{
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> found;
    fmt.findAllTypesForFile (found, path);
    if (found.isEmpty()) { std::cout << "  FAIL  no VST3 plugin found in " << path << "\n"; return 1; }
    std::cout << "  PASS  found VST3: " << found[0]->name << " by " << found[0]->manufacturerName << " (" << found[0]->category << "), uid "
              << juce::String::toHexString (found[0]->uniqueId) << "\n";
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
    int failures = prepTest();
    auto check = [&] (bool ok, const juce::String& what) { std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n"; if (! ok) ++failures; };

    using LS = MacAndCheeseProcessor::LearnState;
    const double rate = 48000;
    auto proc = std::make_unique<MacAndCheeseProcessor>();
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
    {
        // play in 2 s chunks, letting the GUI/timer run in between (like real time), up to two passes of the track
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> block (2, 512);
        const int chunk = (int) (2.0 * rate);
        bool earlyEnough = false;
        juce::String timeline;
        for (int pass = 0; pass < 2 && ! proc->learnEnough.load(); ++pass)
            for (int start = 0; start + chunk <= track.getNumSamples() && ! proc->learnEnough.load(); start += chunk)
            {
                for (int pos = start; pos + 512 <= start + chunk; pos += 512)
                {
                    for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, track, ch, pos, 512);
                    proc->processBlock (block, midi);
                }
                pump (250);
                const double secs = proc->getCaptureSeconds();
                if (secs <= 12.5 && proc->learnEnough.load()) earlyEnough = true;
                if (proc->learnDeviationDb.load() >= 0.0f) timeline << juce::roundToInt (secs) << "s:" << juce::String (proc->learnDeviationDb.load(), 2) << " ";
            }
        std::cout << "  info  learn quality (spectrum change per check): " << timeline << "\n";
        check (! earlyEnough, "Learn quality: not 'enough' after only 12 s");
        check (proc->learnEnough.load(), "Learn quality: says 'enough' once the spectrum is stable (" + juce::String (proc->getCaptureSeconds(), 0) + " s)");
        std::cout << "  info  message: " << proc->getSnapshot().message << "\n";
    }
    pump (100);
    snapshot (*editor, outDir.getChildFile ("2-listening.png"));
    proc->stopLearning();
    bool done = false;
    for (int t = 0; t < 600 && ! done; ++t) { auto s = proc->getSnapshot(); done = proc->getLearnState() == LS::idle && s.slots[0].matched && s.slots[1].matched; pump (50); }
    check (done, "one Learn matched both profiles");
    pump (500);

    // ---- knob responsiveness: sweep Mid/Side EQ 0 -> 100 % over 2 s while audio runs in real time.
    //      Pass 1: GUI thread free. Pass 2: GUI thread busy (as in hosts that block it during a mouse drag).
    {
        auto setEq = [&] (float v) { proc->apvts.getParameter ("midEq")->setValueNotifyingHost (v); proc->apvts.getParameter ("sideEq")->setValueNotifyingHost (v); };
        auto now = [] { return juce::Time::getMillisecondCounterHiRes(); };
        for (int pass = 0; pass < 2; ++pass)
        {
            const bool guiBusy = pass == 1;
            setEq (0.0f);
            pump (600);
            RealtimeRunner rt (*proc, track, rate);
            rt.startThread (juce::Thread::Priority::highest);
            pump (800);
            const double t0 = now();
            pump (400);
            const double start = rt.average (t0, now());
            const double sweep0 = now();
            for (int step = 1; step <= 60; ++step)
            {
                setEq (step / 60.0f);
                if (guiBusy) juce::Thread::sleep (33); else pump (33);
            }
            const double sweep1 = now();
            if (guiBusy) juce::Thread::sleep (300); else pump (300);
            const double endOfDrag = rt.average (sweep1, now());          // knob still "held"
            const double halfway = rt.average (sweep0 + 900, sweep0 + 1100); // knob at ~50 %
            pump (800);                                                    // "mouse released"
            const double t2 = now();
            pump (400);
            const double final = rt.average (t2, now());
            rt.stopThread (2000);
            const double span = final - start;
            const double atEnd = std::abs (span) > 1e-6 ? (endOfDrag - start) / span : 1.0;
            const double atHalf = std::abs (span) > 1e-6 ? (halfway - start) / span : 1.0;
            std::cout << "  info  knob sweep, GUI thread " << (guiBusy ? "busy" : "free") << ": EQ change heard "
                      << juce::roundToInt (atHalf * 100) << "% at mid-turn, " << juce::roundToInt (atEnd * 100)
                      << "% before release (brightness " << juce::String (start, 1) << " -> " << juce::String (final, 1) << " dB)\n";
            check (atEnd > 0.85 && atHalf > 0.2 && atHalf < 0.97, // halfway: moving, not jumped (max correction caps early)
                   juce::String ("sound follows the EQ knobs while turning (GUI thread ") + (guiBusy ? "busy)" : "free)"));
        }
        setEq (1.0f);
        pump (600);
    }

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

    // level-match monitor: EQ only, at the track's own loudness
    {
        auto* lm = proc->apvts.getParameter ("levelMatch");
        lm->setValueNotifyingHost (1.0f);
        pump (300);
        auto lmOut = measure ("level match (EQ only)");
        mm::Config c;
        const double trackLoud = 20 * std::log10 (mm::analyzeTarget (mm::toStereoAtRate (track, rate, 44100), c).matchRms);
        std::cout << "  info  original track loudness " << juce::String (trackLoud, 2) << " dB\n";
        check (std::abs (lmOut.first - trackLoud) < 0.5, "Level match: EQ'd track at the original loudness");
        lm->setValueNotifyingHost (0.0f);
        pump (300);
    }

    // clipper before the limiter
    auto setClip = [&] (float db) { auto* p = proc->apvts.getParameter ("clip"); p->setValueNotifyingHost (p->convertTo0to1 (db)); pump (300); };
    proc->maxGrDb = 0.0f; proc->maxClipDb = 0.0f;
    measure ("clip 0 dB");
    const float grNoClip = proc->maxGrDb.load();
    check (proc->gainIntoLimiterDb.load() > 1.0f, "gain into the limiter shown (" + juce::String (proc->gainIntoLimiterDb.load(), 1) + " dB)");
    check (proc->maxClipDb.load() < 0.01f, "Clip 0 dB: clipper does nothing");
    setClip (3.0f);
    proc->maxGrDb = 0.0f; proc->maxClipDb = 0.0f;
    auto clipped = measure ("clip 3 dB");
    const float grClip = proc->maxGrDb.load(), shaved = proc->maxClipDb.load();
    std::cout << "  info  limiter max GR " << grNoClip << " dB -> " << grClip << " dB with Clip 3 dB; clipper shaved max " << shaved << " dB\n";
    check (shaved > 2.0f && shaved < 4.5f && grNoClip - grClip > 1.5f, "Clip 3 dB: clipper takes ~3 dB, limiter does less");
    check (clipped.second <= -0.9f, "Clip 3 dB: true peak still under the -1 dBTP ceiling");
    check (std::abs (clipped.first - a.first) < 1.0, "Clip 3 dB: loudness still matched");
    proc->apvts.getParameter ("limiter")->setValueNotifyingHost (0.0f);
    pump (600);
    auto clipNoLim = measure ("clip 3 dB, limiter off");
    check (clipNoLim.second <= -0.7f, "Clip 3 dB, limiter off: output stays under the ceiling");
    proc->apvts.getParameter ("limiter")->setValueNotifyingHost (1.0f);
    setClip (0.0f);
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
        if (auto* btn = dynamic_cast<juce::TextButton*> (c); btn != nullptr && btn->getButtonText() == "Recipe")
            btn->triggerClick();
    pump (300);
    snapshot (*editor, outDir.getChildFile ("4-advanced.png"));

    // PREP on: the master is re-matched on the prepped track, loudness and ceiling still hold
    {
        auto setP = [&] (const char* id, float v) { auto* p = proc->apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        proc->apvts.getParameter ("limiter")->setValueNotifyingHost (1.0f);
        const auto before = measure ("PREP off (baseline, same settings)");
        setP ("unclump", 100.0f);
        setP ("monoBase", 120.0f);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        const double g0 = proc->getSnapshot().gainDb;
        pump (8000); // re-match runs in the background after a 0.5 s pause
        std::cout << "  info  waited " << juce::roundToInt ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0) << " s for the re-match; loudness gain " << g0 << " -> " << proc->getSnapshot().gainDb << " dB\n";
        auto prepped = measure ("PREP on (Unclump 100 %, Mono base 120 Hz)");
        check (std::abs (prepped.first - before.first) < 1.0 && prepped.second <= -0.9f, "PREP on: loudness kept (re-matched on the prepped track), ceiling holds");
    }

    // save with the project -> reopen in a fresh instance: same sound without Learn
    {
        juce::MemoryBlock state;
        proc->getStateInformation (state);
        std::cout << "  info  project state size " << (int) (state.getSize() / 1024) << " KB\n";
        auto proc2 = std::make_unique<MacAndCheeseProcessor>();
        proc2->setPlayConfigDetails (2, 2, rate, 512);
        proc2->setStateInformation (state.getData(), (int) state.getSize());
        proc2->prepareToPlay (rate, 512);
        pump (1500);
        const auto s2 = proc2->getSnapshot();
        check (s2.slots[0].matched && s2.slots[1].matched && s2.slots[0].name == "bright-ref" && proc2->getActiveSlot() == 0,
               "reopened project: references, names and matches restored");
        check (! proc2->hasLearnedTrack(), "reopened project: learned audio is not stored (known limit)");
        std::cout << "  info  message after reopen: " << s2.message << "\n";
        juce::AudioBuffer<float> o1 (2, track.getNumSamples()), o2 (2, track.getNumSamples());
        playThrough (*proc, track, nullptr);
        playThrough (*proc, track, &o1);
        playThrough (*proc2, track, nullptr);
        playThrough (*proc2, track, &o2);
        double maxDiff = 0, maxOut = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < track.getNumSamples(); ++i)
            {
                maxDiff = std::max (maxDiff, (double) std::abs (o1.getSample (ch, i) - o2.getSample (ch, i)));
                maxOut = std::max (maxOut, (double) std::abs (o1.getSample (ch, i)));
            }
        std::cout << "  info  max difference original vs reopened: " << juce::String (juce::Decibels::gainToDecibels (maxDiff / maxOut, -200.0), 1) << " dB below peak\n";
        check (maxDiff < maxOut * 1e-3, "reopened project sounds the same as before saving");
        proc2.reset();
    }

    editor.reset();
    proc.reset();
    std::cout << (failures == 0 ? "ALL FLOW TESTS PASSED\n" : "SOME FLOW TESTS FAILED\n");
    return failures == 0 ? 0 : 1;
}
