#include "PluginEditor.h"

using namespace ui;

// ================================================================== LookAndFeel

LookAndFeel::LookAndFeel()
{
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::bg);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::thumbColourId, colours::accent);
    setColour (juce::Slider::backgroundColourId, colours::line);
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
    g.setGradientFill (juce::ColourGradient (colours::line, c.x, c.y - body,
                                             colours::bg, c.x, c.y + body, false));
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

// ================================================================== DropZone (reference slot)


void DropZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool hot = dragOver || isMouseOver (true);
    g.setColour (selected ? colours::panel.brighter (0.1f) : hot ? colours::panel.brighter (0.05f) : colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (selected ? colours::accent : dragOver ? colours::accent.withAlpha (0.6f) : colours::line);
    g.drawRoundedRectangle (r, 8.0f, selected ? 2.0f : 1.0f);

    auto inner = getLocalBounds().reduced (10, 8);
    g.setColour (selected ? colours::accent : colours::dim);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (letter, inner.removeFromTop (22), juce::Justification::left);

    g.setColour (loaded ? colours::text : colours::dim);
    g.setFont (juce::FontOptions (12.0f, loaded ? juce::Font::bold : juce::Font::plain));
    const auto title = busy ? juce::String ("Analysing...") : loaded ? fileName : juce::String ("Drop or click\nto load a song");
    g.drawFittedText (title, inner.withTrimmedBottom (22), juce::Justification::topLeft, 3);
}

void DropZone::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a reference song", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile() && onFile)
                                  onFile (fc.getResult());
                          });
}

void DropZone::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    if (! loaded || e.mods.isPopupMenu())
        browse(); // empty slot or right-click: load a file
    else if (onSelect)
        onSelect();
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
    return state != MacAndCheeseProcessor::LearnState::analysing;
}

