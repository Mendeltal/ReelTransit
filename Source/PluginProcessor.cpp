#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

static constexpr float lenBeats[6]   = { 0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f };
static constexpr float throwBeats[6] = { 0.5f, 0.75f, 1.f, 1.5f, 2.f, 4.f };

APVTS::ParameterLayout ReelTransitProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto addFloat = [&] (const char* id, const char* name, NormalisableRange<float> r, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def));
    };
    auto addChoice = [&] (const char* id, const char* name, StringArray items, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };
    auto addBool = [&] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    addBool   ("stop",      "Tape Stop",    false);
    addBool   ("reverse",   "Reverse",      false);
    addBool   ("stutter",   "Stutter",      false);
    addBool   ("rise",      "Riser",        false);
    addBool   ("throw",     "Echo Throw",   false);
    addBool   ("latch",     "Latch",        false);

    addFloat  ("stopTime",  "Stop Time",    { 80.f, 4000.f, 1.f, 0.5f }, 1200.f);
    addFloat  ("stopCurve", "Stop Curve",   { 0.f, 1.f, 0.001f }, 0.45f);
    addBool   ("spinUp",    "Spin-Up",      true);
    addChoice ("length",    "Length",       { "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar" }, 3);
    addFloat  ("roll",      "Roll",         { 0.f, 1.f, 0.001f }, 0.f);
    addFloat  ("pitch",     "Stutter Pitch",{ -12.f, 12.f, 0.01f }, 0.f);
    addFloat  ("riseTime",  "Rise Time",    { 250.f, 8000.f, 1.f, 0.5f }, 2500.f);
    addFloat  ("riseRange", "Rise Range",   { 0.f, 24.f, 0.01f }, 12.f);
    addChoice ("throwDiv",  "Throw Time",   { "1/8", "1/8.", "1/4", "1/4.", "1/2", "1 Bar" }, 3);
    addFloat  ("throwFb",   "Throw Feedback", { 0.f, 0.92f, 0.001f }, 0.55f);
    addFloat  ("age",       "Age",          { 0.f, 1.f, 0.001f }, 0.25f);
    addFloat  ("drive",     "Drive",        { 0.f, 1.f, 0.001f }, 0.25f);
    addFloat  ("output",    "Output",       { -24.f, 12.f, 0.01f }, 0.f);

    return { p.begin(), p.end() };
}

ReelTransitProcessor::ReelTransitProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pStop      = apvts.getRawParameterValue ("stop");
    pReverse   = apvts.getRawParameterValue ("reverse");
    pStutter   = apvts.getRawParameterValue ("stutter");
    pRise      = apvts.getRawParameterValue ("rise");
    pThrow     = apvts.getRawParameterValue ("throw");
    pStopTime  = apvts.getRawParameterValue ("stopTime");
    pStopCurve = apvts.getRawParameterValue ("stopCurve");
    pSpin      = apvts.getRawParameterValue ("spinUp");
    pLength    = apvts.getRawParameterValue ("length");
    pRoll      = apvts.getRawParameterValue ("roll");
    pPitch     = apvts.getRawParameterValue ("pitch");
    pRiseTime  = apvts.getRawParameterValue ("riseTime");
    pRiseRange = apvts.getRawParameterValue ("riseRange");
    pThrowDiv  = apvts.getRawParameterValue ("throwDiv");
    pThrowFb   = apvts.getRawParameterValue ("throwFb");
    pAge       = apvts.getRawParameterValue ("age");
    pDrive     = apvts.getRawParameterValue ("drive");
    pOutput    = apvts.getRawParameterValue ("output");
}

