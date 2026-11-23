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
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::outlineColourId, colours::line);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
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
        g.drawFittedText (juce::String (juce::roundToInt (s.getValue())) + "%", area.withSizeKeepingCentre (body * 1.4f, body * 0.6f).toNearestInt(),
                          juce::Justification::centred, 1, 0.7f); // shrinks rather than "100..." with wider fonts
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
    g.drawText ("Reference " + letter, inner.removeFromTop (22), juce::Justification::left);

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
    if (getLocalBounds().contains (e.getPosition()) && state != MacAndCheeseProcessor::LearnState::analysing && onClick)
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

    g.setColour (colours::text);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (auto [i, v] : { std::pair<int, float> { 2, clipDb }, { 3, grDb } })
        g.drawText (v >= 0.05f ? "-" + juce::String (v, 1) : juce::String ("0"),
                    juce::Rectangle<float> (area.getX() + i * (w + 4) - 4, area.getBottom() - 16, w + 8, 14), juce::Justification::centred);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    const char* names[] = { "L", "R", "CLIP", "LIM" };
    for (int i = 0; i < 4; ++i)
        g.drawText (names[i], labels.withX ((int) (area.getX() + i * (w + 4)) - 4).withWidth ((int) w + 8), juce::Justification::centred);
}

void BandMeter::paint (juce::Graphics& g)
{
    static const char* names[] = { "Bottom", "Mud", "Body", "Bite", "Fizz" };
    auto area = getLocalBounds().toFloat();
    const float w = area.getWidth() / mm::prepBands;
    for (int b = 0; b < mm::prepBands; ++b)
    {
        auto cell = juce::Rectangle<float> (area.getX() + b * w, area.getY(), w, area.getHeight()).reduced (4, 0);
        auto label = cell.removeFromBottom (14);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (cell, 3.0f);
        // bipolar: boosts above the 0 line (up to Max boost), cuts below it (down to -18 dB)
        const float cut = db[(size_t) b], up = boost[(size_t) b];
        auto upZone = cell.removeFromTop (maxBoost > 0.0f ? cell.getHeight() * 0.2f : 0.0f);
        g.setColour (colours::accent);
        g.fillRect (upZone.withTrimmedTop (upZone.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, up / juce::jmax (0.1f, maxBoost)))));
        g.setColour (colours::accent2);
        g.fillRect (cell.withHeight (cell.getHeight() * juce::jlimit (0.0f, 1.0f, cut / 18.0f)));
        g.setColour (colours::dim);
        g.drawHorizontalLine ((int) cell.getY(), cell.getX(), cell.getRight()); // 0 dB
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (up > cut && up >= 0.05f ? "+" + juce::String (up, 1) : cut >= 0.05f ? "-" + juce::String (cut, 1) : juce::String ("0"),
                    cell.removeFromBottom (16), juce::Justification::centred);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (names[b], label, juce::Justification::centred);
    }
}

// ================================================================== Editor

void MacAndCheeseEditor::setupKnob (Knob& k, const juce::String& name, const char* paramId, const juce::String& tip)
{
    auto& sl = k.slider;
    sl.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    sl.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    sl.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sl.setTooltip (tip);
    addAndMakeVisible (sl);
    k.att = std::make_unique<Attachment> (proc.apvts, paramId, sl);
    k.name.setText (name, juce::dontSendNotification);
    k.name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.name.setColour (juce::Label::textColourId, colours::dim);
    k.value.setFont (juce::FontOptions (13.0f));
    for (auto* l : { &k.name, &k.value })
    {
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*l);
    }
}

void MacAndCheeseEditor::setupAuto (AutoKnob& k, const char* autoId, std::function<float()> current)
{
    k.autoButton.setClickingTogglesState (true);
    addAndMakeVisible (k.autoButton);
    k.autoAtt = std::make_unique<ButtonAtt> (proc.apvts, autoId, k.autoButton);
    // switching Auto off leaves the knob where Auto had it: no jump
    k.autoButton.onClick = [&k, current] { if (! k.autoButton.getToggleState()) k.slider.setValue (current(), juce::sendNotificationSync); };
}