void LearnButton::paint (juce::Graphics& g)
{
    using LS = MacAndCheeseProcessor::LearnState;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool listening = state == LS::listening;

    g.setColour (listening ? colours::rec.withAlpha (0.16f) : colours::panel.brighter (isMouseOver() ? 0.06f : 0.0f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (listening ? (enough ? colours::accent2 : colours::rec) : colours::line.brighter (0.3f));
    g.drawRoundedRectangle (r, 8.0f, enough && listening ? 2.5f : 1.5f);

    auto inner = getLocalBounds().reduced (14, 0);
    auto icon = inner.removeFromLeft (26).toFloat().withSizeKeepingCentre (16, 16);
    g.setColour (listening ? colours::rec.withAlpha (0.55f + 0.45f * pulse) : colours::rec);
    if (listening) g.fillRoundedRectangle (icon.reduced (1), 2.0f);
    else g.fillEllipse (icon);

    juce::String text = listening ? "Stop  " + juce::String ((int) seconds / 60) + ":" + juce::String ((int) seconds % 60).paddedLeft ('0', 2)
                                        + (enough ? "   -   enough" : deviation >= 0.0f ? "   -   keep playing" : "")
                      : state == LS::analysing ? juce::String ("Matching...")
                      : learned ? juce::String ("Re-learn your track") : juce::String ("Learn your track");
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (text, inner, juce::Justification::centredLeft);

    if (listening)
    {
        auto bar = getLocalBounds().toFloat().reduced (10, 0).removeFromBottom (4).translated (0, -4);
        g.setColour (enough ? colours::accent2 : colours::rec);
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
            const float db = juce::jlimit (-maxCorr, maxCorr, 20.0f * std::log10 (juce::jmax (1.0e-4f, std::abs (v))) * amt);
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
    const float w = (area.getWidth() - 12) / 4.0f;
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
    // gain reduction bars, 0-12 dB, drawn from the top: clipper then limiter
    const float grDb = -juce::Decibels::gainToDecibels (gr, -24.0f);
    bar ({ area.getX() + 2 * (w + 4), area.getY(), w, area.getHeight() }, juce::jlimit (0.0f, 1.0f, clipDb / 12.0f), colours::accent, true);
    bar ({ area.getX() + 3 * (w + 4), area.getY(), w, area.getHeight() }, juce::jlimit (0.0f, 1.0f, grDb / 12.0f), colours::accent, true);

    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    const char* names[] = { "L", "R", "CLIP", "LIM" };
    for (int i = 0; i < 4; ++i)
        g.drawText (names[i], labels.withX ((int) (area.getX() + i * (w + 4)) - 4).withWidth ((int) w + 8), juce::Justification::centred);
}

// ================================================================== Editor

MacAndCheeseEditor::MacAndCheeseEditor (MacAndCheeseProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    const char* letters[] = { "A", "B", "C" };
    for (int i = 0; i < MacAndCheeseProcessor::numSlots; ++i)
    {
        auto& z = slotZones[(size_t) i];
        z.letter = letters[i];
        z.onFile = [this, i] (const juce::File& f) { proc.loadReference (i, f); };
        z.onSelect = [this, i] { proc.selectSlot (i); };
        addAndMakeVisible (z);
    }

    learnButton.onClick = [this]
    {
        if (proc.getLearnState() == MacAndCheeseProcessor::LearnState::listening) proc.stopLearning();
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
        s.setTextBoxStyle (big ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false, 80, 18);
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
    setupKnob (smoothKnob, smoothLabel, "SMOOTHING", false);
    setupKnob (corrKnob, corrLabel, "MAX CORR.", false);
    sideKnob.getProperties().set ("side", true);

    auto& vts = proc.apvts;
    matchAtt = std::make_unique<Attachment> (vts, "match", matchKnob);
    midAtt = std::make_unique<Attachment> (vts, "midEq", midKnob);
    sideAtt = std::make_unique<Attachment> (vts, "sideEq", sideKnob);
    loudAtt = std::make_unique<Attachment> (vts, "loudness", loudKnob);
    smoothAtt = std::make_unique<Attachment> (vts, "smoothing", smoothKnob);
    corrAtt = std::make_unique<Attachment> (vts, "maxCorr", corrKnob);

    eqMode.addItemList ({ "EQ: Linear phase", "EQ: Natural (no pre-echo)" }, 1);
    addAndMakeVisible (eqMode);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (vts, "eqMode", eqMode);

    // limiter strip
    for (auto* b : { &limiterButton, &truePeakButton })
    {
        b->setColour (juce::ToggleButton::tickColourId, colours::accent);
        b->setColour (juce::ToggleButton::textColourId, colours::text);
        addAndMakeVisible (b);
    }
    limiterAtt = std::make_unique<ButtonAtt> (vts, "limiter", limiterButton);
    truePeakAtt = std::make_unique<ButtonAtt> (vts, "truePeak", truePeakButton);
    ceilingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    ceilingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    ceilingSlider.setColour (juce::Slider::trackColourId, colours::accent);
    ceilingSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (ceilingSlider);
    ceilingAtt = std::make_unique<Attachment> (vts, "ceiling", ceilingSlider);
    clipSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    clipSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    clipSlider.setColour (juce::Slider::trackColourId, colours::accent);
    clipSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    clipSlider.setTooltip ("dB the clipper shaves off the loudest peaks before the limiter (0 = off)");
    addAndMakeVisible (clipSlider);
    clipAtt = std::make_unique<Attachment> (vts, "clip", clipSlider);
    clipLabel.setText ("Clip", juce::dontSendNotification);
    clipLabel.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (clipLabel);
    ceilingLabel.setText ("Ceiling", juce::dontSendNotification);
    ceilingLabel.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (ceilingLabel);
    limiterReadout.setFont (juce::FontOptions (12.0f));
    limiterReadout.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (limiterReadout);

    addAndMakeVisible (meter);

    levelMatchButton.setClickingTogglesState (true);
    levelMatchButton.setTooltip ("Hear only the EQ match, at your track's own loudness (no loudness push, clipper or limiter)");
    addAndMakeVisible (levelMatchButton);
    levelMatchAtt = std::make_unique<ButtonAtt> (vts, "levelMatch", levelMatchButton);
    advancedButton.setClickingTogglesState (true);
    advancedButton.onClick = [this] { setAdvanced (advancedButton.getToggleState()); };
    addAndMakeVisible (advancedButton);

    readout.setFont (juce::FontOptions (12.0f));
    readout.setColour (juce::Label::textColourId, colours::dim);
    readout.setJustificationType (juce::Justification::topLeft);
    addChildComponent (readout);
    addChildComponent (curve);

    setAdvanced (false);
    startTimerHz (30);
}

MacAndCheeseEditor::~MacAndCheeseEditor()
{
    setLookAndFeel (nullptr);
}

void MacAndCheeseEditor::setAdvanced (bool on)
{
    advanced = on;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &curve, &midKnob, &sideKnob, &loudKnob, &smoothKnob, &corrKnob,
                                                                        &midLabel, &sideLabel, &loudLabel, &smoothLabel, &corrLabel, &readout, &eqMode })
        c->setVisible (on);
    setSize (width, on ? advancedHeight : simpleHeight);
}

void MacAndCheeseEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    auto header = getLocalBounds().removeFromTop (56).reduced (20, 0);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MAC AND CHEESE", header, juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("your mac, their cheese", header.withTrimmedLeft (196), juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());
    g.drawHorizontalLine (simpleHeight - 88, 20.0f, (float) getWidth() - 20.0f);
    if (advanced)
        g.drawHorizontalLine (simpleHeight, 20.0f, (float) getWidth() - 20.0f);
}

void MacAndCheeseEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (56).reduced (20, 14);
    advancedButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (10);
    levelMatchButton.setBounds (header.removeFromRight (120));

    auto main = r.removeFromTop (simpleHeight - 56 - 88).reduced (20, 16);
    auto left = main.removeFromLeft (360);
    auto slotsRow = left.removeFromTop (130);
    const int w = (slotsRow.getWidth() - 16) / 3;
    for (auto& z : slotZones)
    {
        z.setBounds (slotsRow.removeFromLeft (w));
        slotsRow.removeFromLeft (8);
    }
    left.removeFromTop (12);
    learnButton.setBounds (left.removeFromTop (52));
    left.removeFromTop (8);
    messageLabel.setBounds (left);

    meter.setBounds (main.removeFromRight (90));
    main.removeFromRight (10);
    auto knobArea = main.reduced (6, 0);
    matchLabel.setBounds (knobArea.removeFromBottom (22));
    matchKnob.setBounds (knobArea);

    auto strip = r.removeFromTop (88).reduced (20, 12);
    limiterReadout.setBounds (strip.removeFromBottom (24));
    strip.removeFromBottom (4);
    clipLabel.setBounds (strip.removeFromLeft (34));
    clipSlider.setBounds (strip.removeFromLeft (180));
    strip.removeFromLeft (10);
    limiterButton.setBounds (strip.removeFromLeft (84));
    truePeakButton.setBounds (strip.removeFromLeft (96));
    ceilingLabel.setBounds (strip.removeFromLeft (54));
    ceilingSlider.setBounds (strip);

    if (advanced)
    {
        auto adv = r.reduced (20, 14);
        auto top = adv.removeFromTop (24);
        eqMode.setBounds (top.removeFromLeft (220));
        adv.removeFromTop (8);
        curve.setBounds (adv.removeFromTop (100));
        adv.removeFromTop (10);
        readout.setBounds (adv.removeFromRight (150));
        const int cw = adv.getWidth() / 5;
        for (const auto& pair : { std::pair<juce::Slider*, juce::Label*> { &midKnob, &midLabel }, { &sideKnob, &sideLabel },
                                  { &loudKnob, &loudLabel }, { &smoothKnob, &smoothLabel }, { &corrKnob, &corrLabel } })
        {
            auto cell = adv.removeFromLeft (cw).reduced (4, 0);
            pair.second->setBounds (cell.removeFromTop (16));
            pair.first->setBounds (cell);
        }
    }
}

