#include "PluginEditor.h"

using namespace ui;
using Phase = MatchMasterProcessor::Phase;

// ================================================================== LookAndFeel

LookAndFeel::LookAndFeel()
{
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::bg);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.4f));
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, colours::panel);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto area = bounds.withSizeKeepingCentre (size, size).reduced (size * 0.06f);
    const float thick = juce::jmax (3.0f, size * 0.045f);
    const float radius = area.getWidth() * 0.5f - thick;
    const auto c = area.getCentre();
    const float angle = start + pos * (end - start);
    const bool big = size > 150.0f;
    const auto accent = s.getProperties().contains ("side") ? colours::accent2 : colours::accent;

    juce::Path track, value;
    track.addCentredArc (c.x, c.y, radius, radius, 0, start, end, true);
    value.addCentredArc (c.x, c.y, radius, radius, 0, start, angle, true);
    g.setColour (colours::line);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float body = radius - thick * (big ? 2.2f : 1.6f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2f39), c.x, c.y - body,
                                             juce::Colour (0xff15181d), c.x, c.y + body, false));
    g.fillEllipse (c.x - body, c.y - body, body * 2, body * 2);
    g.setColour (colours::line.brighter (0.2f));
    g.drawEllipse (c.x - body, c.y - body, body * 2, body * 2, 1.0f);

    if (big)
    {
        // pointer dot near the rim so it never crosses the value
        const auto dot = c.getPointOnCircumference (body * 0.86f, angle);
        g.setColour (accent);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (dot));
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (size * 0.15f, juce::Font::bold));
        g.drawText (juce::String (juce::roundToInt (s.getValue())) + "%", area.withSizeKeepingCentre (body * 1.4f, body * 0.6f),
                    juce::Justification::centred);
    }
    else
    {
        const auto tip = c.getPointOnCircumference (body * 0.85f, angle);
        const auto base = c.getPointOnCircumference (body * 0.3f, angle);
        g.setColour (colours::text);
        g.drawLine ({ base, tip }, 2.5f);
    }
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? colours::accent : (down ? colours::line.brighter (0.2f) : over ? colours::line : colours::panel));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? colours::accent : colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::FontOptions (juce::jmin (14.0f, h * 0.5f), juce::Font::bold);
}

// ================================================================== DropZone

void DropZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool hot = dragOver || isMouseOver();
    g.setColour (hot ? colours::panel.brighter (0.06f) : colours::panel);
    g.fillRoundedRectangle (r, 10.0f);

    juce::Path border;
    border.addRoundedRectangle (r, 10.0f);
    if (fileName.isEmpty())
    {
        const float dashes[] = { 6.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, border, dashes, 2);
        g.setColour (hot ? colours::accent : colours::dim.withAlpha (0.6f));
        g.fillPath (dashed);
    }
    else
    {
        g.setColour (colours::accent.withAlpha (0.7f));
        g.strokePath (border, juce::PathStrokeType (1.5f));
    }

    auto inner = getLocalBounds().reduced (16, 12);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("1  REFERENCE", inner.removeFromTop (16), juce::Justification::left);

    g.setColour (colours::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    const auto title = busy ? juce::String ("Analysing...") : fileName.isEmpty() ? juce::String ("Drop a mastered song") : fileName;
    g.drawFittedText (title, inner.removeFromTop (inner.getHeight() / 2 + 6), juce::Justification::bottomLeft, 2);

    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (fileName.isEmpty() ? "or click to browse  (wav, aiff, flac, mp3)" : "click or drop to change",
                inner, juce::Justification::topLeft);
}

void DropZone::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference song", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile() && onFile)
                                  onFile (fc.getResult());
                          });
}

void DropZone::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    if (! files.isEmpty() && onFile)
        onFile (juce::File (files[0]));
}

// ================================================================== LearnButton

bool LearnButton::isEnabledForClick() const
{
    return phase == Phase::readyToLearn || phase == Phase::matched || phase == Phase::listening;
}

void LearnButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool enabled = isEnabledForClick();
    const bool listening = phase == Phase::listening;

    g.setColour (listening ? colours::rec.withAlpha (0.16f) : colours::panel.brighter (enabled && isMouseOver() ? 0.06f : 0.0f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (listening ? colours::rec : enabled ? colours::line.brighter (0.3f) : colours::line);
    g.drawRoundedRectangle (r, 10.0f, 1.5f);

    auto inner = getLocalBounds().reduced (16, 10);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("2  LEARN YOUR TRACK", inner.removeFromTop (16), juce::Justification::left);

    // icon
    auto icon = inner.removeFromLeft (28).toFloat().withSizeKeepingCentre (18, 18);
    if (listening)
    {
        g.setColour (colours::rec.withAlpha (0.55f + 0.45f * pulse));
        g.fillRoundedRectangle (icon.reduced (2), 2.0f);
    }
    else
    {
        g.setColour (enabled ? colours::rec : colours::dim.withAlpha (0.4f));
        g.fillEllipse (icon);
    }

    juce::String text;
    if (listening)
        text = "Stop  " + juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2);
    else if (phase == Phase::analysingTrack)
        text = "Matching...";
    else if (phase == Phase::matched)
        text = "Re-learn";
    else
        text = "Learn";

    g.setColour (enabled || phase == Phase::analysingTrack ? colours::text : colours::dim.withAlpha (0.5f));
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText (text, inner, juce::Justification::centredLeft);

    if (listening)
    {
        auto bar = getLocalBounds().toFloat().reduced (12, 0).removeFromBottom (5).translated (0, -6);
        g.setColour (colours::line);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (colours::rec);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, seconds / maxSeconds)), 2.0f);
    }
}

void LearnButton::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && isEnabledForClick() && onClick)
        onClick();
}

// ================================================================== CurveView

void CurveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 10.0f);
    auto plot = r.reduced (36, 14).withTrimmedBottom (10);

    const float fMin = 20.0f, fMax = 20000.0f, dbRange = 18.0f;
    auto xFor = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yFor = [&] (float db) { return plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / dbRange); };

    g.setFont (juce::FontOptions (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (colours::line);
        g.drawVerticalLine ((int) xFor (f), plot.getY(), plot.getBottom());
        g.setColour (colours::dim);
        g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (xFor (f) - 20, plot.getBottom() + 2, 40, 12), juce::Justification::centred);
    }
    for (float db : { -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f })
    {
        g.setColour (db == 0 ? colours::dim.withAlpha (0.6f) : colours::line);
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
        g.setColour (colours::dim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX() + 4, yFor (db) - 6, 28, 12),
                    juce::Justification::centredRight);
    }

    auto drawCurve = [&] (const std::vector<float>& c, float amt, juce::Colour col)
    {
        if (c.empty())
            return;
        juce::Path p;
        const double binHz = snap.curveRate / snap.curveFft;
        for (int px = 0; px <= (int) plot.getWidth(); px += 2)
        {
            const float f = fMin * std::pow (fMax / fMin, px / plot.getWidth());
            const double pos = f / binHz;
            const int i0 = juce::jlimit (0, (int) c.size() - 2, (int) pos);
            const float v = c[(size_t) i0] + (float) (pos - i0) * (c[(size_t) i0 + 1] - c[(size_t) i0]);
            const float db = juce::jlimit (-24.0f, 24.0f, 20.0f * std::log10 (juce::jmax (1.0e-4f, std::abs (v)))) * amt;
            const float x = plot.getX() + (float) px, y = yFor (db);
            if (px == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    };
    drawCurve (snap.sideCurve, sideAmt, colours::accent2);
    drawCurve (snap.midCurve, midAmt, colours::accent);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    auto legend = plot.reduced (8, 4).removeFromTop (14);
    g.setColour (colours::accent);
    g.drawText ("MID", legend.removeFromLeft (34), juce::Justification::left);
    g.setColour (colours::accent2);
    g.drawText ("SIDE", legend.removeFromLeft (40), juce::Justification::left);
    if (snap.midCurve.empty())
    {
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("The matching EQ appears here after Learn", getLocalBounds(), juce::Justification::centred);
    }
}

// ================================================================== Meter

void Meter::paint (juce::Graphics& g)
{
    const float peakR = r;
    auto area = getLocalBounds().toFloat();
    auto labels = area.removeFromBottom (16);
    const float w = (area.getWidth() - 8) / 3.0f;
    auto toNorm = [] (float lin) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lin, -60.0f) + 60.0f) / 60.0f); };

    auto bar = [&] (juce::Rectangle<float> b, float norm, juce::Colour c, bool fromTop)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (b, 3.0f);
        auto fill = fromTop ? b.withHeight (b.getHeight() * norm) : b.withTrimmedTop (b.getHeight() * (1 - norm));
        g.setColour (c);
        g.fillRoundedRectangle (fill, 3.0f);
    };
    bar ({ area.getX(), area.getY(), w, area.getHeight() }, toNorm (l), colours::accent2, false);
    bar ({ area.getX() + w + 4, area.getY(), w, area.getHeight() }, toNorm (peakR), colours::accent2, false);
    const float grDb = -juce::Decibels::gainToDecibels (gr, -24.0f);
    bar ({ area.getX() + 2 * (w + 4), area.getY(), w, area.getHeight() }, juce::jlimit (0.0f, 1.0f, grDb / 12.0f), colours::accent, true);

    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("L", labels.withX (area.getX()).withWidth (w), juce::Justification::centred);
    g.drawText ("R", labels.withX (area.getX() + w + 4).withWidth (w), juce::Justification::centred);
    g.drawText ("GR", labels.withX (area.getX() + 2 * (w + 4)).withWidth (w), juce::Justification::centred);
}