void MacAndCheeseEditor::place (Knob& k, juce::Rectangle<int> r)
{
    k.name.setBounds (r.removeFromTop (16));
    k.value.setBounds (r.removeFromBottom (18));
    k.slider.setBounds (r);
}

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

    auto& vts = proc.apvts;
    levelMatchButton.setClickingTogglesState (true);
    levelMatchButton.setTooltip ("Compare fairly: the match at your track's own loudness (no clip, gain or limiter)");
    addAndMakeVisible (levelMatchButton);
    levelMatchAtt = std::make_unique<ButtonAtt> (vts, "levelMatch", levelMatchButton);
    detailsButton.setClickingTogglesState (true);
    detailsButton.onClick = [this] { setDetails (detailsButton.getToggleState()); };
    addAndMakeVisible (detailsButton);

    // 1 FIX THE MIX
    setupKnob (unclump, "UNCLUMP", "unclump", "Evens out build-ups (mud, harsh hits) compared with the reference");
    setupKnob (monoBass, "MONO BASS", "monoBase", "Low end mono below this frequency (left = off)");
    addAndMakeVisible (bandMeter);

    // 2 MATCH
    matchKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    matchKnob.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    matchKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    matchKnob.setTooltip ("How much of the reference: 0 % = your mix, 100 % = fully matched");
    addAndMakeVisible (matchKnob);
    matchAtt = std::make_unique<Attachment> (vts, "match", matchKnob);
    matchLabel.setText ("MATCH", juce::dontSendNotification);
    matchLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    matchLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (matchLabel);

    // 3 LOUDNESS: clipper -> gain -> limiter
    setupKnob (clip, "CLIP", "clip", "dB the clipper shaves off the loudest peaks before the limiter");
    setupKnob (gain, "GAIN", "gain", "Gain into the limiter. Auto = the reference's loudness");
    setupKnob (ceiling, "LIMITER", "ceiling", "Limiter ceiling. Auto = -1 dBTP");
    setupAuto (clip, "clipAuto", [this] { return proc.effClipDb.load(); });
    setupAuto (gain, "gainAuto", [this] { return proc.gainIntoLimiterDb.load(); });
    setupAuto (ceiling, "ceilingAuto", [this] { return proc.effCeilingDb.load(); });
    autoAllButton.onClick = [this]
    {
        for (auto* id : { "clipAuto", "gainAuto", "ceilingAuto" })
            proc.apvts.getParameter (id)->setValueNotifyingHost (1.0f);
    };
    addAndMakeVisible (autoAllButton);
    for (auto* b : { &limiterButton, &truePeakButton })
    {
        b->setColour (juce::ToggleButton::tickColourId, colours::accent);
        addAndMakeVisible (b);
    }
    limiterAtt = std::make_unique<ButtonAtt> (vts, "limiter", limiterButton);
    truePeakAtt = std::make_unique<ButtonAtt> (vts, "truePeak", truePeakButton);
    addAndMakeVisible (meter);
    readout.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (readout);

    // Details
    eqMode.addItemList ({ "EQ: Linear phase", "EQ: Natural (no pre-echo)" }, 1);
    addChildComponent (eqMode);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (vts, "eqMode", eqMode);
    addChildComponent (curve);
    setupKnob (midEq, "MID EQ", "midEq", "How much of the matching EQ on the centre");
    setupKnob (sideEq, "SIDE EQ", "sideEq", "How much of the matching EQ on the sides");
    setupKnob (smoothing, "EQ SMOOTHING", "smoothing", "Detailed curve (0 %) to a broad tilt (100 %)");
    setupKnob (maxEq, "MAX EQ", "maxCorr", "Largest boost or cut the matching EQ may use");
    sideEq.slider.getProperties().set ("side", true);
    const char* bandIds[] = { "bandBottom", "bandMud", "bandBody", "bandBite", "bandFizz" };
    const char* bandNames[] = { "BOTTOM", "MUD", "BODY", "BITE", "FIZZ" };
    for (int b = 0; b < mm::prepBands; ++b)
        setupKnob (bands[(size_t) b], bandNames[b], bandIds[b], "50 % = match the reference. More = stronger (up to 2x, -18 dB), less = gentler");
    maxBoostLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    maxBoostLabel.setColour (juce::Label::textColourId, colours::dim);
    maxBoost.setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.6f));
    maxBoost.setColour (juce::Slider::backgroundColourId, colours::panel);
    maxBoost.setColour (juce::Slider::textBoxTextColourId, colours::text);
    maxBoost.setTooltip ("Unclump cuts only at 0 dB. Above: it may also lift a band that dips below the reference, up to this much");
    addChildComponent (maxBoostLabel);
    addChildComponent (maxBoost);
    maxBoostAtt = std::make_unique<Attachment> (vts, "maxBoost", maxBoost);
    info.setFont (juce::FontOptions (12.0f));
    info.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (info);

    // move every control into the scaled content layer
    for (auto* c : juce::Array<juce::Component*> (getChildren().begin(), getChildren().size()))
    {
        removeChildComponent (c);
        content.addChildComponent (c); // keeps each child's own visibility
    }
    content.setInterceptsMouseClicks (false, true);
    content.setTransform (juce::AffineTransform::scale (uiScale));
    addAndMakeVisible (content);

    setDetails (false);
    startTimerHz (30);
}

MacAndCheeseEditor::~MacAndCheeseEditor()
{
    setLookAndFeel (nullptr);
}

