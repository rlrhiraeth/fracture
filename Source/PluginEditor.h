#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class StepGrid : public juce::Component
{
public:
    explicit StepGrid (FractureProcessor& p) : proc (p) {}
    int selLane = 0, selStep = 0;
    std::function<void()> onSelect;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    static juce::Colour laneColour (int lane);

private:
    bool cellAt (juce::Point<int> p, int& lane, int& step) const;
    FractureProcessor& proc;
    bool paintVal = true;
    int lastLane = -1, lastStep = -1;
};

class MidiDragButton : public juce::Component
{
public:
    explicit MidiDragButton (FractureProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override;
    void mouseDrag (const juce::MouseEvent& e) override;
private:
    FractureProcessor& proc;
};

class FractureEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FractureEditor (FractureProcessor&);
    ~FractureEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Item { juce::Component* comp; juce::Label* label; int row; bool small; };

    void timerCallback() override;
    void addKnob (const juce::String& id, const juce::String& name, int row);
    void addCombo (const juce::String& id, const juce::String& name, int row);
    void addToggle (const juce::String& id, const juce::String& name, int row);
    void setupMomentary (juce::TextButton& b, const char* paramId);
    void refreshStepPanel();
    void exportMidi();

    FractureProcessor& proc;
    juce::LookAndFeel_V4 laf;
    StepGrid grid;
    MidiDragButton dragBtn;

    // top bar
    juce::ComboBox styleBox;
    std::unique_ptr<ComboAtt> styleAtt;
    juce::TextButton regenBtn { "Regenerate" }, clearBtn { "Clear" }, fillBtn { "Fill" }, rollBtn { "Roll" }, exportBtn { "Export MIDI" };

    // lane rows
    struct LaneRow
    {
        juce::Label name;
        juce::TextButton load { "Load" }, clear { "x" };
        juce::ToggleButton mute { "M" };
        juce::Slider level;
        std::unique_ptr<SliderAtt> levelAtt;
        std::unique_ptr<ButtonAtt> muteAtt;
    };
    std::array<LaneRow, fracture::kLanes> laneRows;

    // step panel
    juce::Label stepTitle;
    juce::Slider velS, pitchS, filtS, retS, sliceS;
    juce::Label velL, pitchL, filtL, retL, sliceL;
    juce::ToggleButton lockBtn { "Lock step" };

    // knob grid
    struct Knob { juce::Slider s; juce::Label l; std::unique_ptr<SliderAtt> a; };
    struct Combo { juce::ComboBox c; juce::Label l; std::unique_ptr<ComboAtt> a; };
    struct Toggle { juce::ToggleButton b; juce::Label l; std::unique_ptr<ButtonAtt> a; };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<Item> items;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractureEditor)
};
