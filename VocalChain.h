// BuntyTune - original vocal enhancement chain: HPF -> compressor -> presence/air EQ -> output gain
#pragma once
#include <cmath>
#include <algorithm>

static constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void reset() { z1 = z2 = 0; }
    void set (double nb0,double nb1,double nb2,double a0,double na1,double na2)
    { b0=nb0/a0; b1=nb1/a0; b2=nb2/a0; a1=na1/a0; a2=na2/a0; }
    void highpass (double sr, double f, double q = 0.7071)
    {
        double w=2*kPi*f/sr, c=cos(w), al=sin(w)/(2*q);
        set ((1+c)/2, -(1+c), (1+c)/2, 1+al, -2*c, 1-al);
    }
    void peaking (double sr, double f, double q, double gdb)
    {
        double A=pow(10.0,gdb/40), w=2*kPi*f/sr, c=cos(w), al=sin(w)/(2*q);
        set (1+al*A, -2*c, 1-al*A, 1+al/A, -2*c, 1-al/A);
    }
    void highshelf (double sr, double f, double gdb)
    {
        double A=pow(10.0,gdb/40), w=2*kPi*f/sr, c=cos(w), s=sin(w);
        double al=s/2*sqrt(2.0), sq=2*sqrt(A)*al;
        set (A*((A+1)+(A-1)*c+sq), -2*A*((A-1)+(A+1)*c), A*((A+1)+(A-1)*c-sq),
             (A+1)-(A-1)*c+sq, 2*((A-1)-(A+1)*c), (A+1)-(A-1)*c-sq);
    }
    float run (float x)
    { double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return (float) y; }
};

class VocalChain
{
public:
    void prepare (double sampleRate) { sr = sampleRate; for (auto& b : hp) b.reset(), 
        pk[0].reset(), pk[1].reset(), sh[0].reset(), sh[1].reset(); grDb = 0; }

    // hpfHz 20..300 (<=20 = off), thresh dB, ratio, attack ms, release ms, makeup dB,
    // presence dB @3.5k, air dB @10k, output dB
    void setParams (float hpfHz, float thrDb, float ratio_, float atkMs, float relMs,
                    float makeupDb, float presDb, float airDb, float outDb)
    {
        hpOn = hpfHz > 20.5f;
        for (auto& b : hp) b.highpass (sr, std::max (20.0f, hpfHz));
        for (auto& b : pk) b.peaking (sr, 3500.0, 1.0, presDb);
        for (auto& b : sh) b.highshelf (sr, 10000.0, airDb);
        thr = thrDb; ratio = std::max (1.0f, ratio_);
        atk = std::exp (-1.0 / (std::max (0.1f, atkMs) * 0.001 * sr));
        rel = std::exp (-1.0 / (std::max (1.0f, relMs) * 0.001 * sr));
        makeup = makeupDb; outGain = (float) std::pow (10.0, outDb / 20.0);
    }

    void process (float** io, int nch, int n)
    {
        nch = std::min (nch, 2);
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.0f;
            for (int c = 0; c < nch; ++c)
            {
                float x = io[c][i];
                if (hpOn) x = hp[c].run (x);
                io[c][i] = x;
                peak = std::max (peak, std::fabs (x));
            }
            double lvl = 20.0 * std::log10 ((double) peak + 1e-9);
            double over = lvl - thr, W = 6.0, outL;
            if (2 * over < -W) outL = lvl;
            else if (2 * std::fabs (over) <= W) outL = lvl + (1.0 / ratio - 1.0) * (over + W / 2) * (over + W / 2) / (2 * W);
            else outL = thr + over / ratio;
            double gr = outL - lvl;                       // <= 0
            double coef = gr < grDb ? atk : rel;          // more reduction -> attack
            grDb = coef * grDb + (1.0 - coef) * gr;
            float g = (float) std::pow (10.0, (grDb + makeup) / 20.0) * outGain;
            for (int c = 0; c < nch; ++c)
                io[c][i] = sh[c].run (pk[c].run (io[c][i])) * g;
        }
    }
    double lastGainReduction() const { return grDb; }

private:
    double sr = 44100, thr = -18, atk = 0.99, rel = 0.999, makeup = 0, grDb = 0;
    float ratio = 3, outGain = 1; bool hpOn = true;
    Biquad hp[2], pk[2], sh[2];
};
