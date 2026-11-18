#include "PluginEditor.h"

namespace
{
    // HatsOff light scheme
    const juce::Colour cBg      { 0xFF141414 };
    const juce::Colour cPanel   { 0xFFE4E4E1 };
    const juce::Colour cInset   { 0xFFC9C9C5 };
    const juce::Colour cInset2  { 0xFFBBBBB7 };
    const juce::Colour cLabel   { 0xFF5C5C59 };
    const juce::Colour cText    { 0xFF1C1C1A };
    const juce::Colour cTrack   { 0xFFB4B4B0 };
    const juce::Colour cOrange  { 0xFFEF5C25 };
    const juce::Colour cBlue    { 0xFF3960B6 };
    const juce::Colour cTeal    { 0xFF2D908B };
    const juce::Colour cPurple  { 0xFF8A4AB8 };
    const juce::Colour cGreen   { 0xFF23955F };

    juce::Font mono (float size, bool bold = false)
    {
       #if JUCE_WINDOWS
        const juce::String name ("Consolas");
       #else
        const juce::String name (juce::Font::getDefaultMonospacedFontName());
       #endif
        return juce::Font (juce::FontOptions (name, size, bold ? juce::Font::bold : juce::Font::plain));
    }

    const juce::String dotSep (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    constexpr float vuMin = -20.0f, vuMax = 3.0f;
}

//==============================================================================
TubeLiftEditor::TubeLiftEditor (TubeLiftProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    using R = juce::Rectangle<float>;
    pHeader  = R (10, 10, 620, 46);
    bSize    = R (300, 23, 96, 20);

    pDrive   = R (10, 64, 300, 150);
    driveBox = R (24, 90, 272, 76);

    pMeter   = R (318, 64, 312, 150);
    meterArea = R (332, 90, 284, 112);

    pFilters = R (10, 222, 400, 128);
    pOut     = R (418, 222, 212, 128);
    levelBar = R (432, 330, 184, 8);

    sliders = {
        { "drive",   "DRIVE",    R (24, 176, 272, 26),  60, 52, cOrange, false },
        { "lowcut",  "LOW CUT",  R (24, 248, 372, 26),  92, 84, cTeal,   false },
        { "tone",    "TONE",     R (24, 276, 372, 26),  92, 84, cTeal,   true  },
        { "highcut", "HIGH CUT", R (24, 304, 372, 26),  92, 84, cTeal,   false },
        { "output",  "OUTPUT",   R (432, 248, 184, 26), 62, 70, cPurple, false },
        { "mix",     "MIX",      R (432, 276, 184, 26), 62, 70, cPurple, false },
    };

    // Read the saved size BEFORE setting resize limits: setResizeLimits() triggers resized()
    // at the minimum size, which used to overwrite the saved scale with 75%.
    const float s = juce::jlimit (0.75f, 2.5f, proc.uiScale.load());
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 5 / 2, H * 5 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / (double) H);
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    sizeReady = true;
    startTimerHz (30);
}

void TubeLiftEditor::resized() { if (sizeReady) proc.uiScale = uiScale(); }

void TubeLiftEditor::timerCallback()
{
    // VU-ish ballistics for the ATTITUDE needle
    const float pk = proc.engine.attitudePeak.exchange (0.0f);
    const float db = pk > 1.0e-5f ? juce::Decibels::gainToDecibels (pk) : -60.0f;
    needleDb += (db > needleDb ? 0.45f : 0.12f) * (db - needleDb);

    const float lv = proc.outLevel.exchange (0.0f);
    const float ldb = lv > 1.0e-6f ? juce::Decibels::gainToDecibels (lv) : -120.0f;
    const float ln = juce::jlimit (0.0f, 1.0f, (ldb + 60.0f) / 60.0f);
    levelDisp = ln > levelDisp ? ln : levelDisp * 0.9f;
    repaint();
}

//==============================================================================
float TubeLiftEditor::vuPos (float db)
{
    // classic VU spacing: linear in voltage
    const float lo = juce::Decibels::decibelsToGain (vuMin), hi = juce::Decibels::decibelsToGain (vuMax);
    const float v = juce::Decibels::decibelsToGain (juce::jlimit (vuMin - 6.0f, vuMax + 1.0f, db));
    return juce::jlimit (-0.03f, 1.03f, (v - lo) / (hi - lo));
}

juce::Rectangle<float> TubeLiftEditor::trackRect (const SliderDef& s) const
{
    return { s.row.getX() + s.labelW, s.row.getY(), s.row.getWidth() - s.labelW - s.valueW, s.row.getHeight() };
}

