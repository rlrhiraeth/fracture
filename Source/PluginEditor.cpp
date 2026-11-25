#include "PluginEditor.h"

using namespace fracture;

// ============================================================ StepGrid

juce::Colour StepGrid::laneColour (int lane)
{
    static const juce::uint32 c[kLanes] = { 0xffff5a36, 0xffffb02e, 0xff3ddc97, 0xffb36bff, 0xff4cc3ff, 0xffff4fa3 };
    return juce::Colour (c[juce::jlimit (0, kLanes - 1, lane)]);
}

void StepGrid::paint (juce::Graphics& g)
{
    Pattern p;
    proc.copyPattern (p);
    const int L = (int) proc.apvts.getRawParameterValue ("steps")->load();
    const float cw = (float) getWidth() / (float) L, rh = (float) getHeight() / (float) kLanes;
    const int cur = proc.getCurrentStep();

    g.fillAll (juce::Colour (0xff14161b));
    for (int lane = 0; lane < kLanes; ++lane)
        for (int i = 0; i < L; ++i)
        {
            const auto r = juce::Rectangle<float> ((float) i * cw, (float) lane * rh, cw, rh).reduced (1.5f);
            const Step& st = p.at (lane, i);
            juce::Colour c = ((i / 4) % 2) ? juce::Colour (0xff23262d) : juce::Colour (0xff1c1f26);
            if (st.on) c = laneColour (lane).withAlpha (0.35f + 0.65f * st.velocity);
            if (i == cur) c = c.brighter (0.3f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 3.f);

            if (st.on && (st.retrig > 1 || st.slice >= 0))
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.setFont (10.f);
                juce::String t;
                if (st.retrig > 1) t << st.retrig << "x";
                if (st.slice >= 0) t << (t.isEmpty() ? "" : " ") << "s" << st.slice;
                g.drawText (t, r, juce::Justification::centred, false);
            }
            if (st.locked) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r, 3.f, 1.5f); }
            if (lane == selLane && i == selStep)
            {
                g.setColour (juce::Colours::yellow.withAlpha (0.9f));
                g.drawRoundedRectangle (r.expanded (1.f), 3.f, 2.f);
            }
        }
}

bool StepGrid::cellAt (juce::Point<int> p, int& lane, int& step) const
{
    const int L = (int) proc.apvts.getRawParameterValue ("steps")->load();
    if (! getLocalBounds().contains (p)) return false;
    step = juce::jlimit (0, L - 1, (int) ((float) p.x / ((float) getWidth() / (float) L)));
    lane = juce::jlimit (0, kLanes - 1, (int) ((float) p.y / ((float) getHeight() / (float) kLanes)));
    return true;
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int lane, step;
    if (! cellAt (e.getPosition(), lane, step)) return;
    selLane = lane; selStep = step; lastLane = lane; lastStep = step;

    if (e.mods.isRightButtonDown() || e.mods.isShiftDown())
    {
        proc.modifyStep (lane, step, [] (Step& s) { s.locked = ! s.locked; });
    }
    else
    {
        Pattern p; proc.copyPattern (p);
        paintVal = ! p.at (lane, step).on;
        const bool v = paintVal;
        proc.modifyStep (lane, step, [v] (Step& s) { s.on = v; });
    }
    if (onSelect) onSelect();
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown() || e.mods.isShiftDown()) return;
    int lane, step;
    if (! cellAt (e.getPosition(), lane, step)) return;
    if (lane == lastLane && step == lastStep) return;
    lastLane = lane; lastStep = step;
    const bool v = paintVal;
    proc.modifyStep (lane, step, [v] (Step& s) { s.on = v; });
    repaint();
}

// ============================================================ MIDI drag

void MidiDragButton::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff3a3f4b));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.f);
    g.setColour (juce::Colours::white);
    g.setFont (13.f);
    g.drawText ("Drag MIDI", getLocalBounds(), juce::Justification::centred);
}

void MidiDragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 6) return;
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Fracture_pattern.mid");
    f.deleteFile();
    {
        juce::FileOutputStream os (f);
        if (! os.openedOk()) return;
        proc.buildMidiFile (8).writeTo (os);
    }
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, nullptr);
}

// ============================================================ Editor

