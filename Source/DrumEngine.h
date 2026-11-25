#pragma once
#include <JuceHeader.h>
#include <memory>
#include "Sequencer.h"

namespace fracture
{
// A loaded audio file (one-shot or break). Onset analysis lets slice boundaries snap to transients.
struct Sample
{
    static constexpr int hop = 128;
    juce::AudioBuffer<float> data;
    double sampleRate = 44100.0;
    std::vector<float> onset;

    int length() const { return data.getNumSamples(); }

    void analyse()
    {
        const int n = length();
        const int hops = n / hop;
        std::vector<float> e ((size_t) hops + 1, 0.f);
        for (int h = 0; h < hops; ++h)
        {
            float sum = 0.f;
            for (int c = 0; c < data.getNumChannels(); ++c)
            {
                const float* d = data.getReadPointer (c) + h * hop;
                for (int i = 0; i < hop; ++i) sum += std::abs (d[i]);
            }
            e[(size_t) h] = sum;
        }
        onset.assign ((size_t) hops + 1, 0.f);
        for (int h = 1; h <= hops; ++h)
            onset[(size_t) h] = std::max (0.f, e[(size_t) h] - e[(size_t) h - 1]);
    }

    // Start sample of slice k when the sample is cut into n slices; k == n returns the end.
    int sliceBoundary (int k, int n) const
    {
        const int len = length();
        if (k <= 0) return 0;
        if (k >= n) return len;
        const int nominal = (int) ((int64_t) len * k / n);
        const int w = std::max (1, (len / n / 4) / hop);
        const int hn = nominal / hop;
        int best = -1; float bv = 1e-4f;
        const int lo = std::max (1, hn - w), hi = std::min ((int) onset.size() - 1, hn + w);
        for (int h = lo; h <= hi; ++h)
            if (onset[(size_t) h] > bv) { bv = onset[(size_t) h]; best = h; }
        return best < 0 ? nominal : std::max (0, best * hop - 32);
    }
};

// Built-in synthesised percussion: 0 kick, 1 snare, 2 hat, 3 clap, 4 perc/tom.
struct SynthVoice
{
    bool active = false;
    int type = 0, age = 0;
    float vel = 1.f, ratio = 1.f, phase = 0.f, n1 = 0.f, lp = 0.f;
    double sr = 44100.0;
    juce::Random rng;

    void trigger (int t, float v, int semis, double sampleRate)
    {
        type = std::min (t, 4); vel = v; ratio = std::pow (2.f, (float) semis / 12.f);
        sr = sampleRate; age = 0; phase = 0.f; n1 = 0.f; lp = 0.f; active = true;
    }

    float next()
    {
        if (! active) return 0.f;
        const float a = (float) ((double) age++ / sr);
        const float tp = 6.2831853f;
        const float nz = rng.nextFloat() * 2.f - 1.f;
        float out = 0.f;
        switch (type)
        {
            case 0: // kick
            {
                const float f = (48.f + 130.f * std::exp (-a * 38.f)) * ratio;
                phase += f / (float) sr;
                out = std::sin (tp * phase) * std::exp (-a * 9.f) * 1.1f + nz * std::exp (-a * 600.f) * 0.35f;
                if (a > 0.5f) active = false;
                break;
            }
            case 1: // snare
            {
                phase += 185.f * ratio * (1.f + 0.3f * std::exp (-a * 50.f)) / (float) sr;
                const float hp = nz - lp; lp += 0.35f * (nz - lp);
                out = std::sin (tp * phase) * std::exp (-a * 28.f) * 0.55f + hp * std::exp (-a * 18.f) * 0.8f;
                if (a > 0.35f) active = false;
                break;
            }
            case 2: // closed hat
            {
                const float hp = nz - n1; n1 = nz;
                out = hp * std::exp (-a * 75.f * ratio) * 0.7f;
                if (a > 0.12f) active = false;
                break;
            }
            case 3: // clap
            {
                float env = 0.f;
                for (int b = 0; b < 3; ++b) { const float t = a - 0.011f * (float) b; if (t >= 0.f) env += std::exp (-t * 220.f); }
                if (a > 0.033f) env += std::exp (-(a - 0.033f) * 30.f) * 0.6f;
                lp += 0.45f * (nz - lp); n1 += 0.08f * (lp - n1);
                out = (lp - n1) * env * 1.6f;
                if (a > 0.3f) active = false;
                break;
            }
            default: // perc / tom
            {
                phase += (120.f + 80.f * std::exp (-a * 25.f)) * ratio / (float) sr;
                out = std::sin (tp * phase) * std::exp (-a * 12.f) * 0.9f;
                if (a > 0.4f) active = false;
                break;
            }
        }
        return out * vel;
    }
};

// One-bar synthesised break at 170 BPM so the Break lane makes noise before any sample is loaded.
inline std::shared_ptr<Sample> makeDefaultBreak (double sr)
{
    const double bpm = 170.0;
    const int step = (int) (sr * 60.0 / bpm / 4.0);
    const int len = step * 16;
    auto s = std::make_shared<Sample>();
    s->sampleRate = sr;
    s->data.setSize (1, len);
    s->data.clear();
    float* d = s->data.getWritePointer (0);
    SynthVoice kick, snare, hat;
    const bool K[16] = { 1,0,1,0, 0,0,0,0, 0,0,1,0, 0,0,0,0 };
    const float S[16] = { 0,0,0,0, 1,0,0,.35f, 0,.35f,0,0, 1,0,0,.4f };
    for (int i = 0; i < len; ++i)
    {
        const int st = i / step;
        if (i % step == 0)
        {
            if (K[st]) kick.trigger (0, 1.f, 0, sr);
            if (S[st] > 0.f) snare.trigger (1, S[st], 0, sr);
            if (st % 2 == 0) hat.trigger (2, st % 4 == 0 ? 0.9f : 0.55f, 0, sr);
        }
        d[i] = 0.9f * kick.next() + 0.8f * snare.next() + 0.45f * hat.next();
    }
    const float peak = s->data.getMagnitude (0, len);
    if (peak > 0.0001f) s->data.applyGain (0.9f / peak);
    s->analyse();
    return s;
}

// One percussion lane: a sample/slice player or a synth voice, with a per-step lowpass lock.
struct Lane
{
    int index = 0;
    SynthVoice synth;
    std::shared_ptr<Sample> active, pending;   // active: audio thread only. pending: hand-off slot.
    bool hasNew = false;
    juce::SpinLock lock;

