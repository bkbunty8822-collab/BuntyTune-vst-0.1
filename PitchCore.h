// BuntyTune - original pitch-correction DSP core (no JUCE dependency)
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

class PitchCorrector
{
public:
    void prepare (double sr, int numCh)
    {
        sampleRate = sr;
        channels = std::max (1, std::min (numCh, 2));
        grain = std::max (64, (int) (0.030 * sr));          // 30 ms grain
        for (auto& d : dl) d.assign (kDelaySize, 0.0f);
        det.assign (kDetSize, 0.0f);
        wp = dw = hop = unvoiced = voicedCount = 0; autoRoot = -1; haveCenter = false;
        for (auto& h : hist) h = 0.0;
        phase = 0.0; ratio = target = 1.0; halfS = halfT = grain * 0.5;
        tauMax = std::min (3000, (int) (sr / 70.0));
        tauMin = std::max (2, (int) (sr / 800.0));
        diff.assign (tauMax + 2, 0.0f);
        cmnd.assign (tauMax + 2, 0.0f);
        lin.assign (kWindow + tauMax + 2, 0.0f);
        setParams (0, 0, 20.0f, 1.0f, 0.5f);
    }

    int getLatency() const { return grain / 2; }

    // key 0..11 (C..B); scale 0=Auto 1=Chromatic 2=Major 3=Minor 4=MinPent 5=Bhairavi 6=Kafi 7=Khamaj 8=Bhairav
    // keep = 0..1 : how much natural vibrato/expression is preserved
    void setParams (int key, int scale, float speedMs, float mixAmt, float keep = 0.5f)
    {
        scaleMode = std::max (0, std::min (8, scale));
        vibKeep = std::max (0.0f, std::min (1.0f, keep));
        static const bool masks[8][12] = {
            {1,1,1,1,1,1,1,1,1,1,1,1},
            {1,0,1,0,1,1,0,1,0,1,0,1},
            {1,0,1,1,0,1,0,1,1,0,1,0},
            {1,0,0,1,0,1,0,1,0,0,1,0},
            {1,1,0,1,0,1,0,1,1,0,1,0},   // Bhairavi (komal Re Ga Dha Ni)
            {1,0,1,1,0,1,0,1,0,1,1,0},   // Kafi (komal Ga Ni)
            {1,0,1,0,1,1,0,1,0,1,1,0},   // Khamaj (komal Ni)
            {1,1,0,0,1,1,0,1,1,0,0,1} }; // Bhairav (komal Re Dha)
        keyRoot = key;
        for (int i = 0; i < 12; ++i) allowed[i] = masks[std::max (0, scaleMode - 1)][i];
        double t = std::max (0.5, (double) speedMs) * 0.001 * sampleRate;
        alpha = 1.0 - std::exp (-1.0 / t);
        mix = mixAmt;
    }

    void process (float** io, int numCh, int numSamples)
    {
        const int nc = std::min (numCh, channels);
        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int c = 0; c < nc; ++c) { dl[c][wp & kDelayMask] = io[c][i]; mono += io[c][i]; }
            det[dw & kDetMask] = mono / (float) nc;
            ++dw;
            if (++hop >= kHop) { hop = 0; analyse(); }

            ratio += alpha * (target - ratio);
            halfS += 0.002 * (halfT - halfS);                 // smooth grain change
            const double G = 2.0 * halfS;
            phase += (1.0 - ratio) / G;
            phase -= std::floor (phase);

            const double d1 = phase * G;
            const double d2 = std::fmod (phase + 0.5, 1.0) * G;
            const float w1 = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * phase));
            const float w2 = 1.0f - w1;

            for (int c = 0; c < nc; ++c)
            {
                float wet = w1 * readDelay (c, d1) + w2 * readDelay (c, d2);
                float dry = readDelay (c, halfS);
                io[c][i] = dry * (1.0f - (float) mix) + wet * (float) mix;
            }
            ++wp;
        }
    }

