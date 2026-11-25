#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include "Sequencer.h"
#include "DrumEngine.h"
#include "Effects.h"

class FractureProcessor : public juce::AudioProcessor
{
public:
    FractureProcessor();
    ~FractureProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Fracture"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- pattern API (message thread) ----
    void regenerate (bool newSeed);
    void clearUnlocked();
    void copyPattern (fracture::Pattern& out) const;
    void modifyStep (int lane, int step, const std::function<void (fracture::Step&)>& fn);
    void loadSample (int lane, const juce::File& f);
    void clearSample (int lane);
    juce::MidiFile buildMidiFile (int repeats);
    int getCurrentStep() const { return currentStep.load(); }
    fracture::GenParams currentGenParams() const;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    struct Event { int64_t time; int lane; float vel; int pitch; float filter; int slice; };
    struct Cur { int steps = 16, slices = 16; float swing = 0, evolve = 0, rateMul = 1; bool fill = false, roll = false; } cur;

    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void pushEvent (const Event& e) { if (numEvents < (int) events.size()) events[(size_t) numEvents++] = e; }
    void scheduleStep (int64_t k, int offset);
    juce::String serialisePattern() const;
    void deserialisePattern (const juce::String&);

    mutable juce::SpinLock patternLock;
    fracture::Pattern pattern;
    std::mt19937 rng { std::random_device{}() };

    std::array<fracture::Lane, fracture::kLanes> lanes;
    std::shared_ptr<fracture::Sample> defaultBreak;
    juce::String samplePaths[fracture::kLanes];
    std::array<std::atomic<float>*, fracture::kLanes> laneLevel {}, laneMute {}, lanePitch {};

    std::array<Event, 2048> events;
    int numEvents = 0;
    int64_t sampleCounter = 0, lastTriggered = -1;
    double currentSampleRate = 44100.0, stepSamples = 5000.0, internalPpq = 0.0, expectedS = 0.0;
    bool wasPlaying = false;
    int rollCount = 0;
    std::atomic<int> currentStep { -1 };
    std::atomic<float> lastBpm { 120.f };

    std::vector<float> mixL, mixR;
    fracture::Svf svfL, svfR;
    fracture::StereoDelay delay;
    juce::SmoothedValue<float> cutoffSm;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractureProcessor)
};
