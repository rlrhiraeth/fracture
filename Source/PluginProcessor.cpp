#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace fracture;

static int slicesFromChoice (int idx) { return 4 << juce::jlimit (0, 2, idx); }

juce::AudioProcessorValueTreeState::ParameterLayout FractureProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pid = [] (const String& s) { return ParameterID { s, 1 }; };
    auto unit = [] (float def) { return NormalisableRange<float> (0.f, 1.f); (void) def; };
    (void) unit;

    layout.add (std::make_unique<AudioParameterChoice> (pid ("style"), "Style", StringArray { "Jungle", "Footwork", "Hybrid", "Breakcore" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("density"), "Density", NormalisableRange<float> (0.f, 1.f), 0.5f));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("sync"), "Syncopation", NormalisableRange<float> (0.f, 1.f), 0.4f));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("chaos"), "Chaos", NormalisableRange<float> (0.f, 1.f), 0.25f));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("evolve"), "Evolve", NormalisableRange<float> (0.f, 1.f), 0.3f));
    layout.add (std::make_unique<AudioParameterInt>   (pid ("steps"), "Steps", 4, 32, 16));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("swing"), "Swing", NormalisableRange<float> (0.f, 1.f), 0.f));
    layout.add (std::make_unique<AudioParameterChoice> (pid ("slices"), "Slices", StringArray { "4", "8", "16" }, 2));
    layout.add (std::make_unique<AudioParameterBool>  (pid ("fill"), "Fill", false));
    layout.add (std::make_unique<AudioParameterBool>  (pid ("roll"), "Snare Roll", false));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("breakBpm"), "Break BPM", NormalisableRange<float> (60.f, 220.f, 0.1f), 170.f));
    layout.add (std::make_unique<AudioParameterBool>  (pid ("breakSync"), "Break Sync", true));

    for (int i = 0; i < kLanes; ++i)
    {
        const String id = "lane" + String (i) + "_", nm = String (kLaneNames[i]) + " ";
        layout.add (std::make_unique<AudioParameterFloat> (pid (id + "level"), nm + "Level", NormalisableRange<float> (0.f, 1.25f), i == kBreakLane ? 0.7f : 0.85f));
        layout.add (std::make_unique<AudioParameterBool>  (pid (id + "mute"), nm + "Mute", false));
        layout.add (std::make_unique<AudioParameterInt>   (pid (id + "pitch"), nm + "Pitch", -12, 12, 0));
    }

    layout.add (std::make_unique<AudioParameterFloat> (pid ("drive"), "Drive", NormalisableRange<float> (0.f, 1.f), 0.15f));
    layout.add (std::make_unique<AudioParameterChoice> (pid ("fltMode"), "Filter Mode", StringArray { "Low-pass", "High-pass", "Band-pass" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("cutoff"), "Cutoff", NormalisableRange<float> (20.f, 20000.f, 0.f, 0.3f), 20000.f));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("res"), "Resonance", NormalisableRange<float> (0.f, 1.f), 0.2f));
    layout.add (std::make_unique<AudioParameterChoice> (pid ("dlyTime"), "Delay Time", StringArray { "1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("dlyFb"), "Feedback", NormalisableRange<float> (0.f, 0.92f), 0.4f));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("dlyMix"), "Delay Mix", NormalisableRange<float> (0.f, 1.f), 0.f));
    layout.add (std::make_unique<AudioParameterFloat> (pid ("master"), "Master", NormalisableRange<float> (0.f, 1.5f), 0.9f));
    return layout;
}

FractureProcessor::FractureProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int i = 0; i < kLanes; ++i)
    {
        lanes[(size_t) i].index = i;
        const juce::String id = "lane" + juce::String (i) + "_";
        laneLevel[(size_t) i] = apvts.getRawParameterValue (id + "level");
        laneMute[(size_t) i]  = apvts.getRawParameterValue (id + "mute");
        lanePitch[(size_t) i] = apvts.getRawParameterValue (id + "pitch");
    }
    mixL.assign (4096, 0.f);
    mixR.assign (4096, 0.f);
    regenerate (true);
}

bool FractureProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void FractureProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    currentSampleRate = sr;
    mixL.assign ((size_t) std::max (samplesPerBlock, 4096), 0.f);
    mixR.assign ((size_t) std::max (samplesPerBlock, 4096), 0.f);
    delay.prepare (sr);
    svfL.reset(); svfR.reset();
    cutoffSm.reset (sr, 0.02);
    cutoffSm.setCurrentAndTargetValue (pv ("cutoff"));
    defaultBreak = makeDefaultBreak (sr);
    if (samplePaths[kBreakLane].isEmpty()) lanes[kBreakLane].active = defaultBreak;
    numEvents = 0;
    lastTriggered = -1;
    wasPlaying = false;
}

