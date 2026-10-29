#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

// Reel Transit by Tal Mendel
// A tape machine built for transitions: tape stop / spin-up, reverse, rolling stutter,
// tape riser and echo throw, all firing from momentary buttons (or host automation)
// on top of an always-on tape character (age: wow/flutter, HF loss, hiss, dropouts, print-through;
// drive: oversampled tape saturation; head bump).
namespace rt
{
    // cubic Hermite read from a power-of-two circular buffer at an absolute (fractional) position
    inline float hermite (const float* b, int mask, double pos)
    {
        const double fl = std::floor (pos);
        const juce::int64 i1 = (juce::int64) fl;
        const float t = (float) (pos - fl);
        const float y0 = b[(int) ((i1 - 1) & mask)];
        const float y1 = b[(int) (i1 & mask)];
        const float y2 = b[(int) ((i1 + 1) & mask)];
        const float y3 = b[(int) ((i1 + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
}

class ReelTransitProcessor : public juce::AudioProcessor
{
public:
    ReelTransitProcessor();
    ~ReelTransitProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Reel Transit"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // read by the editor
    std::atomic<float> inLevel { 0.f }, outLevel { 0.f }, echoLevel { 0.f };
    std::atomic<float> headSpeed { 1.f };       // signed: 1 normal, 0 stopped, <0 reverse
    std::atomic<int>   modeNow { 0 };           // 0 idle, 1 stop, 2 reverse, 3 stutter, 4 rise, 5 spin-up
    std::atomic<bool>  hostPlaying { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    enum Mode { Idle = 0, Stop, Reverse, Stutter, Rise, Spin };

    void engage (Mode m);
    void releaseEngine (bool spinUp);
    void stepEngine (float& hl, float& hr, juce::int64 w);
    float fadeWin (double d, double len) const;

    double sampleRate = 44100.0;
    int maxBlock = 4096;

    // ---- main tape buffer (raw input history)
    std::array<std::vector<float>, 2> buf;
    int bufMask = 0, bufSize = 0;
    juce::int64 wc = 0;

    // ---- transition engine
    Mode mode = Idle;
    float wet = 0.f, wetTarget = 0.f;
    double head = 0.0, speed = 1.0, spinStartSpeed = 0.0;
    double reqLenS = 256.0, rs = 0.0, lenS = 0.0, lenCur = 0.0, revAnchor = 0.0;
    double prog = 0.0, stopSamples = 1.0, spinSamples = 1.0, riseSamples = 1.0, curveExp = 1.f;
    double stutterRatio = 1.0, rollFactor = 1.0, minLen = 256.0, riseRange = 12.0;
    float fadeN = 200.f;
    int activeIdx = -1;
    std::array<bool, 4> prevB { false, false, false, false };
    std::array<float, 2> toneLp {}, riseLp {};
    float engCoef = 0.f, relCoef = 0.f;

    // ---- echo throw
    std::array<std::vector<float>, 2> echoBuf;
    int echoMask = 0;
    juce::int64 ewc = 0;
    float sendG = 0.f, sendCoef = 0.f, echoDelaySm = 0.f, echoDelayCoef = 0.f, echoPhase = 0.f;
    std::array<float, 2> echoLp {};
    float echoLpA = 0.f;

    // ---- tape character
    std::array<std::vector<float>, 2> wfBuf;
    int wfMask = 0;
    juce::int64 wfc = 0;
    float wowPhase = 0.f, flutPhase = 0.f, flutRand = 0.f;
    float wfBase = 0.f;
    juce::Random rng;

    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    using Duplicator = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    Duplicator headBump;

    std::array<float, 2> dcX {}, dcY {}, dc2X {}, dc2Y {}, hf1 {}, hf2 {}, noiseLp {};
    float dropEnv = 1.f, dropTarget = 1.f;
    int dropRemain = 0;

    juce::SmoothedValue<float> driveSm, ageSm, outSm;

    std::atomic<float> *pStop, *pReverse, *pStutter, *pRise, *pThrow, *pStopTime, *pStopCurve, *pSpin,
                       *pLength, *pRoll, *pPitch, *pRiseTime, *pRiseRange, *pThrowDiv, *pThrowFb,
                       *pAge, *pDrive, *pOutput;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReelTransitProcessor)
};
