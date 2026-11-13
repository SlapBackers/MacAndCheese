#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

namespace ui
{
    namespace colours
    {
        const juce::Colour bg { 0xff0f1115 }, panel { 0xff171a20 }, line { 0xff272b34 },
                           text { 0xffe8eaef }, dim { 0xff8a90a0 }, accent { 0xfff5a524 },
                           accent2 { 0xff36c9b4 }, error { 0xffe5534b }, rec { 0xffe5484d };
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override;
    };

    // One reference slot (A/B/C): drop or browse a song, click to select, double-click the word to rename
    class DropZone : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        DropZone();
        std::function<void (const juce::File&)> onFile;
        std::function<void()> onSelect;
        std::function<void (const juce::String&)> onFlavour;
        juce::String letter, fileName;
        bool busy = false, selected = false, loaded = false;
        void setFlavour (const juce::String&);
        void resized() override;
        void browse();

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray&, int, int) override;

    private:
        bool dragOver = false;
        juce::Label flavour;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    // Step 2: Learn / Stop
    class LearnButton : public juce::Component
    {
    public:
        std::function<void()> onClick;
        MatchMasterProcessor::LearnState state = MatchMasterProcessor::LearnState::idle;
        bool learned = false;
        double seconds = 0, maxSeconds = 1;
        float pulse = 0;

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        bool isEnabledForClick() const;
    };

    // Matching EQ curve display (advanced)
    class CurveView : public juce::Component
    {
    public:
        MatchMasterProcessor::Snapshot snap;
        float midAmt = 1, sideAmt = 1, maxCorr = 24;
        void paint (juce::Graphics&) override;
    };

    class Meter : public juce::Component
    {
    public:
        float l = 0, r = 0, gr = 1;
        void paint (juce::Graphics&) override;
    };
}

class MatchMasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MatchMasterEditor (MatchMasterProcessor&);
    ~MatchMasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setAdvanced (bool);

    MatchMasterProcessor& proc;
    ui::LookAndFeel lnf;

    std::array<ui::DropZone, MatchMasterProcessor::numSlots> slotZones;
    ui::LearnButton learnButton;
    juce::Label messageLabel;
    juce::Slider matchKnob;
    juce::Label matchLabel;
    ui::Meter meter;
    juce::TextButton advancedButton { "Advanced" };

    ui::CurveView curve;
    juce::Slider midKnob, sideKnob, loudKnob, smoothKnob, corrKnob;
    juce::Label midLabel, sideLabel, loudLabel, smoothLabel, corrLabel, readout;
    juce::ComboBox eqMode;

    // limiter strip (always visible)
    juce::ToggleButton limiterButton { "Limiter" }, truePeakButton { "True peak" };
    juce::Slider ceilingSlider;
    juce::Label ceilingLabel, limiterReadout;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<Attachment> matchAtt, midAtt, sideAtt, loudAtt, smoothAtt, corrAtt, ceilingAtt;
    std::unique_ptr<ButtonAtt> limiterAtt, truePeakAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;

    bool advanced = false;
    static constexpr int simpleHeight = 470, advancedHeight = 800, width = 700;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchMasterEditor)
};