bool ReelTransitProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ReelTransitProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    const float fs = (float) sr;
    maxBlock = juce::jmax (samplesPerBlock, 512);

    bufSize = juce::nextPowerOfTwo ((int) (11.0 * sr));
    bufMask = bufSize - 1;
    for (auto& b : buf) b.assign ((size_t) bufSize, 0.f);
    wc = 0;

    const int echoSize = juce::nextPowerOfTwo ((int) (6.0 * sr));
    echoMask = echoSize - 1;
    for (auto& b : echoBuf) b.assign ((size_t) echoSize, 0.f);
    ewc = 0;

    const int wfSize = juce::nextPowerOfTwo ((int) (0.05 * sr));
    wfMask = wfSize - 1;
    for (auto& b : wfBuf) b.assign ((size_t) wfSize, 0.f);
    wfc = 0;
    wfBase = 0.006f * fs;

    oversampling.initProcessing ((size_t) maxBlock);
    oversampling.reset();
    setLatencySamples ((int) std::lround (oversampling.getLatencyInSamples() + wfBase));

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
    headBump.prepare (spec);
    *headBump.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, 85.f, 0.7f, juce::Decibels::decibelsToGain (1.4f));
    headBump.reset();

    mode = Idle;  wet = wetTarget = 0.f;  activeIdx = -1;  prevB.fill (false);
    toneLp.fill (0.f);  riseLp.fill (0.f);  echoLp.fill (0.f);
    dcX.fill (0.f); dcY.fill (0.f); dc2X.fill (0.f); dc2Y.fill (0.f); hf1.fill (0.f); hf2.fill (0.f); noiseLp.fill (0.f);
    dropEnv = dropTarget = 1.f;  dropRemain = 0;
    sendG = 0.f;  echoDelaySm = 0.f;  wowPhase = flutPhase = flutRand = echoPhase = 0.f;

    engCoef  = 1.f - std::exp (-1.f / (0.010f * fs));
    relCoef  = 1.f - std::exp (-1.f / (0.030f * fs));
    sendCoef = 1.f - std::exp (-1.f / (0.006f * fs));
    echoDelayCoef = 1.f - std::exp (-1.f / (0.060f * fs));
    echoLpA  = 1.f - std::exp (-juce::MathConstants<float>::twoPi * 4200.f / fs);
    fadeN = 0.004f * fs;

    driveSm.reset (sr * 2.0, 0.04); driveSm.setCurrentAndTargetValue (pDrive->load());
    ageSm.reset   (sr, 0.05);       ageSm.setCurrentAndTargetValue   (pAge->load());
    outSm.reset   (sr, 0.03);       outSm.setCurrentAndTargetValue   (juce::Decibels::decibelsToGain (pOutput->load()));
}

float ReelTransitProcessor::fadeWin (double d, double len) const
{
    const double edge = juce::jmin (d, len - d);
    const double fn = juce::jmax (8.0, juce::jmin ((double) fadeN, len * 0.25));
    return (float) juce::jlimit (0.0, 1.0, edge / fn);
}

void ReelTransitProcessor::engage (Mode m)
{
    mode = m;
    wetTarget = 1.f;
    prog = 0.0;

    const double maxLen = (double) (bufSize / 2);
    double want = reqLenS;   // requested length, locked in at the moment of engagement
    want = juce::jlimit (64.0, juce::jmax (64.0, juce::jmin (maxLen, (double) wc - 4.0)), want);
    lenS = want;

    const double anchor = (double) wc - 1.0;

    switch (m)
    {
        case Stop:    head = anchor; speed = 1.0; break;
        case Reverse: head = anchor; revAnchor = anchor; rs = anchor - lenS; break;
        case Stutter: rs = anchor - lenS; head = rs; lenCur = lenS; break;
        case Rise:    rs = anchor - lenS; head = rs; lenCur = lenS; speed = 1.0; riseLp.fill (0.f); break;
        default: break;
    }
    toneLp.fill (0.f);
}

void ReelTransitProcessor::releaseEngine (bool spinUp)
{
    if (mode == Stop && spinUp)
    {
        mode = Spin;
        prog = 0.0;
        spinStartSpeed = speed;
        return;
    }
    if (mode != Spin)
        wetTarget = 0.f;
}