    bool playing = false, usingSample = false, useLp = false;
    double pos = 0, end = 0, inc = 1;
    float gain = 1.f, lpC = 1.f, lpL = 0.f, lpR = 0.f;
    float tailL = 0.f, tailR = 0.f, lastL = 0.f, lastR = 0.f;   // short crossfade to avoid clicks on retrigger

    void setPending (std::shared_ptr<Sample> s)
    {
        juce::SpinLock::ScopedLockType sl (lock);
        pending = std::move (s);
        hasNew = true;
    }

    void pollPending()
    {
        juce::SpinLock::ScopedTryLockType tl (lock);
        if (tl.isLocked() && hasNew)
        {
            std::swap (active, pending);   // old sample is freed later on the message thread
            hasNew = false; playing = false; usingSample = false;
        }
    }

    void trigger (float vel, int semis, float filter, int slice, int nSlices, double sr, float rateMul)
    {
        tailL = lastL; tailR = lastR;
        gain = vel;
        useLp = filter < 0.99f;
        if (useLp) lpC = 1.f - std::exp (-6.2831853f * (80.f * std::pow (250.f, filter)) / (float) sr);
        const float ratio = std::pow (2.f, (float) semis / 12.f);

        if (active && active->length() > 1)
        {
            const Sample& s = *active;
            double start = 0.0, e = (double) s.length();
            if (index == kBreakLane)
            {
                const int k = std::clamp (slice < 0 ? 0 : slice, 0, nSlices - 1);
                start = s.sliceBoundary (k, nSlices);
                e = s.sliceBoundary (k + 1, nSlices);
            }
            pos = start; end = e;
            inc = (double) (ratio * rateMul) * s.sampleRate / sr;
            usingSample = true;
            playing = e > start + 1.0;
        }
        else
        {
            synth.trigger (index, 1.f, semis, sr);
            usingSample = false;
            playing = true;
        }
    }

    void next (float& l, float& r)
    {
        float a = 0.f, b = 0.f;
        if (playing)
        {
            if (usingSample && active)
            {
                const Sample& s = *active;
                const int i = (int) pos;
                if (i >= s.length() - 1) playing = false;
                else
                {
                    const float fr = (float) (pos - (double) i);
                    const float* c0 = s.data.getReadPointer (0);
                    const float* c1 = s.data.getReadPointer (s.data.getNumChannels() > 1 ? 1 : 0);
                    a = c0[i] + (c0[i + 1] - c0[i]) * fr;
                    b = c1[i] + (c1[i + 1] - c1[i]) * fr;
                    pos += inc;
                    if (pos >= end) playing = false;
                    const double remaining = (end - pos) / inc;
                    if (remaining < 48.0) { const float f = (float) remaining / 48.f; a *= f; b *= f; }
                }
            }
            else
            {
                a = b = synth.next();
                if (! synth.active) playing = false;
            }
            a *= gain; b *= gain;
            if (useLp) { lpL += lpC * (a - lpL); lpR += lpC * (b - lpR); a = lpL; b = lpR; }
        }
        a += tailL; b += tailR;
        tailL *= 0.985f; tailR *= 0.985f;
        lastL = a; lastR = b;
        l = a; r = b;
    }
};
}
