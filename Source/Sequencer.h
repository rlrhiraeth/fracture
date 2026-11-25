#pragma once
// Pure C++ (no JUCE) – pattern model + genre-specific generative algorithms.
#include <array>
#include <random>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fracture
{
constexpr int kLanes = 6;
constexpr int kMaxSteps = 32;
constexpr int kBreakLane = 5;
inline constexpr const char* kLaneNames[kLanes] = { "Kick", "Snare", "Hat", "Clap", "Perc", "Break" };

enum class Style { Jungle = 0, Footwork, Hybrid, Breakcore };

struct Step
{
    bool on = false;
    bool locked = false;   // survives regeneration + evolution
    float velocity = 0.8f; // 0..1
    int pitch = 0;         // semitones (parameter lock)
    float filter = 1.0f;   // 1 = open, lower = darker (parameter lock)
    int retrig = 1;        // 1..8 (parameter lock)
    int slice = -1;        // break lane only: -1 = in-order slice, else slice index
};

struct Pattern
{
    std::array<std::array<Step, kMaxSteps>, kLanes> s {};
    Step& at (int lane, int i)             { return s[(size_t) lane][(size_t) i]; }
    const Step& at (int lane, int i) const { return s[(size_t) lane][(size_t) i]; }
};

struct GenParams
{
    Style style = Style::Jungle;
    float density = 0.5f, syncopation = 0.4f, chaos = 0.25f;
    int length = 16;
    int slices = 16;
};

using Rng = std::mt19937;
inline float rnd (Rng& r) { return std::uniform_real_distribution<float> (0.f, 1.f) (r); }

namespace detail
{
// Probability templates over one 16-step bar: [0 jungle, 1 footwork, 2 breakcore][lane][step]
// Lanes: Kick, Snare, Hat, Clap, Perc, Break. A weight of 1.0 marks an "anchor" step.
inline constexpr float kT[3][kLanes][16] = {
    { // Jungle (amen-style skeleton)
      { 1, 0, .25f, 0,   0, 0, 0, .15f,  0, .2f, .9f, 0,   0, 0, .1f, .1f },
      { 0, 0, 0, 0,      1, 0, 0, .3f,   0, .3f, 0, 0,     1, 0, 0, .35f },
      { .8f, .3f, .8f, .3f, .8f, .3f, .8f, .3f, .8f, .3f, .8f, .3f, .8f, .3f, .8f, .3f },
      { 0, 0, 0, 0,      .2f, 0, 0, .15f, 0, 0, .2f, 0,    .25f, 0, .15f, 0 },
      { 0, 0, .1f, 0,    0, .1f, 0, 0,   0, 0, 0, .15f,    0, 0, .1f, .2f },
      { .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f, .8f } },
    { // Footwork (3-3-2 feel)
      { 1, 0, 0, .9f,    0, 0, .9f, 0,   .6f, 0, 0, .9f,   0, 0, .6f, 0 },
      { 0, 0, 0, 0,      .9f, 0, 0, .5f, 0, 0, .6f, 0,     .9f, 0, 0, .3f },
      { .9f, .6f, .7f, .6f, .9f, .6f, .7f, .6f, .9f, .6f, .7f, .6f, .9f, .6f, .7f, .6f },
      { .7f, 0, 0, .7f,  0, 0, .7f, 0,   .5f, 0, 0, .6f,   0, 0, .5f, 0 },
      { 0, 0, 0, .2f,    0, .2f, 0, 0,   0, 0, .3f, 0,     0, 0, 0, .3f },
      { .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f, .15f } },
    { // Breakcore (dense, unstable)
      { 1, .2f, .3f, .2f, .2f, .3f, .2f, .3f, .3f, .2f, .5f, .2f, .2f, .3f, .2f, .3f },
      { 0, .2f, .3f, .2f, 1, .3f, .2f, .4f,   .2f, .4f, .3f, .2f, 1, .2f, .3f, .5f },
      { .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f, .6f },
      { .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f },
      { .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f, .3f },
      { .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f, .85f } }
};
}

inline float weight (Style st, int lane, int step)
{
    const int i = step % 16;
    switch (st)
    {
        case Style::Jungle:    return detail::kT[0][lane][i];
        case Style::Footwork:  return detail::kT[1][lane][i];
        case Style::Breakcore: return detail::kT[2][lane][i];
        case Style::Hybrid:    return i < 8 ? detail::kT[0][lane][i] : detail::kT[1][lane][i];
    }
    return 0.f;
}

inline bool isAnchor (const GenParams& g, int lane, int i)
{
    return weight (g.style, lane, i) >= 0.99f && g.chaos < 0.85f;
}

// Fill in velocity + parameter locks for a step that has just been switched on.
inline void decorate (Step& st, int lane, int i, const GenParams& g, Rng& r, bool anchor)
{
    st.on = true;
    const float chaos = g.chaos;

    if (anchor)        st.velocity = 1.0f;
    else if (i % 4 == 0) st.velocity = 0.85f + 0.15f * rnd (r);
    else if (i % 2 == 0) st.velocity = 0.65f + 0.20f * rnd (r);
    else                 st.velocity = 0.35f + 0.30f * rnd (r);

    st.retrig = 1;
    float rc = chaos * ((lane == 1 || lane == 2) ? 0.30f : 0.12f);
    if (g.style == Style::Breakcore) rc += 0.12f;
    if (! anchor && rnd (r) < rc)
    {
        static const int opts[] = { 2, 2, 3, 4, 6, 8 };
        st.retrig = opts[std::min (5, (int) (rnd (r) * (2.f + chaos * 4.f)))];
    }

    st.pitch = 0;
    if (! anchor && chaos > 0.3f && rnd (r) < chaos * 0.3f)
        st.pitch = (int) ((rnd (r) - 0.5f) * 2.f * 12.f * chaos);

    st.filter = 1.0f;
    if (rnd (r) < chaos * 0.25f)
        st.filter = 0.25f + 0.6f * rnd (r);

    st.slice = -1;
    if (lane == kBreakLane)
    {
        const float chance = chaos * 0.7f + (g.style == Style::Footwork ? 0.3f : 0.f);
        if (rnd (r) < chance)
            st.slice = std::min (g.slices - 1, (int) (rnd (r) * (float) g.slices));
    }
}

inline void generate (Pattern& p, const GenParams& g, Rng& r)
{
    for (int lane = 0; lane < kLanes; ++lane)
    {
        for (int i = 0; i < kMaxSteps; ++i)   // all 32 steps, so lengthening the pattern reveals material
        {
            Step& st = p.at (lane, i);
            if (st.locked) continue;

            const float base = weight (g.style, lane, i);
            const bool anchor = isAnchor (g, lane, i);

            float pr = base * (0.35f + 1.3f * g.density);
            if (i % 4 != 0)      pr += g.syncopation * 0.25f * (0.3f + base);
            else if (! anchor)   pr *= (1.f - 0.5f * g.syncopation);
            pr += g.chaos * (rnd (r) - 0.5f);
            pr = std::clamp (pr, 0.f, 1.f);

            const bool on = anchor ? true : (rnd (r) < pr);
            st = Step {};
            if (on) decorate (st, lane, i, g, r, anchor);
        }
    }
}

// Evolution: small random drift that respects the style's skeleton and skips locked steps.
inline void mutate (Pattern& p, const GenParams& g, float amount, Rng& r)
{
    if (amount <= 0.f) return;
    for (int lane = 0; lane < kLanes; ++lane)
    {
        for (int i = 0; i < kMaxSteps; ++i)
        {
            Step& st = p.at (lane, i);
            if (st.locked || isAnchor (g, lane, i)) continue;
            const float base = weight (g.style, lane, i);

            if (st.on)
            {
                if (rnd (r) < amount * 0.10f * (1.f - base))
                    st = Step {};
                else if (rnd (r) < amount * 0.18f)
                {
                    const float k = rnd (r);
                    if (lane == kBreakLane && k < 0.5f)
                        st.slice = std::min (g.slices - 1, (int) (rnd (r) * (float) g.slices));
                    else if (k < 0.5f)
                        st.retrig = (st.retrig == 1) ? 2 + (int) (rnd (r) * 3.f) : 1;
                    else if (k < 0.75f)
                        st.pitch = (int) ((rnd (r) - 0.5f) * 2.f * 7.f * std::max (0.2f, g.chaos));
                    else
                        st.filter = rnd (r) < 0.5f ? 1.f : 0.3f + 0.6f * rnd (r);
                }
            }
            else if (rnd (r) < amount * 0.05f * (0.15f + base))
            {
                decorate (st, lane, i, g, r, false);
            }
        }
    }
}
} // namespace fracture