FractureEditor::FractureEditor (FractureProcessor& p)
    : AudioProcessorEditor (&p), proc (p), grid (p), dragBtn (p)
{
    using namespace juce;
    laf.setColour (Slider::rotarySliderFillColourId, Colour (0xffff5a36));
    laf.setColour (Slider::thumbColourId, Colours::white);
    laf.setColour (Slider::trackColourId, Colour (0xffff5a36));
    laf.setColour (ComboBox::backgroundColourId, Colour (0xff23262d));
    laf.setColour (TextButton::buttonColourId, Colour (0xff3a3f4b));
    setLookAndFeel (&laf);

    addAndMakeVisible (grid);
    addAndMakeVisible (dragBtn);
    grid.onSelect = [this] { refreshStepPanel(); };

    // top bar
    styleBox.addItemList ({ "Jungle", "Footwork", "Hybrid", "Breakcore" }, 1);
    styleAtt = std::make_unique<ComboAtt> (proc.apvts, "style", styleBox);
    for (auto* c : { (Component*) &styleBox, (Component*) &regenBtn, (Component*) &clearBtn, (Component*) &fillBtn,
                     (Component*) &rollBtn, (Component*) &exportBtn })
        addAndMakeVisible (c);
    regenBtn.onClick = [this] { proc.regenerate (true); refreshStepPanel(); };
    clearBtn.onClick = [this] { proc.clearUnlocked(); refreshStepPanel(); };
    exportBtn.onClick = [this] { exportMidi(); };
    setupMomentary (fillBtn, "fill");
    setupMomentary (rollBtn, "roll");

    // lane rows
    for (int i = 0; i < kLanes; ++i)
    {
        auto& r = laneRows[(size_t) i];
        r.name.setText (kLaneNames[i], dontSendNotification);
        r.name.setColour (Label::textColourId, StepGrid::laneColour (i));
        r.level.setSliderStyle (Slider::LinearHorizontal);
        r.level.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        const String id = "lane" + String (i) + "_";
        r.levelAtt = std::make_unique<SliderAtt> (proc.apvts, id + "level", r.level);
        r.muteAtt = std::make_unique<ButtonAtt> (proc.apvts, id + "mute", r.mute);
        r.load.onClick = [this, i]
        {
            chooser = std::make_unique<FileChooser> ("Load sample", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [this, i] (const FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) proc.loadSample (i, f); });
        };
        r.clear.onClick = [this, i] { proc.clearSample (i); };
        for (auto* c : { (Component*) &r.name, (Component*) &r.load, (Component*) &r.clear, (Component*) &r.mute, (Component*) &r.level })
            addAndMakeVisible (c);
    }

    // step panel
    addAndMakeVisible (stepTitle);
    stepTitle.setFont (FontOptions (14.f, Font::bold));
    auto setupS = [this] (Slider& s, Label& l, const char* name, double lo, double hi, double iv)
    {
        s.setSliderStyle (Slider::LinearHorizontal);
        s.setTextBoxStyle (Slider::TextBoxRight, false, 44, 18);
        s.setRange (lo, hi, iv);
        l.setText (name, dontSendNotification);
        addAndMakeVisible (s); addAndMakeVisible (l);
    };
    setupS (velS, velL, "Velocity", 0, 1, 0.01);
    setupS (pitchS, pitchL, "Pitch (st)", -24, 24, 1);
    setupS (filtS, filtL, "Filter", 0, 1, 0.01);
    setupS (retS, retL, "Retrig", 1, 8, 1);
    setupS (sliceS, sliceL, "Slice (-1 auto)", -1, 15, 1);
    addAndMakeVisible (lockBtn);
    velS.onValueChange   = [this] { const float v = (float) velS.getValue();   proc.modifyStep (grid.selLane, grid.selStep, [v] (Step& s) { s.velocity = v; }); };
    pitchS.onValueChange = [this] { const int v = (int) pitchS.getValue();     proc.modifyStep (grid.selLane, grid.selStep, [v] (Step& s) { s.pitch = v; }); };
    filtS.onValueChange  = [this] { const float v = (float) filtS.getValue();  proc.modifyStep (grid.selLane, grid.selStep, [v] (Step& s) { s.filter = v; }); };
    retS.onValueChange   = [this] { const int v = (int) retS.getValue();       proc.modifyStep (grid.selLane, grid.selStep, [v] (Step& s) { s.retrig = v; }); };
    sliceS.onValueChange = [this] { const int v = (int) sliceS.getValue();     proc.modifyStep (grid.selLane, grid.selStep, [v] (Step& s) { s.slice = v; }); };
    lockBtn.onClick = [this] { const bool v = lockBtn.getToggleState(); proc.modifyStep (grid.selLane, grid.selStep, [v] (Step& s) { s.locked = v; }); };

    // knobs: row 1 = generator, row 2 = sound + FX
    addKnob ("density", "Density", 1);
    addKnob ("sync", "Syncopation", 1);
    addKnob ("chaos", "Chaos", 1);
    addKnob ("evolve", "Evolve", 1);
    addKnob ("steps", "Steps", 1);
    addKnob ("swing", "Swing", 1);
    addCombo ("slices", "Slices", 1);
    addKnob ("breakBpm", "Break BPM", 1);
    addToggle ("breakSync", "Break Sync", 1);
    addKnob ("drive", "Drive", 2);
    addCombo ("fltMode", "Filter", 2);
    addKnob ("cutoff", "Cutoff", 2);
    addKnob ("res", "Reso", 2);
    addCombo ("dlyTime", "Delay Time", 2);
    addKnob ("dlyFb", "Feedback", 2);
    addKnob ("dlyMix", "Delay Mix", 2);
    addKnob ("master", "Master", 2);

    refreshStepPanel();
    setSize (1000, 640);
    startTimerHz (30);
}