// ================================================================== Editor

MatchMasterEditor::MatchMasterEditor (MatchMasterProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);

    dropZone.onFile = [this] (const juce::File& f) { proc.loadReference (f); };
    addAndMakeVisible (dropZone);

    learnButton.onClick = [this]
    {
        if (proc.getPhase() == Phase::listening) proc.stopLearning();
        else proc.startLearning();
    };
    addAndMakeVisible (learnButton);

    messageLabel.setFont (juce::FontOptions (13.0f));
    messageLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (messageLabel);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, bool big)
    {
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        s.setTextBoxStyle (big ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false, 70, 18);
        s.setDoubleClickReturnValue (true, 100.0);
        addAndMakeVisible (s);
        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (big ? 15.0f : 12.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, big ? colours::text : colours::dim);
        addAndMakeVisible (l);
    };
    setupKnob (matchKnob, matchLabel, "MATCH", true);
    setupKnob (midKnob, midLabel, "MID EQ", false);
    setupKnob (sideKnob, sideLabel, "SIDE EQ", false);
    setupKnob (loudKnob, loudLabel, "LOUDNESS", false);
    sideKnob.getProperties().set ("side", true);

    matchAtt = std::make_unique<Attachment> (proc.apvts, "match", matchKnob);
    midAtt = std::make_unique<Attachment> (proc.apvts, "midEq", midKnob);
    sideAtt = std::make_unique<Attachment> (proc.apvts, "sideEq", sideKnob);
    loudAtt = std::make_unique<Attachment> (proc.apvts, "loudness", loudKnob);

    addAndMakeVisible (meter);

    advancedButton.setClickingTogglesState (true);
    advancedButton.onClick = [this] { setAdvanced (advancedButton.getToggleState()); };
    addAndMakeVisible (advancedButton);

    readout.setFont (juce::FontOptions (12.0f));
    readout.setColour (juce::Label::textColourId, colours::dim);
    readout.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (readout);
    addChildComponent (curve);

    setAdvanced (false);
    startTimerHz (30);
}