void ReelTransitProcessor::stepEngine (float& hl, float& hr, juce::int64 w)
{
    const double oldest = (double) (w - (bufSize - 8192));
    if (head < oldest) head = oldest;

    float gain = 1.f;
    const float* bl = buf[0].data();
    const float* br = buf[1].data();

    switch (mode)
    {
        case Stop:
        {
            const double t = prog / stopSamples;
            speed = t >= 1.0 ? 0.0 : std::pow (1.0 - t, curveExp);
            head += speed;
            prog += 1.0;
            gain = juce::jlimit (0.f, 1.f, (float) speed * 3.f);
            break;
        }
        case Spin:
        {
            const double t = juce::jmin (1.0, prog / spinSamples);
            speed = spinStartSpeed + (1.0 - spinStartSpeed) * std::pow (t, 1.6);
            head += speed;
            prog += 1.0;
            gain = juce::jlimit (0.f, 1.f, (float) speed * 3.f);
            if (t >= 1.0) wetTarget = 0.f;
            break;
        }
        case Reverse:
        {
            head -= 1.0;
            if (head <= rs) head += lenS;
            gain = fadeWin (revAnchor - head, lenS);
            speed = -1.0;
            break;
        }
        case Stutter:
        {
            head += stutterRatio;
            double d = head - rs;
            if (d >= lenCur)
            {
                head = rs + (d - lenCur);
                lenCur = juce::jmax (minLen, lenCur * rollFactor);
                d = head - rs;
                if (d >= lenCur) { head = rs; d = 0.0; }
            }
            gain = fadeWin (d, lenCur);
            speed = stutterRatio;
            break;
        }
        case Rise:
        {
            const double rp = juce::jmin (1.0, prog / riseSamples);
            prog += 1.0;
            speed = std::pow (2.0, riseRange * std::pow (rp, 1.5) / 12.0);
            lenCur = juce::jmax (minLen, lenS / (1.0 + 5.0 * rp * rp));
            head += speed;
            double d = head - rs;
            if (d >= lenCur) { head = rs + std::fmod (d, lenCur); d = head - rs; }
            gain = fadeWin (d, lenCur);
            break;
        }
        default: break;
    }

    hl = rt::hermite (bl, bufMask, head);
    hr = rt::hermite (br, bufMask, head);

    if (mode == Stop || mode == Spin)
    {
        // tape slowing down darkens the sound
        const float cutoff = 250.f + 15000.f * std::pow ((float) juce::jlimit (0.0, 1.0, speed), 1.5f);
        const float a = 1.f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sampleRate);
        toneLp[0] += a * (hl - toneLp[0]);  hl = toneLp[0];
        toneLp[1] += a * (hr - toneLp[1]);  hr = toneLp[1];
    }
    else if (mode == Rise)
    {
        const double rp = juce::jmin (1.0, prog / riseSamples);
        const float cutoff = 20.f + 2400.f * (float) (rp * rp);
        const float a = 1.f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sampleRate);
        riseLp[0] += a * (hl - riseLp[0]);  hl -= riseLp[0];
        riseLp[1] += a * (hr - riseLp[1]);  hr -= riseLp[1];
        gain *= 1.f + 0.35f * (float) rp;
    }

    hl *= gain;
    hr *= gain;
}

void ReelTransitProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    const float fs = (float) sampleRate;
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // ---- host info
    double bpm = 120.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
        }
    bpm = juce::jlimit (30.0, 400.0, bpm);
    hostPlaying.store (playing);
    const double beatSamples = 60.0 / bpm * sampleRate;

    // ---- parameters (block rate)
    reqLenS = (double) lenBeats[juce::jlimit (0, 5, (int) pLength->load())] * beatSamples;
    stopSamples  = juce::jmax (64.0, (double) pStopTime->load() * 0.001 * sampleRate);
    spinSamples  = juce::jmax (64.0, stopSamples * 0.45);
    riseSamples  = juce::jmax (64.0, (double) pRiseTime->load() * 0.001 * sampleRate);
    riseRange    = pRiseRange->load();
    curveExp     = 0.5 + 2.5 * (double) pStopCurve->load();
    stutterRatio = std::pow (2.0, (double) pPitch->load() / 12.0);
    rollFactor   = 1.0 - 0.5 * (double) pRoll->load();
    minLen       = juce::jmax (256.0, 0.004 * sampleRate);
    const bool spinUp = pSpin->load() > 0.5f;

    const float throwFb = pThrowFb->load();
    const double throwDelay = juce::jmin (5.0 * sampleRate,
        (double) throwBeats[juce::jlimit (0, 5, (int) pThrowDiv->load())] * beatSamples);
    const bool throwOn = pThrow->load() > 0.5f;

    driveSm.setTargetValue (pDrive->load());
    ageSm.setTargetValue   (pAge->load());
    outSm.setTargetValue   (juce::Decibels::decibelsToGain (pOutput->load()));

    // ---- trigger logic (which transition is active)
    {
        const std::array<bool, 4> b { pStop->load() > 0.5f, pReverse->load() > 0.5f,
                                      pStutter->load() > 0.5f, pRise->load() > 0.5f };
        static constexpr Mode modes[4] = { Stop, Reverse, Stutter, Rise };

        int newest = -1;
        for (int k = 0; k < 4; ++k)
            if (b[(size_t) k] && ! prevB[(size_t) k]) newest = k;

        if (newest >= 0)
        {
            engage (modes[newest]);
            activeIdx = newest;
        }
        else if (activeIdx >= 0 && ! b[(size_t) activeIdx])
        {
            int other = -1;
            for (int k = 0; k < 4; ++k)
                if (b[(size_t) k]) other = k;

            if (other >= 0) { engage (modes[other]); activeIdx = other; }
            else            { releaseEngine (spinUp); activeIdx = -1; }
        }
        prevB = b;
    }

    const float age = ageSm.getTargetValue();
    const float twoPi = juce::MathConstants<float>::twoPi;
    const juce::int64 wcStart = wc;
    float peakIn = 0.f, peakEcho = 0.f;

    // ============================================================ pass 1: engine + echo + wow/flutter
    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R[i];
        peakIn = juce::jmax (peakIn, std::abs (inL), std::abs (inR));

        buf[0][(size_t) (wc & bufMask)] = inL;
        buf[1][(size_t) (wc & bufMask)] = inR;

        wet += (wetTarget - wet) * (wetTarget > wet ? engCoef : relCoef);
        if (mode != Idle && wetTarget == 0.f && wet < 0.0005f) { mode = Idle; wet = 0.f; }

        float oL = inL, oR = inR;
        if (mode != Idle)
        {
            float hl = 0.f, hr = 0.f;
            stepEngine (hl, hr, wc);
            const float gw = std::sin (wet * juce::MathConstants<float>::halfPi);
            const float gd = std::cos (wet * juce::MathConstants<float>::halfPi);
            oL = inL * gd + hl * gw;
            oR = inR * gd + hr * gw;
        }

        // ---- echo throw
        {
            sendG += ((throwOn ? 1.f : 0.f) - sendG) * sendCoef;
            echoDelaySm += ((float) throwDelay - echoDelaySm) * echoDelayCoef;
            if (echoDelaySm < 8.f) echoDelaySm = 8.f;

            echoPhase += 0.7f / fs;
            if (echoPhase >= 1.f) echoPhase -= 1.f;
            const float d = echoDelaySm + std::sin (twoPi * echoPhase) * (0.0007f * fs) * (0.3f + age);

            const float eL = rt::hermite (echoBuf[0].data(), echoMask, (double) ewc - (double) d);
            const float eR = rt::hermite (echoBuf[1].data(), echoMask, (double) ewc - (double) d);

            echoLp[0] += echoLpA * (eR - echoLp[0]);       // ping-pong cross feedback, darker each pass
            echoLp[1] += echoLpA * (eL - echoLp[1]);
            const float fL = std::tanh (echoLp[0] * 1.3f) / 1.3f;
            const float fR = std::tanh (echoLp[1] * 1.3f) / 1.3f;

            echoBuf[0][(size_t) (ewc & echoMask)] = sendG * oL * 0.8f + throwFb * fL;
            echoBuf[1][(size_t) (ewc & echoMask)] = sendG * oR * 0.8f + throwFb * fR;
            ++ewc;

            oL += eL * 0.9f;
            oR += eR * 0.9f;
            peakEcho = juce::jmax (peakEcho, std::abs (eL), std::abs (eR));
        }

        // ---- wow & flutter (small modulated delay)
        {
            wowPhase += 0.55f / fs;   if (wowPhase >= 1.f)  wowPhase -= 1.f;
            flutPhase += 8.7f / fs;   if (flutPhase >= 1.f) flutPhase -= 1.f;
            flutRand += 0.0015f * ((rng.nextFloat() * 2.f - 1.f) - flutRand);

            const float a = ageSm.getNextValue();
            const float mod = a * (std::sin (twoPi * wowPhase) * 0.0035f * fs
                                   + (0.5f * std::sin (twoPi * flutPhase) + 6.f * flutRand) * 0.0004f * fs);
            const double rd = (double) wfc - (double) (wfBase + mod);

            wfBuf[0][(size_t) (wfc & wfMask)] = oL;
            wfBuf[1][(size_t) (wfc & wfMask)] = oR;
            ++wfc;

            L[i] = rt::hermite (wfBuf[0].data(), wfMask, rd);
            R[i] = rt::hermite (wfBuf[1].data(), wfMask, rd);
        }

        ++wc;
    }

    // ============================================================ pass 2: oversampled tape saturation + head bump
    for (int start = 0; start < n; start += maxBlock)
    {
        const int cnt = juce::jmin (maxBlock, n - start);
        float* ch[2] = { L + start, R + start };
        juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) cnt);

        auto up = oversampling.processSamplesUp (blk);
        const size_t upN = up.getNumSamples();
        float* u0 = up.getChannelPointer (0);
        float* u1 = up.getChannelPointer (1);

        for (size_t j = 0; j < upN; ++j)
        {
            const float dr = driveSm.getNextValue();
            const float g = 1.f + 9.f * std::pow (dr, 1.4f);
            const float amt = juce::jmin (1.f, dr * 5.f);
            const float bias = 0.08f * dr;
            const float bOff = std::tanh (g * bias);

            const float x0 = u0[j], x1 = u1[j];
            const float s0 = (std::tanh (g * (x0 + bias)) - bOff) / g;
            const float s1 = (std::tanh (g * (x1 + bias)) - bOff) / g;
            u0[j] = x0 + amt * (s0 - x0);
            u1[j] = x1 + amt * (s1 - x1);
        }
        oversampling.processSamplesDown (blk);

        juce::dsp::ProcessContextReplacing<float> ctx (blk);
        headBump.process (ctx);
    }

    // ============================================================ pass 3: age (hf loss, hiss, dropouts, ghosts) + output
    float peakOut = 0.f;
    const float cutoff = juce::jmap (age, 0.f, 1.f, 19000.f, 4800.f);
    const float hfA = 1.f - std::exp (-twoPi * juce::jmin (cutoff, 0.45f * fs) / fs);
    const float noiseA = 1.f - std::exp (-twoPi * 6500.f / fs);
    const int ghostDelay = (int) (0.47 * sampleRate);
    const float ghostAmt = age * 0.03f;
    const float dropProb = age * age * 0.35f / fs;

    for (int i = 0; i < n; ++i)
    {
        float x[2] = { L[i], R[i] };

        // dropouts: brief level and brightness dips
        if (dropRemain > 0) --dropRemain;
        else if (rng.nextFloat() < dropProb)
        {
            dropRemain = (int) ((0.015f + 0.075f * rng.nextFloat()) * fs);
            dropTarget = 0.15f + 0.5f * rng.nextFloat();
        }
        if (dropRemain == 0) dropTarget = 1.f;
        dropEnv += (dropTarget - dropEnv) * (1.f - std::exp (-1.f / (0.003f * fs)));

        const juce::int64 gw = wcStart + i - ghostDelay;

        for (int c = 0; c < 2; ++c)
        {
            float v = x[c];

            // print-through ghost of the raw input
            if (gw > 0 && ghostAmt > 0.f)
                v += ghostAmt * buf[(size_t) c][(size_t) (gw & bufMask)];

            // dc blocker
            const float y = v - dcX[(size_t) c] + 0.9995f * dcY[(size_t) c];
            dcX[(size_t) c] = v;  dcY[(size_t) c] = y;
            v = y;

            // hf loss, two poles
            hf1[(size_t) c] += hfA * (v - hf1[(size_t) c]);
            hf2[(size_t) c] += hfA * (hf1[(size_t) c] - hf2[(size_t) c]);
            v = hf2[(size_t) c];

            // dropout dips darken as well as attenuate
            v *= dropEnv;

            // hiss
            noiseLp[(size_t) c] += noiseA * ((rng.nextFloat() * 2.f - 1.f) - noiseLp[(size_t) c]);
            v += noiseLp[(size_t) c] * age * 0.0035f;

            x[c] = v;
        }

        const float og = outSm.getNextValue();
        L[i] = x[0] * og;
        R[i] = x[1] * og;
        peakOut = juce::jmax (peakOut, std::abs (L[i]), std::abs (R[i]));
    }

    // ---- editor feedback
    inLevel.store (peakIn);
    outLevel.store (peakOut);
    echoLevel.store (peakEcho);
    modeNow.store ((int) mode);
    headSpeed.store (mode == Idle ? 1.f : 1.f + wet * ((float) speed - 1.f));
}

juce::AudioProcessorEditor* ReelTransitProcessor::createEditor()
{
    return new ReelTransitEditor (*this);
}

void ReelTransitProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void ReelTransitProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReelTransitProcessor();
}