private:
    static constexpr int kDelaySize = 16384, kDelayMask = kDelaySize - 1;
    static constexpr int kDetSize = 8192, kDetMask = kDetSize - 1;
    static constexpr int kWindow = 1024, kHop = 512;

    float readDelay (int c, double d) const
    {
        double pos = (double) wp - d;
        long i0 = (long) std::floor (pos);
        float f = (float) (pos - (double) i0);
        float a = dl[c][i0 & kDelayMask], b = dl[c][(i0 + 1) & kDelayMask];
        return a + f * (b - a);
    }

    void analyse()
    {
        const int total = kWindow + tauMax + 1;
        double energy = 0.0;
        for (int k = 0; k < total; ++k)
        {
            lin[k] = det[(dw - total + k) & kDetMask];
            if (k < kWindow) energy += (double) lin[k] * lin[k];
        }
        if (std::sqrt (energy / kWindow) < 0.01) { markUnvoiced(); return; }

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double s = 0.0;
            for (int j = 0; j < kWindow; ++j) { float e = lin[j] - lin[j + tau]; s += (double) e * e; }
            diff[tau] = (float) s;
        }
        double run = 0.0;
        cmnd[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += diff[tau];
            cmnd[tau] = run > 0.0 ? (float) (diff[tau] * tau / run) : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cmnd[tau] < 0.15f)
            {
                while (tau + 1 < tauMax && cmnd[tau + 1] < cmnd[tau]) ++tau;
                best = tau; break;
            }
        if (best < 0) { markUnvoiced(); return; }

        double tauF = best;
        if (best > 1 && best < tauMax)
        {
            double a = cmnd[best - 1], b = cmnd[best], c = cmnd[best + 1];
            double den = a - 2.0 * b + c;
            if (std::fabs (den) > 1e-9) tauF += 0.5 * (a - c) / den;
        }
        {   // heads exactly k pitch periods apart => crossfade stays in phase
            int k = std::max (1, (int) std::lround (0.015 * sampleRate / tauF));
            halfT = std::min (6000.0, k * tauF);
        }
        double f0 = sampleRate / tauF;
        double midi = 69.0 + 12.0 * std::log2 (f0 / 440.0);

        // slow "centre pitch" so vibrato can be kept
        if (!haveCenter || std::fabs (midi - center) > 1.0) { center = midi; haveCenter = true; }
        else center += 0.09 * (midi - center);

        // histogram of sung pitch classes -> auto key detection
        for (auto& h : hist) h *= 0.9995;
        hist[(((int) std::lround (midi) % 12) + 12) % 12] += 1.0;
        ++voicedCount;
        if (scaleMode == 0 && voicedCount >= 80 && voicedCount % 8 == 0) updateAutoKey();

        const double ref = vibKeep * center + (1.0 - vibKeep) * midi;
        int nearest = (int) std::lround (ref);
        double bestDist = 1e9; int bestNote = nearest;
        for (int n = nearest - 6; n <= nearest + 6; ++n)
        {
            if (!noteAllowed (n)) continue;
            double dist = std::fabs ((double) n - ref);
            if (dist < bestDist) { bestDist = dist; bestNote = n; }
        }
        unvoiced = 0;
        target = std::max (0.5, std::min (2.0, std::pow (2.0, ((double) bestNote - ref) / 12.0)));
    }

    bool noteAllowed (int n) const
    {
        static const bool major[12] = {1,0,1,0,1,1,0,1,0,1,0,1};
        if (scaleMode == 0)
            return autoRoot < 0 ? true : major[(((n - autoRoot) % 12) + 12) % 12];
        return allowed[(((n - keyRoot) % 12) + 12) % 12];
    }

    // Krumhansl-Kessler major profile; relative minor shares the same notes
    double keyScore (int root) const
    {
        static const double prof[12] = {6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88};
        double mh = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mh += hist[(root + i) % 12]; mp += prof[i]; }
        mh /= 12; mp /= 12;
        double num = 0, dh = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            double a = hist[(root + i) % 12] - mh, b = prof[i] - mp;
            num += a * b; dh += a * a; dp += b * b;
        }
        return (dh > 0 && dp > 0) ? num / std::sqrt (dh * dp) : 0.0;
    }

    void updateAutoKey()
    {
        int best = 0; double bs = -2.0;
        for (int r = 0; r < 12; ++r) { double s = keyScore (r); if (s > bs) { bs = s; best = r; } }
        if (autoRoot < 0 || (best != autoRoot && bs > keyScore (autoRoot) + 0.03)) autoRoot = best;
    }

    void markUnvoiced() { if (++unvoiced > 3) { target = 1.0; haveCenter = false; } }

    double sampleRate = 44100.0;
    int channels = 2, grain = 1323, wp = 0, dw = 0, hop = 0, unvoiced = 0;
    int tauMax = 600, tauMin = 55, keyRoot = 0;
    bool allowed[12] {};
    int scaleMode = 0, voicedCount = 0, autoRoot = -1;
    float vibKeep = 0.5f;
    bool haveCenter = false;
    double center = 0.0, hist[12] {};
    double halfS = 720.0, halfT = 720.0;
    double phase = 0.0, ratio = 1.0, target = 1.0, alpha = 0.01, mix = 1.0;
    std::vector<float> dl[2], det, diff, cmnd, lin;
};