juce::String TubeLiftEditor::formatValue (int i) const
{
    const juce::String id (sliders[(size_t) i].id);
    const float v = value (sliders[(size_t) i].id);
    if (id == "drive")   return juce::String (v, 1);
    if (id == "lowcut")  return v <= 20.5f ? juce::String ("OFF") : juce::String (juce::roundToInt (v)) + " Hz";
    if (id == "highcut") return v >= 19900.0f ? juce::String ("OFF") : juce::String (v / 1000.0f, 1) + " kHz";
    if (id == "tone")
    {
        const int t = juce::roundToInt (v * 100.0f);
        if (t == 0) return "FLAT";
        return (t < 0 ? "DARK " : "BRIGHT ") + juce::String (std::abs (t));
    }
    if (id == "output")  return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
    return juce::String (juce::roundToInt (v)) + " %";
}

void TubeLiftEditor::setNorm (const char* id, float n)
{
    if (auto* p = param (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n));
}

TubeLiftEditor::Target TubeLiftEditor::findTarget (juce::Point<float> pt) const
{
    if (bSize.contains (pt)) return { SizeBtn, -1 };
    if (driveBox.contains (pt)) return { DriveBox, -1 };
    for (int i = 0; i < (int) sliders.size(); ++i)
        if (trackRect (sliders[(size_t) i]).expanded (6, 0).contains (pt)) return { Slider, i };
    return {};
}

//==============================================================================
void TubeLiftEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto t = findTarget (pos (e));
    if (! (t == hover)) { hover = t; repaint(); }
}

void TubeLiftEditor::mouseExit (const juce::MouseEvent&) { hover = {}; repaint(); }

void TubeLiftEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto t = findTarget (pos (e));
    active = {};
    dragStartX = pos (e).x;
    dragStartY = pos (e).y;
    dragFine = e.mods.isShiftDown();

    if (t.kind == SizeBtn) { openSizeMenu(); return; }
    if (t.kind == DriveBox || t.kind == Slider)
    {
        active = t;
        auto* p = param (t.kind == DriveBox ? "drive" : sliders[(size_t) t.index].id);
        dragStartVal = p->getValue();
        p->beginChangeGesture();
    }
    repaint();
}

void TubeLiftEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (active.kind == None) return;
    const char* id = active.kind == DriveBox ? "drive" : sliders[(size_t) active.index].id;
    const bool fine = e.mods.isShiftDown();
    if (fine != dragFine)   // re-anchor so switching to fine mode doesn't jump
    {
        dragFine = fine;
        dragStartX = pos (e).x;
        dragStartY = pos (e).y;
        dragStartVal = param (id)->getValue();
    }
    const auto pt = pos (e);
    if (active.kind == Slider)
        setNorm (id, dragStartVal + (pt.x - dragStartX) / trackRect (sliders[(size_t) active.index]).getWidth() * (fine ? 0.15f : 1.0f));
    else
        setNorm (id, dragStartVal + (dragStartY - pt.y) / (fine ? 1200.0f : 180.0f));
    repaint();
}

void TubeLiftEditor::mouseUp (const juce::MouseEvent& e)
{
    if (active.kind == Slider)        param (sliders[(size_t) active.index].id)->endChangeGesture();
    else if (active.kind == DriveBox) param ("drive")->endChangeGesture();
    active = {};
    hover = findTarget (pos (e));
    repaint();
}

void TubeLiftEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = findTarget (pos (e));
    if (t.kind != Slider && t.kind != DriveBox) return;
    auto* p = param (t.kind == DriveBox ? "drive" : sliders[(size_t) t.index].id);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
    repaint();
}

void TubeLiftEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-4f) return;
    const float dir = w.deltaY > 0 ? 1.0f : -1.0f;
    const auto t = findTarget (pos (e));
    if (t.kind != Slider && t.kind != DriveBox) return;
    auto* p = param (t.kind == DriveBox ? "drive" : sliders[(size_t) t.index].id);
    p->beginChangeGesture();
    p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + dir * (e.mods.isShiftDown() ? 0.002f : 0.01f)));
    p->endChangeGesture();
    repaint();
}

void TubeLiftEditor::openSizeMenu()
{
    juce::PopupMenu m;
    const int pcts[] = { 100, 125, 150, 175, 200 };
    const int cur = juce::roundToInt (uiScale() * 100.0f);
    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, juce::String (pcts[i]) + "%", true, std::abs (cur - pcts[i]) <= 1);
    juce::Component::SafePointer<TubeLiftEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                               .withTargetScreenArea (localAreaToGlobal ((bSize * uiScale()).toNearestInt())),
                     [safe, pcts] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         const float f = pcts[r - 1] / 100.0f;
                         safe->setSize (juce::roundToInt (W * f), juce::roundToInt (H * f));
                     });
}

