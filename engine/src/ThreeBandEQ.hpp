#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

// Per-channel 3-band DJ mixer EQ / isolator.
//
// This replaces the old "mid = dry - low - high" splitter.  That trick is
// numerically neat at unity, but it makes cuts feel odd because the dry sample
// is not phase-matched to the filtered low/high bands.  This version uses a
// proper 3-way Linkwitz-Riley style splitter:
//
//   input -> LR4 split @ low/mid -> LOW + UPPER
//   UPPER -> LR4 split @ mid/high -> MID + HIGH
//   LOW is all-pass compensated through the mid/high split, so the three bands
//   recombine like a real isolator instead of a subtraction artifact.
//
// Knob convention:
//   0.0 = full left, 0.5 = center detent / unity, 1.0 = full right
//
// Curves:
//   DJM_A9_EQ        : -26 dB .. +6 dB, matching the public DJM-A9 range.
//   DJM_A9_ISO      : full-kill style cut .. +6 dB, useful for Rekordbox/DJM ISO.
//   DJM_V10_MASTER  : full-kill style cut .. +9 dB, like a V10 master isolator.
//
// It keeps the original public calls (setLow/setMid/setHigh/process), so it can
// be dropped in for the old ThreeBandEQ class.
class ThreeBandEQ {
public:
    enum class Curve {
        DJM_A9_EQ = 0,
        DJM_A9_ISO = 1,
        DJM_V10_MASTER = 2
    };

    explicit ThreeBandEQ(int sampleRate = 44100) {
        setSampleRate(sampleRate);
        reset();
    }

    void setSampleRate(int sr) {
        sampleRate_ = sr > 0 ? sr : 44100;
        configureFilters();

        const float smoothingMs = 8.0f;
        lowSmooth_.setTime(sampleRate_, smoothingMs);
        midSmooth_.setTime(sampleRate_, smoothingMs);
        highSmooth_.setTime(sampleRate_, smoothingMs);
    }

    // Optional, but useful if you want exact behavior per mixer mode.
    void setCurve(Curve curve) {
        curve_.store(static_cast<int>(curve), std::memory_order_relaxed);
    }

    // Optional: tune the split points for your taste.  A more DJM-ish default is
    // intentionally wider than the old 250/2500 split: LOW owns kick/bass, HIGH
    // starts around hats/air, and MID carries the body/vocal range.
    void setCrossoverFrequencies(float lowMidHz, float midHighHz) {
        lowMidHz_ = sanitizeFrequency(lowMidHz, 20.0f, 0.45f * sampleRate_);
        midHighHz_ = sanitizeFrequency(midHighHz, lowMidHz_ * 1.5f, 0.45f * sampleRate_);
        configureFilters();
    }

    // Clears filter history. Call after seek, stop/start, or a sample-rate jump.
    void reset() {
        for (int c = 0; c < 2; ++c) {
            split_[c].reset();
        }

        const Curve c = currentCurve();
        lowSmooth_.reset(gainFromKnob(lowKnob_.load(std::memory_order_relaxed), c));
        midSmooth_.reset(gainFromKnob(midKnob_.load(std::memory_order_relaxed), c));
        highSmooth_.reset(gainFromKnob(highKnob_.load(std::memory_order_relaxed), c));
    }

    // knob in [0, 1], center detent = 0 dB.
    void setLow(float knob)  { lowKnob_.store(clamp01(knob),  std::memory_order_relaxed); }
    void setMid(float knob)  { midKnob_.store(clamp01(knob),  std::memory_order_relaxed); }
    void setHigh(float knob) { highKnob_.store(clamp01(knob), std::memory_order_relaxed); }

    // Current knob positions (for UI feedback so script/automation moves the knobs).
    float getLow()  const { return lowKnob_.load(std::memory_order_relaxed); }
    float getMid()  const { return midKnob_.load(std::memory_order_relaxed); }
    float getHigh() const { return highKnob_.load(std::memory_order_relaxed); }