GenParams FractureProcessor::currentGenParams() const
{
    GenParams g;
    g.style = (Style) (int) pv ("style");
    g.density = pv ("density");
    g.syncopation = pv ("sync");
    g.chaos = pv ("chaos");
    g.length = (int) pv ("steps");
    g.slices = slicesFromChoice ((int) pv ("slices"));
    return g;
}

// ---------------------------------------------------------------- pattern editing

void FractureProcessor::regenerate (bool newSeed)
{
    (void) newSeed;   // each call draws a fresh seed from the RNG; locked steps are preserved
    Pattern tmp;
    copyPattern (tmp);
    Rng r ((unsigned) juce::Random::getSystemRandom().nextInt());
    generate (tmp, currentGenParams(), r);
    juce::SpinLock::ScopedLockType sl (patternLock);
    pattern = tmp;
}

void FractureProcessor::clearUnlocked()
{
    juce::SpinLock::ScopedLockType sl (patternLock);
    for (auto& lane : pattern.s)
        for (auto& st : lane)
            if (! st.locked) st = Step {};
}

void FractureProcessor::copyPattern (Pattern& out) const
{
    juce::SpinLock::ScopedLockType sl (patternLock);
    out = pattern;
}

void FractureProcessor::modifyStep (int lane, int step, const std::function<void (Step&)>& fn)
{
    if (lane < 0 || lane >= kLanes || step < 0 || step >= kMaxSteps) return;
    juce::SpinLock::ScopedLockType sl (patternLock);
    fn (pattern.at (lane, step));
}

// ---------------------------------------------------------------- samples

void FractureProcessor::loadSample (int lane, const juce::File& f)
{
    if (lane < 0 || lane >= kLanes) return;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) return;

    const int len = (int) std::min<int64_t> (reader->lengthInSamples, (int64_t) (reader->sampleRate * 30.0));
    if (len < 64) return;

    auto s = std::make_shared<Sample>();
    s->sampleRate = reader->sampleRate;
    s->data.setSize (juce::jmin (2, (int) reader->numChannels), len);
    reader->read (&s->data, 0, len, 0, true, true);
    const float peak = s->data.getMagnitude (0, len);
    if (peak > 0.0001f) s->data.applyGain (0.9f / peak);
    s->analyse();

    samplePaths[lane] = f.getFullPathName();
    lanes[(size_t) lane].setPending (s);
}

void FractureProcessor::clearSample (int lane)
{
    if (lane < 0 || lane >= kLanes) return;
    samplePaths[lane] = {};
    lanes[(size_t) lane].setPending (lane == kBreakLane ? defaultBreak : nullptr);
}

// ---------------------------------------------------------------- scheduling

void FractureProcessor::scheduleStep (int64_t k, int offset)
{
    const int L = cur.steps;
    const int si = (int) (((k % L) + L) % L);
    std::array<Step, kLanes> st;
    {
        juce::SpinLock::ScopedLockType sl (patternLock);
        if (si == 0 && cur.evolve > 0.01f)
            mutate (pattern, currentGenParams(), cur.evolve, rng);
        for (int l = 0; l < kLanes; ++l) st[(size_t) l] = pattern.at (l, si);
    }

    // performance controls
    if (cur.fill && si >= L - 4)
    {
        const int j = si - (L - 4);
        Step& s = st[1];
        s.on = true; s.velocity = 0.55f + 0.12f * (float) j; s.retrig = std::min (8, 2 + j); s.filter = 1.f; s.pitch = 0;
    }
    if (cur.roll)
    {
        Step& s = st[1];
        s.on = true; s.velocity = std::min (1.f, 0.45f + 0.07f * (float) rollCount);
        s.retrig = rollCount < 4 ? 2 : (rollCount < 8 ? 3 : 4); s.filter = 1.f; s.pitch = 0;
        ++rollCount;
    }
    else rollCount = 0;

    const double swingOff = (si % 2 == 1) ? (double) cur.swing * 0.5 * stepSamples : 0.0;
    const int64_t blockStart = sampleCounter;

    for (int l = 0; l < kLanes; ++l)
    {
        const Step& s = st[(size_t) l];
        if (! s.on) continue;
        int slice = -1;
        if (l == kBreakLane)
            slice = std::clamp (s.slice >= 0 ? s.slice : (si % 16) * cur.slices / 16, 0, cur.slices - 1);

        const int rt = std::clamp (s.retrig, 1, 8);
        for (int r = 0; r < rt; ++r)
        {
            const double t = swingOff + (double) r * stepSamples / (double) rt;
            pushEvent ({ blockStart + offset + (int64_t) t, l, s.velocity * (1.f - 0.05f * (float) r), s.pitch, s.filter, slice });
        }
    }
}

// ---------------------------------------------------------------- audio

void FractureProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n > (int) mixL.size()) { mixL.resize ((size_t) n); mixR.resize ((size_t) n); }

    // ---- transport
    double bpm = 120.0, ppq = 0.0;
    bool playing = true, hasPpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) { ppq = *p; hasPpq = true; }
        }
    if (bpm < 20.0) bpm = 120.0;
    if (! hasPpq) ppq = internalPpq;   // standalone / hosts without position info: free-run
    lastBpm.store ((float) bpm);

    const double sr = currentSampleRate;
    const double stepsPerSample = bpm / 60.0 * 4.0 / sr;
    stepSamples = 1.0 / stepsPerSample;

    // ---- parameters
    cur.steps = juce::jlimit (4, kMaxSteps, (int) pv ("steps"));
    cur.slices = slicesFromChoice ((int) pv ("slices"));
    cur.swing = pv ("swing");
    cur.evolve = pv ("evolve");
    cur.fill = pv ("fill") > 0.5f;
    cur.roll = pv ("roll") > 0.5f;
    cur.rateMul = pv ("breakSync") > 0.5f ? (float) (bpm / (double) pv ("breakBpm")) : 1.f;

    float laneGain[kLanes]; int lanePitchV[kLanes];
    for (int l = 0; l < kLanes; ++l)
    {
        laneGain[l] = laneMute[(size_t) l]->load() > 0.5f ? 0.f : laneLevel[(size_t) l]->load();
        lanePitchV[l] = (int) lanePitch[(size_t) l]->load();
        lanes[(size_t) l].pollPending();
    }

    const int64_t blockStart = sampleCounter;

    // ---- MIDI input: C1..E1 (36-40) = lanes, C2.. (48-63) = break slices
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (! msg.isNoteOn()) continue;
        const int note = msg.getNoteNumber();
        int lane = -1, slice = -1;
        if (note >= 36 && note <= 40) lane = note - 36;
        else if (note >= 48 && note < 64) { lane = kBreakLane; slice = std::min (note - 48, cur.slices - 1); }
        if (lane >= 0) pushEvent ({ blockStart + m.samplePosition, lane, msg.getFloatVelocity(), 0, 1.f, slice });
    }
    midi.clear();

    // ---- step clock
    if (playing)
    {
        const double s0 = ppq * 4.0;
        int64_t k = (int64_t) std::ceil (s0 - 1e-6);
        if (! wasPlaying || std::abs (s0 - expectedS) > 0.5) lastTriggered = k - 1;
        for (;; ++k)
        {
            const double off = ((double) k - s0) / stepsPerSample;
            if (off >= (double) n) break;
            if (k > lastTriggered) { scheduleStep (k, std::max (0, (int) off)); lastTriggered = k; }
        }
        expectedS = s0 + (double) n * stepsPerSample;
        const int L = cur.steps;
        currentStep.store ((int) (((int64_t) std::floor (s0) % L + L) % L));
    }
    else currentStep.store (-1);
    wasPlaying = playing;
    if (! hasPpq && playing) internalPpq += (double) n * bpm / 60.0 / sr;

    // ---- render lanes, splitting the block at each event
    std::sort (events.begin(), events.begin() + numEvents, [] (const Event& a, const Event& b) { return a.time < b.time; });
    int idx = 0, pos = 0;
    while (pos < n)
    {
        while (idx < numEvents && events[(size_t) idx].time - blockStart <= pos)
        {
            const Event& e = events[(size_t) idx++];
            lanes[(size_t) e.lane].trigger (e.vel, e.pitch + lanePitchV[e.lane], e.filter, e.slice, cur.slices, sr,
                                            e.lane == kBreakLane ? cur.rateMul : 1.f);
        }
        int next = n;
        if (idx < numEvents) next = std::min (n, (int) (events[(size_t) idx].time - blockStart));
        for (int s = pos; s < next; ++s)
        {
            float L = 0.f, R = 0.f;
            for (int l = 0; l < kLanes; ++l)
            {
                float x, y;
                lanes[(size_t) l].next (x, y);
                L += x * laneGain[l]; R += y * laneGain[l];
            }
            mixL[(size_t) s] = L; mixR[(size_t) s] = R;
        }
        pos = next;
    }
    const int remaining = numEvents - idx;
    for (int i = 0; i < remaining; ++i) events[(size_t) i] = events[(size_t) (idx + i)];
    numEvents = remaining;

    // ---- master FX: saturation -> filter -> ping-pong delay -> master
    const float drive = pv ("drive");
    const float g = 1.f + drive * 14.f, makeup = 1.f / std::pow (g, 0.45f);
    const int mode = (int) pv ("fltMode");
    const float q = 0.55f + std::pow (pv ("res"), 2.f) * 12.f;
    cutoffSm.setTargetValue (pv ("cutoff"));
    static const float beats[] = { 0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f };
    const float dlySamples = (float) std::clamp (beats[juce::jlimit (0, 5, (int) pv ("dlyTime"))] * 60.0 / bpm * sr, 10.0, sr * 3.9);
    const float fb = pv ("dlyFb"), dmix = pv ("dlyMix"), master = pv ("master");

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int s = 0; s < n; ++s)
    {
        const float fc = cutoffSm.getNextValue();
        svfL.set (fc, q, sr); svfR.a1 = svfL.a1; svfR.a2 = svfL.a2; svfR.a3 = svfL.a3; svfR.k = svfL.k;
        float l = saturate (mixL[(size_t) s], g, makeup);
        float r = saturate (mixR[(size_t) s], g, makeup);
        l = svfL.process (l, mode);
        r = svfR.process (r, mode);
        delay.process (l, r, dlySamples, fb, dmix, 0.45f);
        l = std::tanh (l * master);
        r = std::tanh (r * master);
        if (outR != nullptr) { outL[s] = l; outR[s] = r; }
        else outL[s] = 0.5f * (l + r);
    }

    sampleCounter += n;
}

