#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

Image rtui::loadImage (const String& n)
{
    int size = 0;
    auto* d = BinaryData::getNamedResource ((n.replaceCharacter ('.', '_') + "_png").toRawUTF8(), size);
    if (d == nullptr) { jassertfalse; return {}; }
    return ImageCache::getFromMemory (d, size);
}

// ------------------------------------------------------------------ Knob
rtui::Knob::Knob (AudioProcessorValueTreeState& a, const String& id, int st) : steps (st)
{
    setSliderStyle (Slider::RotaryVerticalDrag);
    setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (degreesToRadians (225.f), degreesToRadians (495.f), true);
    setVelocityBasedMode (false);
    setMouseDragSensitivity (180);
    setDoubleClickReturnValue (true, a.getParameter (id)->convertFrom0to1 (a.getParameter (id)->getDefaultValue()));
}

void rtui::Knob::paint (Graphics& g)
{
    static Image body = loadImage ("knob");
    auto b = getLocalBounds().toFloat();
    g.drawImage (body, b, RectanglePlacement::stretchToFit);
    const auto c = b.getCentre();
    const float pos = (float) valueToProportionOfLength (getValue());
    const float a = degreesToRadians (-135.f + 270.f * pos);
    const float r = (float) layout::knobD * 0.5f;
    // indicator line
    Path p; p.addRoundedRectangle (-1.3f, -r + 4.5f, 2.6f, r * 0.50f, 1.2f);
    g.setColour (Colour (0xffe9d9a8));
    g.fillPath (p, AffineTransform::rotation (a).translated (c.x, c.y));
    // active arc
    Path arc; arc.addCentredArc (c.x, c.y, r + 11.5f, r + 11.5f, 0.f, degreesToRadians (-135.f), a, true);
    g.setColour (Colour (0xffc9741f).withAlpha (isMouseOverOrDragging() ? 1.f : 0.85f));
    g.strokePath (arc, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

// ------------------------------------------------------------------ KeyButton
rtui::KeyButton::KeyButton (AudioProcessorValueTreeState& a, const String& id, const String& asset,
                            bool isToggle, RangedAudioParameter* latch)
    : param (a.getParameter (id)), latchParam (latch), toggle (isToggle)
{
    up = loadImage (asset + "_up"); down = loadImage (asset + "_down");
    setMouseCursor (MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

void rtui::KeyButton::set (bool on)
{
    param->setValueNotifyingHost (on ? 1.f : 0.f);
}

void rtui::KeyButton::forceOff()
{
    if (isOn()) { param->beginChangeGesture(); set (false); param->endChangeGesture(); }
}

void rtui::KeyButton::mouseDown (const MouseEvent&)
{
    const bool latched = latchParam != nullptr && latchParam->getValue() > 0.5f;
    held = true;
    param->beginChangeGesture();
    if (toggle || latched) { set (! isOn()); if (isOn() && latched && onEngaged) onEngaged (this); }
    else { set (true); }
    repaint();
}

void rtui::KeyButton::mouseUp (const MouseEvent&)
{
    const bool latched = latchParam != nullptr && latchParam->getValue() > 0.5f;
    if (! toggle && ! latched) set (false);
    held = false;
    param->endChangeGesture();
    repaint();
}

void rtui::KeyButton::paint (Graphics& g)
{
    const bool on = isOn();
    auto b = getLocalBounds().toFloat();
    g.drawImage (on ? down : up, b, RectanglePlacement::stretchToFit);
    // lamp
    const float cx = b.getCentreX(), cy = 13.f;
    if (on)
    {
        ColourGradient glow (Colour (0xffffb050).withAlpha (0.85f), cx, cy, Colours::transparentBlack, cx, cy + 20.f, true);
        g.setGradientFill (glow); g.fillEllipse (cx - 20.f, cy - 20.f, 40.f, 40.f);
        g.setColour (Colour (0xffffe1a8)); g.fillEllipse (cx - 3.2f, cy - 3.2f, 6.4f, 6.4f);
    }
    else
    {
        g.setColour (Colour (0xff3a2a22)); g.fillEllipse (cx - 3.2f, cy - 3.2f, 6.4f, 6.4f);
    }
}

// ------------------------------------------------------------------ Editor
static Font findSignatureFont()
{
    const StringArray wanted { "Snell Roundhand", "Zapfino", "Apple Chancery", "Brush Script MT", "Savoye LET",
                               "Bradley Hand", "Segoe Script", "Lucida Handwriting", "URW Chancery L", "Z003" };
    const auto all = Font::findAllTypefaceNames();
    for (auto& w : wanted)
        if (all.contains (w))
            return Font (FontOptions (w, 30.f, Font::bold));
    return Font (FontOptions (all.contains ("Georgia") ? "Georgia" : Font::getDefaultSerifFontName(), 28.f, Font::bold | Font::italic));
}

ReelTransitEditor::ReelTransitEditor (ReelTransitProcessor& p)
    : AudioProcessorEditor (&p), proc (p), sigFont (findSignatureFont())
{
    faceplate = rtui::loadImage ("faceplate"); knobImg = rtui::loadImage ("knob");
    reelImg = rtui::loadImage ("reel"); vuIn = rtui::loadImage ("vu_in"); vuOut = rtui::loadImage ("vu_out");
    vuGlass = rtui::loadImage ("vu_glass");
    setSize (layout::W, layout::H);

    auto* latch = proc.apvts.getParameter ("latch");
    for (int i = 0; i < layout::numKeys; ++i)
    {
        const String id (layout::keys[i].id);
        const bool isToggle = (id == "spinUp" || id == "latch");
        auto* k = keys.add (new rtui::KeyButton (proc.apvts, id, layout::keys[i].asset, isToggle, isToggle ? nullptr : latch));
        k->setBounds (layout::keyX0 + i * layout::keyDX, layout::keyY, layout::keyW, layout::keyH);
        addAndMakeVisible (k);
    }
    // in latch mode a newly latched transition releases the other transition keys
    for (int i = 0; i < 5; ++i)
        keys[i]->onEngaged = [this] (rtui::KeyButton* me) { for (int j = 0; j < 5; ++j) if (keys[j] != me) keys[j]->forceOff(); };

    static const int stepsFor[] = { 0, 0, 6, 0, 0, 0, 0, 6, 0, 0, 0, 0 };
    for (int i = 0; i < layout::numKnobs; ++i)
    {
        auto* k = knobs.add (new rtui::Knob (proc.apvts, layout::knobIds[i], stepsFor[i]));
        const int cx = (int) std::lround (layout::knobX0 + i * layout::knobDX);
        const int pad = 8;
        k->setBounds (cx - layout::knobD / 2 - pad, layout::knobY - layout::knobD / 2 - pad, layout::knobD + 2 * pad, layout::knobD + 2 * pad);
        addAndMakeVisible (k);
        attach.push_back (std::make_unique<SliderParameterAttachment> (*proc.apvts.getParameter (layout::knobIds[i]), *k));
        k->setPopupDisplayEnabled (true, true, this);
    }
    startTimerHz (40);
}

ReelTransitEditor::~ReelTransitEditor() { stopTimer(); }

void ReelTransitEditor::timerCallback()
{
    const float sp = proc.headSpeed.load();
    smoothSpeed += (sp - smoothSpeed) * 0.35f;
    const float v = smoothSpeed * 0.22f;       // radians per frame at 1x
    angL -= v; angR -= v;
    // levels -> VU ballistics
    auto toDb = [] (float lin) { return 20.f * std::log10 (std::max (lin, 1.0e-5f)) + 12.f; };
    auto ball = [] (float& shown, float target) { shown += (target - shown) * (target > shown ? 0.35f : 0.12f); };
    ball (shownIn,  toDb (proc.inLevel.load()));
    ball (shownOut, toDb (proc.outLevel.load()));
    tapeMoved += std::abs (smoothSpeed) * 0.0004f;
    repaint (layout::reelWin[0], layout::reelWin[1], layout::reelWin[2], layout::reelWin[3]);
    repaint (layout::vuIn[0], layout::vuIn[1], layout::vuIn[2], layout::vuIn[3]);
    repaint (layout::vuOut[0], layout::vuOut[1], layout::vuOut[2], layout::vuOut[3]);
}

void ReelTransitEditor::paint (Graphics& g)
{
    g.drawImage (faceplate, getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);

    // knob value read-outs
    g.setFont (Font (FontOptions (9.f)));
    for (int i = 0; i < layout::numKnobs; ++i)
    {
        const int cx = (int) std::lround (layout::knobX0 + i * layout::knobDX);
        g.setColour (Colour (0xff7a5a2c));
        g.drawText (knobs[i]->getTextFromValue (knobs[i]->getValue()), cx - 34, layout::knobY + 54, 68, 12, Justification::centred);
    }

    // reels
    const float played = jlimit (0.f, 1.f, 0.5f + 0.5f * std::sin (tapeMoved));
    const float rl = 34.f + 30.f * (1.f - played), rr = 34.f + 30.f * played;
    drawReel (g, layout::reelL[0], layout::reelL[1], rl, angL);
    drawReel (g, layout::reelR[0], layout::reelR[1], rr, angR);

    // tape path between reels
    g.setColour (Colour (0xff1c140f));
    g.drawLine ((float) layout::reelL[0] + rl, (float) layout::reelL[1] + 6.f, (float) layout::reelR[0] - rr, (float) layout::reelR[1] + 6.f, 2.2f);

    drawVU (g, layout::vuIn, shownIn, shownIn);
    drawVU (g, layout::vuOut, shownOut, shownOut);

    // signature on brass plate
    auto pr = Rectangle<int> (layout::plate[0], layout::plate[1], layout::plate[2], layout::plate[3]).reduced (20, 2).toFloat();
    g.setFont (sigFont.withHeight (pr.getHeight() * 0.78f));
    g.setColour (Colour (0x66ffe9b0)); g.drawText ("Tal Mendel", pr.translated (0.f, 1.f), Justification::centred);
    g.setColour (Colour (0xff3b2a0f)); g.drawText ("Tal Mendel", pr, Justification::centred);
}

void ReelTransitEditor::drawReel (Graphics& g, int cx, int cy, float packR, float angle)
{
    // tape pack with a soft sheen
    ColourGradient pack (Colour (0xff3a2619), (float) cx - packR * 0.4f, (float) cy - packR * 0.4f,
                         Colour (0xff130c08), (float) cx + packR * 0.8f, (float) cy + packR * 0.8f, true);
    g.setGradientFill (pack);
    g.fillEllipse (cx - packR, cy - packR, packR * 2.f, packR * 2.f);
    g.setColour (Colour (0x22ffffff));
    g.drawEllipse (cx - packR + 1.f, cy - packR + 1.f, packR * 2.f - 2.f, packR * 2.f - 2.f, 0.8f);
    // reel flange
    const float rad = (float) layout::reelRadius;
    g.drawImageTransformed (reelImg, AffineTransform::scale (rad * 2.f / reelImg.getWidth())
                                         .translated (-rad, -rad).rotated (angle).translated ((float) cx, (float) cy));
}

void ReelTransitEditor::drawVU (Graphics& g, const int rect[4], float level, float&)
{
    const bool isIn = (rect == layout::vuIn);
    Rectangle<int> r (rect[0], rect[1], rect[2], rect[3]);
    g.drawImage (isIn ? vuIn : vuOut, r.toFloat(), RectanglePlacement::stretchToFit);
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r);
    const float db = jlimit (layout::vuMinDb - 2.f, layout::vuMaxDb + 2.f, level);
    const float a = degreesToRadians ((db - (layout::vuMinDb + layout::vuMaxDb) * 0.5f) / (layout::vuMaxDb - layout::vuMinDb) * 2.f * layout::vuAngle);
    const float px = (float) r.getX() + layout::vuPivotX, py = (float) r.getY() + layout::vuPivotY;
    const float len = layout::vuNeedleLen;
    const float tx = px + len * std::sin (a), ty = py - len * std::cos (a);
    g.setColour (Colour (0x33000000)); g.drawLine (px + 2.f, py + 2.f, tx + 2.f, ty + 2.f, 1.6f);
    g.setColour (Colour (0xff1b1410)); g.drawLine (px, py, tx, ty, 1.3f);
    g.drawImage (vuGlass, r.toFloat(), RectanglePlacement::stretchToFit);
}

void ReelTransitEditor::paintOverChildren (Graphics&) {}