void MacAndCheeseEditor::setDetails (bool on)
{
    details = on;
    std::vector<juce::Component*> panel { &eqMode, &curve, &maxBoostLabel, &maxBoost, &info };
    for (auto* k : { &midEq, &sideEq, &smoothing, &maxEq })
        panel.insert (panel.end(), { &k->slider, &k->name, &k->value });
    for (auto& k : bands)
        panel.insert (panel.end(), { &k.slider, &k.name, &k.value });
    for (auto* c : panel)
        c->setVisible (on);
    const int w = on ? mainWidth + detailsWidth : mainWidth;
    content.setBounds (0, 0, w, height);
    setSize (juce::roundToInt (w * uiScale), juce::roundToInt (height * uiScale));
}

void MacAndCheeseEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    g.addTransform (juce::AffineTransform::scale (uiScale)); // same drawing as the full-size layout
    auto header = getLocalBounds().removeFromTop (56).reduced (20, 0);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MAC AND CHEESE", header, juce::Justification::centredLeft);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("your mac, their cheese", header.withTrimmedLeft (196), juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.drawHorizontalLine (56, 0.0f, (float) content.getWidth());
    g.drawHorizontalLine (topBottom, 20.0f, mainWidth - 20.0f);
    g.drawVerticalLine (290, topBottom + 10.0f, height - 50.0f);
    g.drawVerticalLine (540, topBottom + 10.0f, height - 50.0f);
    g.setColour (colours::accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("1   FIX THE MIX", 20, topBottom + 10, 250, 16, juce::Justification::left);
    g.drawText ("2   MATCH", 310, topBottom + 10, 220, 16, juce::Justification::left);
    g.drawText ("3   LOUDNESS", 560, topBottom + 10, 320, 16, juce::Justification::left);
    if (details)
    {
        g.setColour (colours::line);
        g.drawVerticalLine (mainWidth, 56.0f, (float) content.getHeight());
        g.setColour (colours::accent);
        g.drawText ("DETAILS", mainWidth + 20, 66, 200, 16, juce::Justification::left);
        g.drawText ("1   Unclump per band", mainWidth + 20, 382, 300, 16, juce::Justification::left);
    }
}

void MacAndCheeseEditor::resized()
{
    auto header = juce::Rectangle<int> (0, 0, mainWidth, 56).reduced (20, 14);
    detailsButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (10);
    levelMatchButton.setBounds (header.removeFromRight (120));

    // references + learn
    auto top = juce::Rectangle<int> (20, 72, mainWidth - 40, topBottom - 72 - 14);
    for (auto& z : slotZones)
    {
        z.setBounds (top.removeFromLeft (150));
        top.removeFromLeft (10);
    }
    top.removeFromLeft (10);
    learnButton.setBounds (top.removeFromTop (50));
    top.removeFromTop (8);
    messageLabel.setBounds (top);

    const int colTop = topBottom + 34, colBottom = height - 52;
    // 1 FIX THE MIX
    auto fix = juce::Rectangle<int> (20, colTop, 250, colBottom - colTop);
    auto knobs = fix.removeFromTop (130);
    place (unclump, knobs.removeFromLeft (125));
    place (monoBass, knobs);
    fix.removeFromTop (10);
    bandMeter.setBounds (fix);
    // 2 MATCH
    auto m = juce::Rectangle<int> (310, colTop, 210, colBottom - colTop);
    matchLabel.setBounds (m.removeFromBottom (22));
    matchKnob.setBounds (m.withSizeKeepingCentre (200, 200));
    // 3 LOUDNESS
    auto loud = juce::Rectangle<int> (560, colTop, 320, colBottom - colTop);
    meter.setBounds (loud.removeFromRight (80));
    loud.removeFromRight (10);
    auto row = loud.removeFromTop (150);
    const int kw = row.getWidth() / 3;
    for (auto* k : { &clip, &gain, &ceiling })
    {
        auto c = row.removeFromLeft (kw).reduced (3, 0);
        k->autoButton.setBounds (c.removeFromBottom (22).reduced (8, 0));
        c.removeFromBottom (2);
        place (*k, c);
    }
    loud.removeFromTop (10);
    autoAllButton.setBounds (loud.removeFromTop (26).withWidth (100));
    loud.removeFromTop (8);
    limiterButton.setBounds (loud.removeFromTop (24).withWidth (120));
    truePeakButton.setBounds (loud.removeFromTop (24).withWidth (120));
    readout.setBounds (20, height - 40, mainWidth - 40, 24);

    if (details)
    {
        auto d = juce::Rectangle<int> (mainWidth + 20, 92, detailsWidth - 40, height - 110);
        eqMode.setBounds (d.removeFromTop (26));
        d.removeFromTop (8);
        curve.setBounds (d.removeFromTop (130));
        d.removeFromTop (8);
        auto r1 = d.removeFromTop (110);
        const int w4 = r1.getWidth() / 4;
        for (auto* k : { &midEq, &sideEq, &smoothing, &maxEq })
            place (*k, r1.removeFromLeft (w4));
        d.removeFromTop (34); // "1 Unclump per band" heading
        auto r2 = d.removeFromTop (90);
        const int w5 = r2.getWidth() / mm::prepBands;
        for (auto& k : bands)
            place (k, r2.removeFromLeft (w5));
        d.removeFromTop (6);
        auto mb = d.removeFromTop (24);
        maxBoostLabel.setBounds (mb.removeFromLeft (90));
        maxBoost.setBounds (mb);
        info.setBounds (d.removeFromBottom (20));
    }
}

