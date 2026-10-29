#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "Layout.h"

namespace rtui
{
    juce::Image loadImage (const juce::String& resourceName);

    // Rotary knob with a baked knob body; pointer + value readout drawn in code.
    class Knob : public juce::Slider
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& a, const juce::String& id, int steps);
        void paint (juce::Graphics&) override;
        int steps;
    };

    // Piano-key style button. momentary unless 'toggle' or latch mode is on.
    class KeyButton : public juce::Component, private juce::Timer
    {
    public:
        KeyButton (juce::AudioProcessorValueTreeState& a, const juce::String& id, const juce::String& asset,
                   bool isToggle, juce::RangedAudioParameter* latch);
        ~KeyButton() override { stopTimer(); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        std::function<void (KeyButton*)> onEngaged;
        void forceOff();
        bool isOn() const { return param->getValue() > 0.5f; }
    private:
        void timerCallback() override { repaint(); }
        void set (bool on);
        juce::RangedAudioParameter* param;
        juce::RangedAudioParameter* latchParam;
        juce::Image up, down;
        bool toggle, held = false;
    };
}

class ReelTransitEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ReelTransitEditor (ReelTransitProcessor&);
    ~ReelTransitEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
private:
    void timerCallback() override;
    void drawVU (juce::Graphics&, const int rect[4], float level, float& shown);
    void drawReel (juce::Graphics&, int cx, int cy, float packR, float angle);

    ReelTransitProcessor& proc;
    juce::Image faceplate, knobImg, reelImg, vuIn, vuOut, vuGlass;
    juce::OwnedArray<rtui::Knob> knobs;
    juce::OwnedArray<rtui::KeyButton> keys;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> attach;
    juce::Font sigFont;
    float angL = 0.f, angR = 0.f, shownIn = 0.f, shownOut = 0.f, smoothSpeed = 1.f, tapeMoved = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelTransitEditor)
};
