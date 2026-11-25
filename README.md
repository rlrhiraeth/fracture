# Fracture – generative rhythmic instrument (VST3)

JUCE 8 / C++17 project. Builds VST3 + Standalone.

## Build
macOS: `xcode-select --install` and `brew install cmake`
Windows: Visual Studio 2022 (Desktop C++) + CMake

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

JUCE is downloaded automatically. The VST3 is copied to the system VST3 folder after the build
(macOS: ~/Library/Audio/Plug-Ins/VST3, Windows: C:\Program Files\Common Files\VST3).
Then rescan plug-ins in Ableton Live (Preferences > Plug-ins). Load Fracture on a MIDI track.
Ableton on macOS also needs the plug-in to be signed on Apple Silicon: `codesign --force --deep -s - Fracture.vst3`.

## Using it
- Grid: click/drag = toggle steps. Shift- or right-click = lock a step (white outline); locked steps survive Regenerate and Evolve.
- Click a step to select it; the panel below edits its locks: velocity, pitch, filter, retrig, slice (Break lane).
- Style: Jungle / Footwork / Hybrid / Breakcore. Density, Syncopation, Chaos shape generation. Regenerate rolls a new pattern.
- Evolve: how much the pattern mutates each time it loops (anchor hits like the kick on 1 and snare backbeat stay put).
- Fill (hold): snare build into the end of the pattern. Roll (hold): accelerating snare roll. Both are automatable.
- Lanes: Kick, Snare, Hat, Clap, Perc, Break. Each has a built-in synth voice; Load puts your own sample in. The Break lane ships with a synthesised 170 BPM break
  and slices any loaded loop (4/8/16 slices, snapped to transients). Break BPM + Sync varispeeds the break to the host tempo.
- FX: Drive (saturation) > Filter (LP/HP/BP) > ping-pong delay synced to host tempo.
- MIDI export: "Export MIDI" saves 8 repeats of the pattern (with evolution applied) at the host tempo; "Drag MIDI" drags the same clip
  straight into an Ableton MIDI track. Notes: lanes = C1..E1 (36-40), Break slices = C2..D#3 (48-63).
- MIDI in: the same notes trigger the lanes/slices live.
- Transport: follows host tempo, play state and position. In Standalone it free-runs at 120 BPM.

## Layout
Source/Sequencer.h    pattern model + style templates + generate/mutate (no JUCE dependency)
Source/DrumEngine.h   synth percussion, sample/slice player, default break
Source/Effects.h      saturation, TPT filter, ping-pong delay
Source/PluginProcessor.*  parameters, host sync, event scheduling, state, MIDI export
Source/PluginEditor.*     UI

## No compiler? Build in the cloud
Push this folder to a GitHub repo (Actions enabled). The workflow in .github/workflows/build.yml builds
macOS and Windows VST3s; download them from the run's Artifacts. Copy Fracture.vst3 to
~/Library/Audio/Plug-Ins/VST3 (macOS) or C:\Program Files\Common Files\VST3 (Windows).