// ---------------------------------------------------------------- MIDI export

juce::MidiFile FractureProcessor::buildMidiFile (int repeats)
{
    Pattern p;
    copyPattern (p);
    const GenParams g = currentGenParams();
    std::mt19937 r { std::random_device{}() };
    const float evo = pv ("evolve");
    const int L = juce::jlimit (4, kMaxSteps, (int) pv ("steps"));
    const int sl = slicesFromChoice ((int) pv ("slices"));
    const short tpq = 960;
    const int stepTicks = tpq / 4;

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / (double) lastBpm.load())), 0);

    for (int rep = 0; rep < repeats; ++rep)
    {
        for (int i = 0; i < L; ++i)
            for (int lane = 0; lane < kLanes; ++lane)
            {
                const Step& st = p.at (lane, i);
                if (! st.on) continue;
                const int note = lane == kBreakLane
                    ? 48 + std::clamp (st.slice >= 0 ? st.slice : (i % 16) * sl / 16, 0, 15)
                    : 36 + lane;
                const int vel = std::clamp ((int) (st.velocity * 127.f), 1, 127);
                const int rt = std::clamp (st.retrig, 1, 8);
                for (int k = 0; k < rt; ++k)
                {
                    const double tick = (double) ((rep * L + i) * stepTicks + k * stepTicks / rt);
                    const double len = std::max (10.0, (double) stepTicks / rt * 0.8);
                    seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), tick);
                    seq.addEvent (juce::MidiMessage::noteOff (1, note), tick + len);
                }
            }
        mutate (p, g, evo, r);   // export follows the same evolution the plugin performs live
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    return mf;
}

// ---------------------------------------------------------------- state

juce::String FractureProcessor::serialisePattern() const
{
    Pattern p; copyPattern (p);
    juce::StringArray out;
    for (auto& lane : p.s)
        for (auto& s : lane)
            out.add (juce::String (s.on ? 1 : 0) + "," + juce::String (s.locked ? 1 : 0) + "," + juce::String (juce::roundToInt (s.velocity * 1000.f))
                     + "," + juce::String (s.pitch) + "," + juce::String (juce::roundToInt (s.filter * 1000.f)) + ","
                     + juce::String (s.retrig) + "," + juce::String (s.slice));
    return out.joinIntoString (";");
}

void FractureProcessor::deserialisePattern (const juce::String& str)
{
    auto steps = juce::StringArray::fromTokens (str, ";", "");
    if (steps.size() != kLanes * kMaxSteps) return;
    Pattern p;
    int n = 0;
    for (auto& lane : p.s)
        for (auto& s : lane)
        {
            auto f = juce::StringArray::fromTokens (steps[n++], ",", "");
            if (f.size() != 7) continue;
            s.on = f[0].getIntValue() != 0; s.locked = f[1].getIntValue() != 0;
            s.velocity = f[2].getIntValue() / 1000.f; s.pitch = f[3].getIntValue();
            s.filter = f[4].getIntValue() / 1000.f; s.retrig = std::clamp (f[5].getIntValue(), 1, 8);
            s.slice = f[6].getIntValue();
        }
    juce::SpinLock::ScopedLockType sl (patternLock);
    pattern = p;
}

void FractureProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("pattern", serialisePattern(), nullptr);
    for (int i = 0; i < kLanes; ++i) state.setProperty ("sample" + juce::String (i), samplePaths[i], nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void FractureProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto vt = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (vt);
    deserialisePattern (vt.getProperty ("pattern").toString());
    for (int i = 0; i < kLanes; ++i)
    {
        const juce::File f (vt.getProperty ("sample" + juce::String (i)).toString());
        if (f.existsAsFile()) loadSample (i, f);
    }
}

juce::AudioProcessorEditor* FractureProcessor::createEditor() { return new FractureEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FractureProcessor(); }