void MacAndCheeseEditor::timerCallback()
{
    const auto snap = proc.getSnapshot();
    const int active = proc.getActiveSlot();
    for (int i = 0; i < MacAndCheeseProcessor::numSlots; ++i)
    {
        auto& z = slotZones[(size_t) i];
        const auto& s = snap.slots[(size_t) i];
        z.fileName = s.name;
        z.loaded = s.loaded;
        z.busy = s.busy;
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

    auto fmt = [] (double db) { return (db >= 0 ? "+" : "") + juce::String (db, 1) + " dB"; };
    for (auto* k : { &unclump, &monoBass, &midEq, &sideEq, &smoothing, &maxEq })
        k->value.setText (k->slider.getTextFromValue (k->slider.getValue()), juce::dontSendNotification);
    for (auto& k : bands)
        k.value.setText (k.slider.getTextFromValue (k.slider.getValue()), juce::dontSendNotification);
    const std::pair<AutoKnob*, float> lk[] = { { &clip, proc.effClipDb.load() }, { &gain, proc.gainIntoLimiterDb.load() },
                                               { &ceiling, proc.effCeilingDb.load() } };
    for (auto [k, now] : lk)
    {
        const bool isAuto = k->autoButton.getToggleState();
        k->slider.setEnabled (! isAuto);
        k->value.setText ((isAuto ? "auto " : "") + fmt (now), juce::dontSendNotification);
        k->value.setColour (juce::Label::textColourId, isAuto ? colours::accent : colours::text);
    }

    meter.l = proc.meterL.load();
    meter.r = proc.meterR.load();
    const float gr = proc.meterGr.exchange (1.0f);
    meter.gr = gr;
    meter.clipDb = proc.clipDbNow.load();
    meter.repaint();
    for (int b = 0; b < mm::prepBands; ++b)
    {
        bandMeter.db[(size_t) b] = proc.bandReduction[(size_t) b].load();
        bandMeter.boost[(size_t) b] = proc.bandBoost[(size_t) b].load();
    }
    bandMeter.maxBoost = (float) maxBoost.getValue();
    bandMeter.repaint();

    const bool limOn = limiterButton.getToggleState(), lm = levelMatchButton.getToggleState();
    truePeakButton.setEnabled (limOn);
    juce::String line;
    if (lm)
        line = "Level match: the match at your track's own loudness (no clip, gain or limiter)";
    else
    {
        line = "Clip " + juce::String (proc.clipDbNow.load(), 1) + " dB (max " + juce::String (proc.maxClipDb.load(), 1) + ")   >   Gain "
               + fmt (proc.gainIntoLimiterDb.load()) + "   >   ";
        if (limOn)
            line += "Limiter " + juce::String (juce::jmax (0.0f, -juce::Decibels::gainToDecibels (gr, -60.0f)), 1) + " dB (max " + juce::String (proc.maxGrDb.load(), 1) + ")";
        else if (proc.suggestionDb.load() > 0.05f)
            line += "no limiter: push " + fmt (proc.suggestionDb.load()) + " more into yours to match the reference";
        else
            line += "no limiter, kept under the ceiling";
    }
    readout.setText (line, juce::dontSendNotification);
    readout.setColour (juce::Label::textColourId, lm ? colours::accent2 : colours::dim);

    if (details)
    {
        const float match = (float) matchKnob.getValue() * 0.01f;
        curve.snap = snap;
        curve.midAmt = match * (float) midEq.slider.getValue() * 0.01f;
        curve.sideAmt = match * (float) sideEq.slider.getValue() * 0.01f;
        curve.maxCorr = (float) maxEq.slider.getValue();
        curve.repaint();
        info.setText (snap.midCurve.empty() ? juce::String ("Learn your track to see the match")
                                            : "Reference level " + fmt (snap.refLevelDb * match) + "   Latency "
                                                  + juce::String (juce::roundToInt (proc.getLatencySamples() * 1000.0 / juce::jmax (1.0, proc.getSampleRate()))) + " ms",
                      juce::dontSendNotification);
    }
}
