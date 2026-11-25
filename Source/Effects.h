#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace fracture
{
// Zero-delay-feedback state variable filter (TPT).
struct Svf
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;

    void set (float fc, float q, double sr)
    {
        fc = std::min (fc, (float) (sr * 0.45));
        const float g = std::tan (3.14159265f * fc / (float) sr);
        k = 1.f / q;
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // mode: 0 = low-pass, 1 = high-pass, 2 = band-pass
    float process (float x, int mode)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        if (mode == 0) return v2;
        if (mode == 1) return x - k * v1 - v2;
        return k * v1;
    }
    void reset() { ic1 = ic2 = 0.f; }
};

inline float saturate (float x, float g, float makeup) { return std::tanh (x * g) * makeup; }

// Ping-pong feedback delay with damping in the loop and smoothed delay time.
struct StereoDelay
{
    std::vector<float> bufL, bufR;
    int size = 0, w = 0;
    float time = 0.f, lpL = 0.f, lpR = 0.f;

    void prepare (double sr)
    {
        size = (int) (sr * 4.0) + 4;
        bufL.assign ((size_t) size, 0.f);
        bufR.assign ((size_t) size, 0.f);
        w = 0; lpL = lpR = 0.f;
        time = (float) (sr * 0.25);
    }

    float read (const std::vector<float>& b, float d) const
    {
        float rp = (float) w - d;
        while (rp < 0.f) rp += (float) size;
        int i0 = (int) rp;
        const float fr = rp - (float) i0;
        if (i0 >= size) i0 -= size;
        int i1 = i0 + 1; if (i1 >= size) i1 = 0;
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
    }

    void process (float& l, float& r, float targetSamples, float fb, float mix, float damp)
    {
        time += (targetSamples - time) * 0.0004f;
        const float d = std::clamp (time, 2.f, (float) (size - 3));
        const float dl = read (bufL, d), dr = read (bufR, d);
        lpL += damp * (dr - lpL);   // cross-feed = ping-pong
        lpR += damp * (dl - lpR);
        bufL[(size_t) w] = l + lpL * fb;
        bufR[(size_t) w] = r + lpR * fb;
        if (++w >= size) w = 0;
        l += dl * mix;
        r += dr * mix;
    }
};
}