void MacAndCheeseEditor::timerCallback()
{
    const auto snap = proc.getSnapshot();
    const int active = proc.getActiveSlot();
    for (int i = 0; i < MacAndCheeseProcessor::numSlots; ++i)
    {
        auto& z = slotZones[(size_t) i];
        const auto& info = snap.slots[(size_t) i];
        z.fileName = info.name;
        z.loaded = info.loaded;
        z.busy = info.busy;
        z.selected = i == active;
        z.repaint();
    }

    learnButton.state = proc.getLearnState();
    learnButton.learned = proc.hasLearnedTrack();
    learnButton.seconds = proc.getCaptureSeconds();
    learnButton.enough = proc.learnEnough.load();
    learnButton.deviation = proc.learnDeviationDb.load();
    learnButton.maxSeconds = proc.getMaxCaptureSeconds();
    learnButton.pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.006f);
    learnButton.repaint();

    messageLabel.setText (snap.message, juce::dontSendNotification);
    messageLabel.setColour (juce::Label::textColourId, snap.messageIsError ? colours::error : colours::dim);

    meter.l = proc.meterL.load();
    meter.r = proc.meterR.load();
    const float gr = proc.meterGr.exchange (1.0f);
    meter.gr = gr;
    meter.clipDb = proc.clipDbNow.load();
    meter.repaint();

    auto fmt = [] (double db) { return (db >= 0 ? "+" : "") + juce::String (db, 1) + " dB"; };
    const bool limOn = limiterButton.getToggleState();
    truePeakButton.setEnabled (limOn);
    // Clipper -> clean gain -> limiter, one stage per field
    juce::String lim;
    const bool levelMatchOn = levelMatchButton.getToggleState();
    if (levelMatchOn)
        lim = "Level match: EQ only, at your track's loudness (no clipper, gain or limiter)";
    else
    {
        lim = (clipSlider.getValue() > 0.01 ? "Clip " + juce::String (proc.clipDbNow.load(), 1) + " dB (max " + juce::String (proc.maxClipDb.load(), 1) + ")"
                                            : juce::String ("Clip off"))
              + "   >   Gain " + fmt (proc.gainIntoLimiterDb.load()) + "   >   ";
        if (limOn)
            lim += "Limiter " + juce::String (juce::jmax (0.0f, -juce::Decibels::gainToDecibels (gr, -60.0f)), 1)
                   + " dB (max " + juce::String (proc.maxGrDb.load(), 1) + ")";
        else if (proc.suggestionDb.load() > 0.05f)
            lim += "your limiter: push " + fmt (proc.suggestionDb.load()) + " more, ceiling " + juce::String (ceilingSlider.getValue(), 1) + " dBTP";
        else
            lim += "no limiter, kept under the ceiling";
    }
    limiterReadout.setText (lim, juce::dontSendNotification);
    limiterReadout.setColour (juce::Label::textColourId, levelMatchOn ? colours::accent2 : limOn ? colours::dim : colours::accent);

    if (advanced)
    {
        const float match = (float) matchKnob.getValue() * 0.01f;
        curve.snap = snap;
        curve.midAmt = match * (float) midKnob.getValue() * 0.01f;
        curve.sideAmt = match * (float) sideKnob.getValue() * 0.01f;
        curve.maxCorr = (float) corrKnob.getValue();
        curve.repaint();

        const bool has = ! snap.midCurve.empty();
        readout.setText (has ? "Loudness match\n" + fmt (snap.gainDb * match * loudKnob.getValue() * 0.01)
                                   + "\nReference level\n" + fmt (snap.refLevelDb * match)
                                   + "\nLatency\n" + juce::String (juce::roundToInt (proc.getLatencySamples() * 1000.0 / juce::jmax (1.0, proc.getSampleRate()))) + " ms"
                             : juce::String ("Learn your track to see the match"),
                         juce::dontSendNotification);
    }
}