//==============================================================================
void TubeLiftEditor::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour dot) const
{
    g.setColour (cPanel);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (cLabel);
    g.setFont (mono (12.0f));
    g.drawText (title, r.getX() + 14, r.getY() + 8, r.getWidth() - 40, 14, juce::Justification::centredLeft);
    g.setColour (dot);
    g.fillEllipse (r.getRight() - 19, r.getY() + 11, 8, 8);
}

void TubeLiftEditor::drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                               juce::Colour fill, juce::Colour textCol, float fontSize) const
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
    g.setColour (textCol);
    g.setFont (mono (fontSize, true));
    g.drawText (text, r, juce::Justification::centred);
}

void TubeLiftEditor::drawMeter (juce::Graphics& g) const
{
    g.setColour (cInset);
    g.fillRoundedRectangle (meterArea, 6.0f);

    const juce::Point<float> pivot (meterArea.getCentreX(), meterArea.getBottom() + 34.0f);
    const float radius = 118.0f;
    const float a0 = juce::degreesToRadians (-44.0f), a1 = juce::degreesToRadians (44.0f);
    auto angleFor = [&] (float db) { return a0 + (a1 - a0) * vuPos (db); };
    auto pointAt = [&] (float ang, float r) { return pivot.getPointOnCircumference (r, ang); };

    g.saveState();
    g.reduceClipRegion (meterArea.reduced (2).toNearestInt());

    // scale arc + hot zone
    {
        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, a0, angleFor (0.0f), true);
        g.setColour (cLabel);
        g.strokePath (arc, juce::PathStrokeType (1.5f));
        juce::Path hot;
        hot.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (0.0f), a1, true);
        g.setColour (cOrange);
        g.strokePath (hot, juce::PathStrokeType (4.0f));
    }

    // ticks + labels
    const float marks[] = { -20, -10, -7, -5, -3, -1, 0, 1, 2, 3 };
    g.setFont (mono (10.0f, true));
    for (float m : marks)
    {
        const float ang = angleFor (m);
        const bool big = m == -20 || m == -10 || m == -5 || m == 0 || m == 3 || m == -3 || m == -7;
        g.setColour (m > 0 ? cOrange : cText);
        g.drawLine ({ pointAt (ang, radius - (big ? 9.0f : 6.0f)), pointAt (ang, radius + 2.0f) }, 1.5f);
        if (big)
        {
            const auto lp = pointAt (ang, radius + 13.0f);
            const juce::String s = m > 0 ? "+" + juce::String ((int) m) : juce::String ((int) m);
            g.drawText (s, juce::Rectangle<float> (40, 12).withCentre (lp), juce::Justification::centred);
        }
    }

    // caption
    g.setFont (mono (11.0f, true));
    g.setColour (cLabel);
    g.drawText ("ATTITUDE", meterArea.getX(), meterArea.getBottom() - 30, meterArea.getWidth(), 14, juce::Justification::centred);

    // needle
    {
        const float ang = angleFor (needleDb);
        g.setColour (cText);
        g.drawLine ({ pointAt (ang, 30.0f), pointAt (ang, radius + 4.0f) }, 2.0f);
    }
    g.restoreState();

    // pivot cap
    g.setColour (cInset2);
    g.fillRoundedRectangle (meterArea.getX() + 110, meterArea.getBottom() - 12, meterArea.getWidth() - 220, 6, 3);
}