FractureEditor::~FractureEditor() { stopTimer(); setLookAndFeel (nullptr); }

void FractureEditor::setupMomentary (juce::TextButton& b, const char* paramId)
{
    b.onStateChange = [this, &b, paramId]
    {
        const bool down = b.isDown();
        if (auto* prm = proc.apvts.getParameter (paramId))
            if ((prm->getValue() > 0.5f) != down)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (down ? 1.f : 0.f);
                prm->endChangeGesture();
            }
    };
}

void FractureEditor::addKnob (const juce::String& id, const juce::String& name, int row)
{
    auto k = std::make_unique<Knob>();
    k->s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k->l.setText (name, juce::dontSendNotification);
    k->l.setJustificationType (juce::Justification::centred);
    k->a = std::make_unique<SliderAtt> (proc.apvts, id, k->s);
    addAndMakeVisible (k->s); addAndMakeVisible (k->l);
    items.push_back ({ &k->s, &k->l, row, false });
    knobs.push_back (std::move (k));
}

void FractureEditor::addCombo (const juce::String& id, const juce::String& name, int row)
{
    auto c = std::make_unique<Combo>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->c.addItemList (p->choices, 1);
    c->l.setText (name, juce::dontSendNotification);
    c->l.setJustificationType (juce::Justification::centred);
    c->a = std::make_unique<ComboAtt> (proc.apvts, id, c->c);
    addAndMakeVisible (c->c); addAndMakeVisible (c->l);
    items.push_back ({ &c->c, &c->l, row, true });
    combos.push_back (std::move (c));
}

void FractureEditor::addToggle (const juce::String& id, const juce::String& name, int row)
{
    auto t = std::make_unique<Toggle>();
    t->b.setButtonText ("On");
    t->l.setText (name, juce::dontSendNotification);
    t->l.setJustificationType (juce::Justification::centred);
    t->a = std::make_unique<ButtonAtt> (proc.apvts, id, t->b);
    addAndMakeVisible (t->b); addAndMakeVisible (t->l);
    items.push_back ({ &t->b, &t->l, row, true });
    toggles.push_back (std::move (t));
}

void FractureEditor::refreshStepPanel()
{
    Pattern p;
    proc.copyPattern (p);
    const Step& st = p.at (grid.selLane, grid.selStep);
    if (! velS.isMouseButtonDown())   velS.setValue (st.velocity, juce::dontSendNotification);
    if (! pitchS.isMouseButtonDown()) pitchS.setValue (st.pitch, juce::dontSendNotification);
    if (! filtS.isMouseButtonDown())  filtS.setValue (st.filter, juce::dontSendNotification);
    if (! retS.isMouseButtonDown())   retS.setValue (st.retrig, juce::dontSendNotification);
    if (! sliceS.isMouseButtonDown()) sliceS.setValue (st.slice, juce::dontSendNotification);
    lockBtn.setToggleState (st.locked, juce::dontSendNotification);
    stepTitle.setText (juce::String (kLaneNames[grid.selLane]) + "  -  step " + juce::String (grid.selStep + 1) + (st.on ? "" : "  (off)"),
                       juce::dontSendNotification);
}