    // Processes two de-interleaved channels in place.
    void process(float* left, float* right, int frames) {
        if (!left || !right || frames <= 0) {
            return;
        }

        const Curve curve = currentCurve();
        lowSmooth_.setTarget(gainFromKnob(lowKnob_.load(std::memory_order_relaxed), curve));
        midSmooth_.setTarget(gainFromKnob(midKnob_.load(std::memory_order_relaxed), curve));
        highSmooth_.setTarget(gainFromKnob(highKnob_.load(std::memory_order_relaxed), curve));

        for (int i = 0; i < frames; ++i) {
            const float lg = lowSmooth_.next();
            const float mg = midSmooth_.next();
            const float hg = highSmooth_.next();

            const Bands lb = split_[0].process(left[i]);
            const Bands rb = split_[1].process(right[i]);

            left[i]  = lb.low  * lg + lb.mid  * mg + lb.high  * hg;
            right[i] = rb.low  * lg + rb.mid  * mg + rb.high  * hg;
        }
    }

private:
    struct Bands {
        float low;
        float mid;
        float high;
    };

    // Transposed Direct Form II biquad.  Coefficients are normalized so a0 = 1.
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void reset() { z1 = z2 = 0.0; }

        inline float process(float input) {
            const double x = static_cast<double>(input);
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return static_cast<float>(snapToZero(y));
        }

        void setLowpass(float hz, int fs)  { setButterworth(hz, fs, Type::Lowpass); }
        void setHighpass(float hz, int fs) { setButterworth(hz, fs, Type::Highpass); }
        void setAllpass(float hz, int fs)  { setButterworth(hz, fs, Type::Allpass); }

    private:
        enum class Type { Lowpass, Highpass, Allpass };