MatchMasterEditor::~MatchMasterEditor()
{
    setLookAndFeel (nullptr);
}

void MatchMasterEditor::setAdvanced (bool on)
{
    advanced = on;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &curve, &midKnob, &sideKnob, &loudKnob, &midLabel, &sideLabel, &loudLabel, &readout })
        c->setVisible (on);
    setSize (width, on ? advancedHeight : simpleHeight);
}

void MatchMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    auto header = getLocalBounds().removeFromTop (56).reduced (20, 0);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MATCHMASTER", header, juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("reference mastering", header.withTrimmedLeft (156), juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());
    if (advanced)
        g.drawHorizontalLine (simpleHeight, 20.0f, (float) getWidth() - 20.0f);
}

void MatchMasterEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (56).reduced (20, 14);
    advancedButton.setBounds (header.removeFromRight (100));

    auto top = r.removeFromTop (simpleHeight - 56).reduced (20, 18);
    auto left = top.removeFromLeft (270);
    dropZone.setBounds (left.removeFromTop (112));
    left.removeFromTop (12);
    learnButton.setBounds (left.removeFromTop (92));
    left.removeFromTop (10);
    messageLabel.setBounds (left);

    meter.setBounds (top.removeFromRight (54));
    top.removeFromRight (10);
    auto knobArea = top.reduced (10, 0);
    matchLabel.setBounds (knobArea.removeFromBottom (22));
    matchKnob.setBounds (knobArea);

    if (advanced)
    {
        auto adv = r.reduced (20, 16);
        curve.setBounds (adv.removeFromTop (180));
        adv.removeFromTop (14);
        auto row = adv;
        readout.setBounds (row.removeFromRight (190));
        for (const auto& pair : { std::pair<juce::Slider*, juce::Label*> { &midKnob, &midLabel },
                            { &sideKnob, &sideLabel }, { &loudKnob, &loudLabel } })
        {
            auto cell = row.removeFromLeft (120).reduced (6, 0);
            pair.second->setBounds (cell.removeFromTop (16));
            pair.first->setBounds (cell);
        }
    }
}

void MatchMasterEditor::timerCallback()
{
    const auto phase = proc.getPhase();
    const auto snap = proc.getSnapshot();

    dropZone.fileName = snap.referenceName;
    dropZone.busy = phase == Phase::analysingReference;
    dropZone.repaint();

    learnButton.phase = phase;
    learnButton.seconds = proc.getCaptureSeconds();
    learnButton.maxSeconds = proc.getMaxCaptureSeconds();
    learnButton.pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.006f);
    learnButton.repaint();

    messageLabel.setText (snap.message, juce::dontSendNotification);
    messageLabel.setColour (juce::Label::textColourId, snap.messageIsError ? colours::error : colours::dim);

    meter.l = proc.meterL.load();
    meter.r = proc.meterR.load();
    meter.gr = proc.meterGr.exchange (1.0f);
    meter.repaint();

    if (advanced)
    {
        const float match = (float) matchKnob.getValue() * 0.01f;
        curve.snap = snap;
        curve.midAmt = match * (float) midKnob.getValue() * 0.01f;
        curve.sideAmt = match * (float) sideKnob.getValue() * 0.01f;
        curve.repaint();

        const bool has = ! snap.midCurve.empty();
        auto fmt = [] (double db) { return (db >= 0 ? "+" : "") + juce::String (db, 1) + " dB"; };
        readout.setText (has ? "Loudness match  " + fmt (snap.gainDb * match * loudKnob.getValue() * 0.01)
                                   + "\nReference level  " + fmt (snap.refLevelDb * match)
                                   + "\nLatency  " + juce::String (juce::roundToInt (proc.getLatencySamples() * 1000.0 / juce::jmax (1.0, proc.getSampleRate()))) + " ms"
                             : juce::String ("Learn your track to see the match"),
                         juce::dontSendNotification);
    }
}