void FractureEditor::timerCallback()
{
    grid.repaint();
    refreshStepPanel();
}

void FractureEditor::exportMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Fracture.mid"), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f.deleteFile();
                              juce::FileOutputStream os (f);
                              if (os.openedOk()) proc.buildMidiFile (8).writeTo (os);
                          });
}

void FractureEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e0f13));
    g.setColour (juce::Colour (0xffff5a36));
    g.setFont (juce::FontOptions (26.f, juce::Font::bold));
    g.drawText ("FRACTURE", 12, 8, 170, 36, juce::Justification::centredLeft);
    g.setColour (juce::Colours::grey);
    g.setFont (11.f);
    g.drawText ("click = toggle  |  shift/right-click = lock step  |  MIDI in: C1-E1 lanes, C2-D#3 slices", 12, getHeight() - 18, 700, 14, juce::Justification::centredLeft);
}

void FractureEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (40);
    top.removeFromLeft (180);
    styleBox.setBounds (top.removeFromLeft (120).reduced (0, 6));
    top.removeFromLeft (10);
    regenBtn.setBounds (top.removeFromLeft (96).reduced (0, 6));
    top.removeFromLeft (6);
    clearBtn.setBounds (top.removeFromLeft (60).reduced (0, 6));
    top.removeFromLeft (16);
    fillBtn.setBounds (top.removeFromLeft (60).reduced (0, 6));
    top.removeFromLeft (6);
    rollBtn.setBounds (top.removeFromLeft (60).reduced (0, 6));
    dragBtn.setBounds (top.removeFromRight (90).reduced (0, 6));
    top.removeFromRight (6);
    exportBtn.setBounds (top.removeFromRight (100).reduced (0, 6));

    r.removeFromTop (8);
    auto gridArea = r.removeFromTop (kLanes * 40);
    auto left = gridArea.removeFromLeft (200);
    grid.setBounds (gridArea);
    for (int i = 0; i < kLanes; ++i)
    {
        auto row = left.removeFromTop (40).reduced (0, 6);
        auto& lr = laneRows[(size_t) i];
        lr.name.setBounds (row.removeFromLeft (46));
        lr.load.setBounds (row.removeFromLeft (38));
        row.removeFromLeft (2);
        lr.clear.setBounds (row.removeFromLeft (20));
        row.removeFromLeft (2);
        lr.mute.setBounds (row.removeFromLeft (34));
        lr.level.setBounds (row);
    }

    r.removeFromTop (8);
    auto panel = r.removeFromTop (92);
    stepTitle.setBounds (panel.removeFromTop (22));
    auto pw = panel.getWidth() / 3;
    auto place = [] (juce::Rectangle<int> a, juce::Label& l, juce::Slider& s)
    {
        l.setBounds (a.removeFromLeft (92));
        s.setBounds (a);
    };
    auto colA = panel.removeFromLeft (pw).reduced (4, 0);
    auto colB = panel.removeFromLeft (pw).reduced (4, 0);
    auto colC = panel.reduced (4, 0);
    place (colA.removeFromTop (22), velL, velS);
    place (colA.removeFromTop (22), pitchL, pitchS);
    place (colB.removeFromTop (22), filtL, filtS);
    place (colB.removeFromTop (22), retL, retS);
    place (colC.removeFromTop (22), sliceL, sliceS);
    lockBtn.setBounds (colC.removeFromTop (22));

    r.removeFromTop (6);
    const int rowH = 104, itemW = 78;
    for (int row = 1; row <= 2; ++row)
    {
        auto rr = r.removeFromTop (rowH);
        int x = rr.getX();
        for (auto& it : items)
        {
            if (it.row != row) continue;
            juce::Rectangle<int> cell (x, rr.getY(), itemW, rowH);
            it.label->setBounds (cell.removeFromTop (16));
            if (it.small) it.comp->setBounds (cell.withSizeKeepingCentre (itemW - 6, 26).translated (0, -8));
            else          it.comp->setBounds (cell);
            x += itemW + 4;
        }
    }
}