        void setButterworth(float hz, int fs, Type type) {
            const double f = static_cast<double>(sanitizeFrequency(hz, 20.0f, 0.45f * fs));
            const double w0 = 2.0 * kPi * f / static_cast<double>(fs);
            const double cw = std::cos(w0);
            const double sw = std::sin(w0);
            const double q = 0.70710678118654752440; // Butterworth Q
            const double alpha = sw / (2.0 * q);
            const double a0 = 1.0 + alpha;

            double B0 = 1.0, B1 = 0.0, B2 = 0.0;

            switch (type) {
                case Type::Lowpass:
                    B0 = (1.0 - cw) * 0.5;
                    B1 =  1.0 - cw;
                    B2 = (1.0 - cw) * 0.5;
                    break;

                case Type::Highpass:
                    B0 = (1.0 + cw) * 0.5;
                    B1 = -(1.0 + cw);
                    B2 = (1.0 + cw) * 0.5;
                    break;

                case Type::Allpass: {
                    const double A1 = -2.0 * cw / a0;
                    const double A2 = (1.0 - alpha) / a0;
                    b0 = A2;
                    b1 = A1;
                    b2 = 1.0;
                    a1 = A1;
                    a2 = A2;
                    return;
                }
            }

            b0 = B0 / a0;
            b1 = B1 / a0;
            b2 = B2 / a0;
            a1 = (-2.0 * cw) / a0;
            a2 = (1.0 - alpha) / a0;
        }
    };

    struct LR4Split {
        Biquad lp[2];
        Biquad hp[2];

        void set(float hz, int fs) {
            for (int i = 0; i < 2; ++i) {
                lp[i].setLowpass(hz, fs);
                hp[i].setHighpass(hz, fs);
            }
        }

        void reset() {
            for (int i = 0; i < 2; ++i) {
                lp[i].reset();
                hp[i].reset();
            }
        }

        inline void process(float x, float& low, float& high) {
            low = lp[1].process(lp[0].process(x));
            high = hp[1].process(hp[0].process(x));
        }
    };

    struct ThreeWaySplitter {
        LR4Split lowMid;
        LR4Split midHigh;
        Biquad lowPhaseMatch[2];

        void set(float lowMidHz, float midHighHz, int fs) {
            lowMid.set(lowMidHz, fs);
            midHigh.set(midHighHz, fs);
            for (int i = 0; i < 2; ++i) {
                lowPhaseMatch[i].setAllpass(midHighHz, fs);
            }
        }

        void reset() {
            lowMid.reset();
            midHigh.reset();
            lowPhaseMatch[0].reset();
            lowPhaseMatch[1].reset();
        }

        inline Bands process(float x) {
            float lowRaw = 0.0f;
            float upper = 0.0f;
            lowMid.process(x, lowRaw, upper);

            float mid = 0.0f;
            float high = 0.0f;
            midHigh.process(upper, mid, high);

            const float low = lowPhaseMatch[1].process(lowPhaseMatch[0].process(lowRaw));
            return { low, mid, high };
        }
    };

    struct LinearSmoother {
        float value = 1.0f;
        float target = 1.0f;
        float coeff = 0.0f;

        void setTime(int fs, float milliseconds) {
            const float seconds = std::max(milliseconds, 0.1f) * 0.001f;
            coeff = std::exp(-1.0f / (seconds * static_cast<float>(std::max(fs, 1))));
        }

        void reset(float v) {
            value = target = v;
        }

        void setTarget(float v) {
            target = v;
        }

        inline float next() {
            value = target + (value - target) * coeff;
            if (std::fabs(value - target) < 1.0e-7f) {
                value = target;
            }
            return value;
        }
    };

    void configureFilters() {
        lowMidHz_ = sanitizeFrequency(lowMidHz_, 20.0f, 0.45f * sampleRate_);
        midHighHz_ = sanitizeFrequency(midHighHz_, lowMidHz_ * 1.5f, 0.45f * sampleRate_);
        for (int c = 0; c < 2; ++c) {
            split_[c].set(lowMidHz_, midHighHz_, sampleRate_);
        }
    }

    Curve currentCurve() const {
        const int raw = curve_.load(std::memory_order_relaxed);
        if (raw == static_cast<int>(Curve::DJM_A9_EQ)) return Curve::DJM_A9_EQ;
        if (raw == static_cast<int>(Curve::DJM_V10_MASTER)) return Curve::DJM_V10_MASTER;
        return Curve::DJM_A9_ISO;
    }

    static float gainFromKnob(float k, Curve curve) {
        k = clamp01(k);

        // Make the virtual center detent exact.  This avoids tiny GUI jitter from
        // moving a supposedly-flat EQ away from unity.
        if (std::fabs(k - 0.5f) < 0.0015f) {
            return 1.0f;
        }

        switch (curve) {
            case Curve::DJM_A9_EQ:
                return dbToGain(knobToDb(k, -26.0f, 6.0f, false));

            case Curve::DJM_V10_MASTER:
                if (k <= 0.002f) return 0.0f;
                return dbToGain(knobToDb(k, -72.0f, 9.0f, true));

            case Curve::DJM_A9_ISO:
            default:
                if (k <= 0.002f) return 0.0f;
                return dbToGain(knobToDb(k, -72.0f, 6.0f, true));
        }
    }

    static float knobToDb(float k, float minDb, float maxDb, bool isolatorCut) {
        if (k >= 0.5f) {
            const float t = (k - 0.5f) * 2.0f;
            // Boost side is intentionally close to linear-in-dB: +6/+9 should not
            // explode before the end of the throw.
            return maxDb * t;
        }

        const float t = (0.5f - k) * 2.0f;
        // Real DJ EQs feel gentle just below center and much steeper near the
        // bottom.  The old file cut too much immediately off the center detent.
        const float shaped = std::pow(t, isolatorCut ? 1.35f : 1.15f);
        return minDb * shaped;
    }

    static float dbToGain(float db) {
        return std::pow(10.0f, db / 20.0f);
    }

    static float clamp01(float v) {
        if (!std::isfinite(v)) return 0.5f;
        return std::max(0.0f, std::min(1.0f, v));
    }

    static float sanitizeFrequency(float hz, float minHz, float maxHz) {
        if (!std::isfinite(hz)) return minHz;
        if (maxHz < minHz) maxHz = minHz;
        return std::max(minHz, std::min(hz, maxHz));
    }

    static double snapToZero(double x) {
        return std::fabs(x) < 1.0e-24 ? 0.0 : x;
    }

    static constexpr double kPi = 3.14159265358979323846264338327950288;

    int sampleRate_ = 44100;
    float lowMidHz_ = 260.0f;
    float midHighHz_ = 4000.0f;

    std::atomic<int> curve_{static_cast<int>(Curve::DJM_A9_ISO)};
    std::atomic<float> lowKnob_{0.5f};
    std::atomic<float> midKnob_{0.5f};
    std::atomic<float> highKnob_{0.5f};

    LinearSmoother lowSmooth_;
    LinearSmoother midSmooth_;
    LinearSmoother highSmooth_;

    ThreeWaySplitter split_[2];
};
