#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <memory>
#include "PluginProcessor.h"

// =============================================================================
//  Paleta y utilidades
// =============================================================================
namespace SedaUI
{
    inline const juce::Colour cBgTop    { 0xff131c29 };
    inline const juce::Colour cBgBot    { 0xff070b11 };
    inline const juce::Colour cPanelTop { 0xff18232f };
    inline const juce::Colour cPanelBot { 0xff111a25 };
    inline const juce::Colour cBorder   { 0xff263548 };
    inline const juce::Colour cTrack    { 0xff243246 };
    inline const juce::Colour cGold     { 0xffe9c46a };
    inline const juce::Colour cGoldDim  { 0xff9c8244 };
    inline const juce::Colour cText     { 0xffe6edf5 };
    inline const juce::Colour cDim      { 0xff7f93a8 };
    inline const juce::Colour cRed      { 0xffe5484d };
}

// Fuente compatible con JUCE 7 y 8
inline juce::Font sedaFont (float height, bool bold = false)
{
   #if JUCE_MAJOR_VERSION >= 8
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
   #else
    return juce::Font (height, bold ? juce::Font::bold : juce::Font::plain);
   #endif
}

// =============================================================================
//  Look & Feel: perillas, cajas de valor y selector de presets
// =============================================================================
class SedaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SedaLookAndFeel()
    {
        using namespace SedaUI;
        setColour (juce::Slider::textBoxTextColourId,       cText);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,  cGold.withAlpha (0.35f));
        setColour (juce::TextEditor::focusedOutlineColourId, cGold.withAlpha (0.7f));
        setColour (juce::CaretComponent::caretColourId,     cGold);

        setColour (juce::ComboBox::textColourId,            cText);
        setColour (juce::PopupMenu::backgroundColourId,     juce::Colour (0xff131d29));
        setColour (juce::PopupMenu::textColourId,           cText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, cGold.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, cText);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        using namespace SedaUI;

        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
        const float dia = std::min (bounds.getWidth(), bounds.getHeight());
        auto area = juce::Rectangle<float> (dia, dia).withCentre (bounds.getCentre());
        const auto c = area.getCentre();

        const float thick = 4.0f;
        const float arcR = dia * 0.5f - thick * 0.5f;
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        // pista
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (cTrack);
        g.strokePath (track, stroke);

        // arco de valor con halo
        if (sliderPos > 0.002f)
        {
            juce::Path val;
            val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (cGold.withAlpha (0.16f));
            g.strokePath (val, juce::PathStrokeType (thick + 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (cGold);
            g.strokePath (val, stroke);
        }

        // cuerpo de la perilla
        auto body = area.reduced (thick + 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (body.translated (0.0f, 2.0f).expanded (1.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c4a5e), body.getX(), body.getY(),
                                                 juce::Colour (0xff111923), body.getRight(), body.getBottom(), false));
        g.fillEllipse (body);
        g.setColour (juce::Colour (0xff566883));
        g.drawEllipse (body, 1.0f);

        // aguja
        const float r = body.getWidth() * 0.5f;
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.7f, -r + 3.0f, 3.4f, r * 0.45f, 1.7f);
        g.setColour (cGold.brighter (0.25f));
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (sedaFont (12.5f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace SedaUI;
        auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (juce::Colour (0xff152030));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (box.hasKeyboardFocus (true) ? cGold : cBorder);
        g.drawRoundedRectangle (b, 6.0f, 1.0f);

        juce::Path chevron;
        const float cx = (float) width - 18.0f, cy = (float) height * 0.5f;
        chevron.startNewSubPath (cx - 4.5f, cy - 2.0f);
        chevron.lineTo (cx, cy + 2.5f);
        chevron.lineTo (cx + 4.5f, cy - 2.0f);
        g.setColour (cGold);
        g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return sedaFont (14.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (12, 1, box.getWidth() - 36, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
};

// =============================================================================
//  Medidor estéreo de picos con peak-hold e indicador de clip (clic = reinicia)
// =============================================================================
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& titleText) : title (titleText)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    // Llamar desde el timer con el pico lineal acumulado desde la ultima llamada
    void push (float peakL, float peakR, float dt)
    {
        update (ch[0], peakL, dt);
        update (ch[1], peakR, dt);
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        for (auto& c : ch) { c.hold = kMinDb; c.holdTime = 0.0f; c.clip = false; }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace SedaUI;
        auto b = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient (cPanelTop, b.getX(), b.getY(), cPanelBot, b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (cBorder);
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

        // titulo
        g.setColour (cGold.withAlpha (0.9f));
        g.setFont (sedaFont (11.0f, true));
        g.drawText (title, b.removeFromTop (30.0f).withTrimmedTop (10.0f), juce::Justification::centred, false);

        // lectura del pico maximo
        auto readout = b.removeFromTop (20.0f);
        const float peakDb = std::max (ch[0].hold, ch[1].hold);
        const bool clipped = ch[0].clip || ch[1].clip;
        const juce::String txt = peakDb <= kMinDb + 0.1f ? juce::String ("-inf") : juce::String (peakDb, 1);
        g.setColour (clipped ? cRed : cText);
        g.setFont (sedaFont (12.0f, true));
        g.drawText (txt, readout, juce::Justification::centred, false);

        // etiquetas L / R
        auto bottom = b.removeFromBottom (18.0f);
        b.removeFromTop (4.0f);

        const float barW = 16.0f, midW = 24.0f;
        const float totalW = barW * 2.0f + midW;
        const float x0 = b.getCentreX() - totalW * 0.5f;
        const auto left  = juce::Rectangle<float> (x0, b.getY(), barW, b.getHeight());
        const auto mid   = juce::Rectangle<float> (x0 + barW, b.getY(), midW, b.getHeight());
        const auto right = juce::Rectangle<float> (x0 + barW + midW, b.getY(), barW, b.getHeight());

        g.setColour (cDim);
        g.setFont (sedaFont (10.0f, true));
        g.drawText ("L", bottom.withX (left.getX()).withWidth (barW), juce::Justification::centred, false);
        g.drawText ("R", bottom.withX (right.getX()).withWidth (barW), juce::Justification::centred, false);

        drawBar (g, left,  ch[0]);
        drawBar (g, right, ch[1]);

        // escala en dB entre las dos barras
        g.setColour (cDim);
        g.setFont (sedaFont (9.0f));
        for (int db : { 0, -6, -12, -24, -36, -48 })
        {
            const float y = yFor (mid, (float) db);
            g.drawText (juce::String (db), mid.withY (y - 6.0f).withHeight (12.0f), juce::Justification::centred, false);
        }
    }

private:
    struct Ch { float db = kMinDb, hold = kMinDb, holdTime = 0.0f; bool clip = false; };

    static constexpr float kMinDb = -60.0f, kMaxDb = 6.0f;

    static float yFor (const juce::Rectangle<float>& r, float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
        return r.getBottom() - t * r.getHeight();
    }

    static void update (Ch& c, float peakLin, float dt)
    {
        const float pdb = peakLin > 1.0e-5f ? juce::Decibels::gainToDecibels (peakLin) : kMinDb;
        c.db = std::max (pdb, c.db - 26.0f * dt);               // ataque instantaneo, caida ~26 dB/s
        if (pdb >= c.hold) { c.hold = pdb; c.holdTime = 1.6f; }  // peak-hold 1.6 s
        else if ((c.holdTime -= dt) <= 0.0f) c.hold = std::max (c.db, c.hold - 14.0f * dt);
        if (peakLin >= 1.0f) c.clip = true;
    }

    static void drawBar (juce::Graphics& g, const juce::Rectangle<float>& r, const Ch& c)
    {
        using namespace SedaUI;

        g.setColour (juce::Colour (0xff080c12));
        g.fillRoundedRectangle (r, 3.0f);

        const float yLevel = yFor (r, c.db);
        if (yLevel < r.getBottom() - 0.5f)
        {
            juce::ColourGradient grad (juce::Colour (0xff1f9d5c), 0.0f, r.getBottom(),
                                       cRed, 0.0f, r.getY(), false);
            grad.addColour ((-18.0f - kMinDb) / (kMaxDb - kMinDb), juce::Colour (0xff3ddc84));
            grad.addColour ((-8.0f  - kMinDb) / (kMaxDb - kMinDb), cGold);
            grad.addColour ((-1.0f  - kMinDb) / (kMaxDb - kMinDb), juce::Colour (0xffff9f43));
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> (r.getX(), yLevel, r.getWidth(), r.getBottom() - yLevel));
        }

        // segmentos tipo LED
        g.setColour (juce::Colour (0xff080c12).withAlpha (0.85f));
        for (float yy = r.getY() + 2.0f; yy < r.getBottom(); yy += 3.0f)
            g.fillRect (r.getX(), yy, r.getWidth(), 1.0f);

        // peak-hold
        if (c.hold > kMinDb + 0.1f)
        {
            const float yh = yFor (r, c.hold);
            g.setColour (c.hold >= 0.0f ? cRed : cText);
            g.fillRect (r.getX(), yh - 1.0f, r.getWidth(), 2.0f);
        }

        g.setColour (cBorder);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    juce::String title;
    std::array<Ch, 2> ch {};
};

// =============================================================================
//  Agrupacion de parametros en paneles
// =============================================================================
struct SedaGroup { const char* title; int idx[4]; };

inline constexpr SedaGroup kSedaGroups[4] =
{
    { "SPACE",             { pMix,       pDecay, pPreDelay,  pSize } },
    { "TONE",              { pDamping,   pBass,  pLowCut,    pHighCut } },
    { "TEXTURE",           { pDiffusion, pEarly, pModDepth,  pModRate } },
    { "STEREO & DYNAMICS", { pWidth,     pDeEss, pDuck,      pDuckRelease } },
};

// =============================================================================
//  Editor
// =============================================================================
class SedaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SedaEditor (SedaProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p), inMeter ("INPUT"), outMeter ("OUTPUT")
    {
        setLookAndFeel (&laf);

        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& d = kSedaParams[i];
            auto& k = knobs[(size_t) i];

            k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
            k.slider.setTextValueSuffix (d.unit);
            k.slider.setDoubleClickReturnValue (true, (double) d.def);
            addAndMakeVisible (k.slider);

            k.label.setText (juce::String (d.name).toUpperCase(), juce::dontSendNotification);
            k.label.setJustificationType (juce::Justification::centred);
            k.label.setFont (sedaFont (10.5f, true));
            k.label.setColour (juce::Label::textColourId, SedaUI::cDim);
            addAndMakeVisible (k.label);

            k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, d.id, k.slider);
        }

        for (int i = 0; i < kNumSedaPresets; ++i)
            presetBox.addItem (kSedaPresets[i].name, i + 1);
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
        presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
        addAndMakeVisible (presetBox);

        addAndMakeVisible (inMeter);
        addAndMakeVisible (outMeter);

        latencyCaption.setText ("LATENCY", juce::dontSendNotification);
        latencyCaption.setFont (sedaFont (10.0f, true));
        latencyCaption.setJustificationType (juce::Justification::centredRight);
        latencyCaption.setColour (juce::Label::textColourId, SedaUI::cDim);
        addAndMakeVisible (latencyCaption);

        latencyValue.setFont (sedaFont (13.0f, true));
        latencyValue.setJustificationType (juce::Justification::centredRight);
        latencyValue.setColour (juce::Label::textColourId, SedaUI::cText);
        addAndMakeVisible (latencyValue);

        refreshLatency();
        setSize (920, 540);
        startTimerHz (30);
    }

    ~SedaEditor() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace SedaUI;

        g.setGradientFill (juce::ColourGradient (cBgTop, 0.0f, 0.0f, cBgBot, 0.0f, (float) getHeight(), false));
        g.fillAll();

        const auto content = getLocalBounds().reduced (20, 14);

        // --- logo: ondas de reverb + nombre
        const float lx = (float) content.getX() + 4.0f, ly = (float) content.getY() + 30.0f;
        for (int i = 0; i < 3; ++i)
        {
            juce::Path wave;
            const float rad = 8.0f + 7.0f * (float) i;
            wave.addCentredArc (lx, ly, rad, rad, 0.0f,
                                juce::MathConstants<float>::halfPi - 0.75f,
                                juce::MathConstants<float>::halfPi + 0.75f, true);
            g.setColour (cGold.withAlpha (1.0f - 0.3f * (float) i));
            g.strokePath (wave, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (cGold);
        g.fillEllipse (lx - 3.0f, ly - 3.0f, 6.0f, 6.0f);

        const int tx = content.getX() + 46;
        g.setGradientFill (juce::ColourGradient (cGold.brighter (0.3f), 0.0f, (float) content.getY() + 6.0f,
                                                 cGoldDim.brighter (0.2f), 0.0f, (float) content.getY() + 42.0f, false));
        g.setFont (sedaFont (34.0f, true));
        g.drawText ("SEDA", tx, content.getY() + 4, 200, 40, juce::Justification::centredLeft, false);
        g.setColour (cDim);
        g.setFont (sedaFont (11.0f, true));
        g.drawText ("VOCAL REVERB", tx + 2, content.getY() + 42, 200, 16, juce::Justification::centredLeft, false);

        // --- cabecera
        g.setColour (cDim);
        g.setFont (sedaFont (10.0f, true));
        g.drawText ("PRESET", presetCaption, juce::Justification::centredLeft, false);

        g.setColour (cBorder);
        g.fillRect (content.getX(), content.getY() + 64, content.getWidth(), 1);

        // --- paneles
        for (int i = 0; i < 4; ++i)
        {
            const auto b = panelBounds[(size_t) i].toFloat();
            g.setGradientFill (juce::ColourGradient (cPanelTop, b.getX(), b.getY(), cPanelBot, b.getX(), b.getBottom(), false));
            g.fillRoundedRectangle (b, 10.0f);
            g.setColour (cBorder);
            g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

            g.setColour (cGold.withAlpha (0.9f));
            g.setFont (sedaFont (11.0f, true));
            g.drawText (kSedaGroups[i].title, (int) b.getX() + 16, (int) b.getY() + 10, (int) b.getWidth() - 32, 16,
                        juce::Justification::centredLeft, false);
            g.setColour (cBorder);
            g.fillRect ((int) b.getX() + 16, (int) b.getY() + 30, (int) b.getWidth() - 32, 1);
        }

        // --- pie: credito
        juce::AttributedString credit;
        credit.setJustification (juce::Justification::centred);
        credit.append ("Made by ", sedaFont (12.5f), cDim);
        credit.append (juce::String (L"David Garc\u00EDa"), sedaFont (12.5f, true), cGold);
        credit.append (" (DavertBlaze)", sedaFont (12.5f), cDim);
        credit.draw (g, footerBounds.toFloat());

        g.setColour (cDim.withAlpha (0.6f));
        g.setFont (sedaFont (10.5f));
        g.drawText ("v1.0.0", footerBounds, juce::Justification::centredRight, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 14);
        auto header = r.removeFromTop (64);
        footerBounds = r.removeFromBottom (24);
        r.removeFromTop (10);
        r.removeFromBottom (8);

        // cabecera: latencia (derecha) y presets
        auto latencyArea = header.removeFromRight (170);
        latencyCaption.setBounds (latencyArea.removeFromTop (28).withTrimmedTop (10));
        latencyValue.setBounds (latencyArea.removeFromTop (22));
        header.removeFromRight (16);
        auto presetArea = header.removeFromRight (210);
        presetCaption = presetArea.removeFromTop (22);
        presetBox.setBounds (presetArea.removeFromTop (30));

        // medidores a los lados
        inMeter.setBounds (r.removeFromLeft (66));
        r.removeFromLeft (12);
        outMeter.setBounds (r.removeFromRight (66));
        r.removeFromRight (12);

        // 4 paneles en cuadricula 2x2
        const int gap = 12;
        const int pw = (r.getWidth() - gap) / 2, ph = (r.getHeight() - gap) / 2;
        for (int gi = 0; gi < 4; ++gi)
        {
            const auto panel = juce::Rectangle<int> (r.getX() + (gi % 2) * (pw + gap),
                                                     r.getY() + (gi / 2) * (ph + gap), pw, ph);
            panelBounds[(size_t) gi] = panel;

            auto inner = panel.reduced (14, 8);
            inner.removeFromTop (30);
            const int cw = inner.getWidth() / 4;
            for (int k = 0; k < 4; ++k)
            {
                auto cell = juce::Rectangle<int> (inner.getX() + k * cw, inner.getY(), cw, inner.getHeight()).reduced (2, 0);
                auto& knob = knobs[(size_t) kSedaGroups[gi].idx[k]];
                knob.label.setBounds (cell.removeFromTop (16));
                knob.slider.setBounds (cell);
            }
        }
    }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void timerCallback() override
    {
        constexpr float dt = 1.0f / 30.0f;
        float l = 0.0f, r = 0.0f;
        proc.readInputPeaks (l, r);
        inMeter.push (l, r, dt);
        proc.readOutputPeaks (l, r);
        outMeter.push (l, r, dt);
        refreshLatency();
    }

    void refreshLatency()
    {
        const int smp = proc.getLatencySamples();
        const double sr = proc.getSampleRate();
        const double ms = sr > 0.0 ? 1000.0 * (double) smp / sr : 0.0;
        latencyValue.setText (juce::String (smp) + " smp  |  " + juce::String (ms, 2) + " ms",
                              juce::dontSendNotification);
    }

    SedaProcessor& proc;
    SedaLookAndFeel laf;
    LevelMeter inMeter, outMeter;
    juce::ComboBox presetBox;
    juce::Label latencyCaption, latencyValue;
    std::array<Knob, kNumParams> knobs;
    std::array<juce::Rectangle<int>, 4> panelBounds;
    juce::Rectangle<int> footerBounds, presetCaption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SedaEditor)
};