void TubeLiftEditor::paint (juce::Graphics& g)
{
    g.fillAll (cBg);
    g.addTransform (juce::AffineTransform::scale (uiScale()));
    auto hov = [this] (Kind k, int idx = -1) { return hover.kind == k && hover.index == idx && active.kind == None; };

    const float drive = value ("drive");

    // ---------- header ----------
    g.setColour (cPanel);
    g.fillRoundedRectangle (pHeader, 7.0f);
    g.setFont (mono (25.0f, true));
    g.setColour (cText);
    g.drawText ("TUBE", pHeader.getX() + 16, pHeader.getY(), 90, pHeader.getHeight(), juce::Justification::centredLeft);
    g.setColour (cOrange);
    g.drawText ("LIFT", pHeader.getX() + 16 + 56, pHeader.getY(), 90, pHeader.getHeight(), juce::Justification::centredLeft);
    g.setFont (mono (11.0f));
    g.setColour (cLabel);
    g.drawText ("A-STYLE TUBE PREAMP", pHeader.getX() + 140, pHeader.getY() + 2, 150, pHeader.getHeight(), juce::Justification::centredLeft);

    drawPill (g, bSize, "SIZE " + juce::String (juce::roundToInt (uiScale() * 100.0f)) + "%", hov (SizeBtn) ? cInset2 : cInset, cText);
    {
        juce::Path tri;
        const float tx = bSize.getRight() - 12, ty = bSize.getCentreY();
        tri.addTriangle (tx - 3.5f, ty - 2, tx + 3.5f, ty - 2, tx, ty + 2.5f);
        g.setColour (cText);
        g.fillPath (tri);
    }
    {
        const juce::String st = "DRIVE " + juce::String (drive, 1) + dotSep + "MIX " + juce::String (juce::roundToInt (value ("mix"))) + "%";
        const float pw = st.length() * 6.4f + 22.0f;
        drawPill (g, { pHeader.getRight() - 14 - pw, pHeader.getY() + 13, pw, 20 }, st, cTeal, juce::Colours::white, 11.0f);
    }

    // ---------- drive ----------
    drawPanel (g, pDrive, "DRIVE", cOrange);
    g.setColour (cInset);
    g.fillRoundedRectangle (driveBox, 7.0f);
    if (hov (DriveBox) || active.kind == DriveBox)
    {
        g.setColour (cInset2);
        g.fillRoundedRectangle (driveBox.reduced (4), 5.0f);
    }
    {
        // 0-10 step bars along the bottom of the box
        const auto bars = juce::Rectangle<float> (driveBox.getX() + 12, driveBox.getBottom() - 16, driveBox.getWidth() - 24, 8);
        const float bw = bars.getWidth() / 20.0f;
        const int lit = juce::roundToInt (drive * 2.0f);
        for (int k = 0; k < 20; ++k)
        {
            g.setColour (k < lit ? (k >= 16 ? cOrange : cGreen) : cInset2.darker (0.06f));
            g.fillRoundedRectangle (bars.getX() + k * bw + 1.0f, bars.getY(), bw - 2.0f, bars.getHeight(), 1.5f);
        }
        g.setColour (cLabel);
        g.setFont (mono (10.0f));
        g.drawText ("DRAG / WHEEL", driveBox.getX() + 12, driveBox.getY() + 7, 120, 12, juce::Justification::centredLeft);
        g.drawText ("DBL-CLICK = 3.0", driveBox.getRight() - 132, driveBox.getY() + 7, 120, 12, juce::Justification::centredRight);
        g.setColour (cOrange);
        g.setFont (mono (36.0f, true));
        g.drawText (juce::String (drive, 1), driveBox.withTrimmedTop (8).withTrimmedBottom (20), juce::Justification::centred);
    }

    // ---------- meter ----------
    drawPanel (g, pMeter, "ATTITUDE", cBlue);
    drawMeter (g);

    // ---------- filters / output ----------
    drawPanel (g, pFilters, "FILTERS & TONE", cTeal);
    drawPanel (g, pOut, "OUTPUT", cPurple);
    for (int i = 0; i < (int) sliders.size(); ++i)
    {
        const auto& s = sliders[(size_t) i];
        const auto tr = trackRect (s);
        const bool hot = hov (Slider, i) || (active.kind == Slider && active.index == i);
        g.setFont (mono (11.0f));
        g.setColour (cLabel);
        g.drawText (s.label, s.row.getX(), s.row.getY(), s.labelW - 6, s.row.getHeight(), juce::Justification::centredLeft);
        g.setFont (mono (12.0f, true));
        g.setColour (cText);
        g.drawText (formatValue (i), tr.getRight() + 4, s.row.getY(), s.valueW - 4, s.row.getHeight(), juce::Justification::centredRight);

        const float n = param (s.id)->getValue();
        const float y = tr.getCentreY();
        g.setColour (cTrack);
        g.fillRect (tr.getX(), y - 1, tr.getWidth(), 2.0f);
        g.setColour (s.col);
        if (s.bipolar)
        {
            const float cx = tr.getX() + 0.5f * tr.getWidth(), hx = tr.getX() + n * tr.getWidth();
            g.fillRect (std::min (cx, hx), y - 1, std::abs (hx - cx), 2.0f);
        }
        else
            g.fillRect (tr.getX(), y - 1, n * tr.getWidth(), 2.0f);
        g.setColour (hot ? cText : s.col);
        g.fillRect (tr.getX() + n * tr.getWidth() - 1.5f, y - 6, 3.0f, 12.0f);
    }

    g.setColour (cInset);
    g.fillRoundedRectangle (levelBar, 4.0f);
    g.setColour (levelDisp > 0.95f ? cOrange : cGreen);
    g.fillRoundedRectangle (levelBar.withWidth (std::max (8.0f, levelDisp * levelBar.getWidth())), 4.0f);
    g.setFont (mono (10.0f));
    g.setColour (cLabel);
    g.drawText ("LEVEL", levelBar.getX(), levelBar.getY() - 16, 80, 12, juce::Justification::centredLeft);
}
