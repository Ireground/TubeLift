#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include <memory>
#include <atomic>

// TubeLift engine: a two-stage triode preamp in the spirit of a 1950s tape-machine
// record/playback amp (the "A" flavour). Not tape: no hysteresis, no wow, no head bump.
//
//   input -> LOW CUT -> TONE tilt -> input-transformer weight
//         -> [4x oversampled]  drive -> triode 1 (biased, sagging) -> interstage (Miller roll-off)
//                               -> triode 2 -> coupling cap / DC block -> level compensation
//         -> output-transformer softening -> HIGH CUT
namespace tl
{
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        inline float process (float xf) noexcept
        {
            const double x = xf;
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
        void reset() noexcept { z1 = z2 = 0; }
        void bypass() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

        void set (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
        {
            b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
        }
        static double clampF (double fs, double f) { return juce::jlimit (5.0, fs * 0.45, f); }

        void lowpass (double fs, double f, double q)
        {
            const double w = juce::MathConstants<double>::twoPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
            set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
        }
        void highpass (double fs, double f, double q)
        {
            const double w = juce::MathConstants<double>::twoPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
            set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
        }
        void peak (double fs, double f, double q, double db)
        {
            const double A = std::pow (10.0, db / 40.0);
            const double w = juce::MathConstants<double>::twoPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
            set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
        }
        void lowShelf (double fs, double f, double db)
        {
            const double A = std::pow (10.0, db / 40.0), sA = std::sqrt (A);
            const double w = juce::MathConstants<double>::twoPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / 2 * std::sqrt (2.0);
            set (A * ((A + 1) - (A - 1) * c + 2 * sA * al), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - 2 * sA * al),
                 (A + 1) + (A - 1) * c + 2 * sA * al, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - 2 * sA * al);
        }
        void highShelf (double fs, double f, double db)
        {
            const double A = std::pow (10.0, db / 40.0), sA = std::sqrt (A);
            const double w = juce::MathConstants<double>::twoPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / 2 * std::sqrt (2.0);
            set (A * ((A + 1) + (A - 1) * c + 2 * sA * al), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - 2 * sA * al),
                 (A + 1) - (A - 1) * c + 2 * sA * al, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - 2 * sA * al);
        }
    };

    // Triode transfer curve: positive swing (grid conduction) bends over firmly,
    // negative swing (towards cutoff) bends later and softer -> asymmetry -> 2nd/even harmonics.
    inline float triode (float u) noexcept
    {
        return u >= 0.0f ? std::tanh (u) : std::tanh (0.72f * u) * (1.0f / 0.72f);
    }
    inline float stage (float x, float bias) noexcept { return triode (x + bias) - triode (bias); }

    class Engine
    {
    public:
        static constexpr float kBias1  = 0.35f;   // first triode operating point
        static constexpr float kBias2  = 0.60f;   // second triode
        static constexpr float kStage2 = 1.2f;    // interstage gain

        void prepare (double sampleRate, int maxBlock, int numChannels)
        {
            fs = sampleRate;
            nch = juce::jmax (1, numChannels);
            os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) nch, 2,
                    juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
            os->initProcessing ((size_t) maxBlock);
            osFs = fs * 4.0;
            gBuf.assign ((size_t) maxBlock * 4 + 8, 1.0f);
            cBuf.assign ((size_t) maxBlock * 4 + 8, 1.0f);
            ch.assign ((size_t) nch, Ch {});
            envAtk = 1.0f - (float) std::exp (-1.0 / (0.002 * osFs));
            envRel = 1.0f - (float) std::exp (-1.0 / (0.090 * osFs));
            driveSm.reset (osFs, 0.05);
            compSm.reset (osFs, 0.05);
            lastDrive = -1.0f;
            setParams (curDrive, curLowCut, curTone, curHighCut, true);
            reset();
        }

        void reset()
        {
            for (auto& c : ch)
            {
                for (auto* b : { &c.lowcut, &c.tiltLo, &c.tiltHi, &c.weight, &c.inter, &c.dc, &c.soften, &c.highcut })
                    b->reset();
                c.env = 0.0f;
            }
            if (os) os->reset();
        }

        int latency() const { return os ? juce::roundToInt (os->getLatencyInSamples()) : 0; }

        static float driveToGain (float drive) // 0..10 -> -6..+36 dB into the first triode
        {
            return juce::Decibels::decibelsToGain (-6.0f + drive * 4.2f);
        }

        // Static level match: a -12 dBFS signal leaves at roughly the level it came in, whatever
        // the drive, so DRIVE changes the colour instead of just blasting the volume.
        static float compensationFor (float g)
        {
            constexpr int N = 96;
            constexpr float a = 0.25f;   // -12 dBFS reference
            double si = 0, so = 0, mean = 0;
            float out[N];
            for (int i = 0; i < N; ++i)
            {
                const float s = a * std::sin (juce::MathConstants<float>::twoPi * (float) i / (float) N);
                out[i] = stage (stage (s * g, kBias1) * kStage2, kBias2);
                mean += out[i];
                si += (double) s * s;
            }
            mean /= N;
            for (int i = 0; i < N; ++i) so += (out[i] - mean) * (out[i] - mean);
            return so > 1.0e-12 ? (float) std::sqrt (si / so) : 1.0f;
        }

        void setParams (float drive, float lowCut, float tone, float highCut, bool force = false)
        {
            const bool filtersChanged = force || lowCut != curLowCut || tone != curTone || highCut != curHighCut || drive != curDrive;
            curLowCut = lowCut; curTone = tone; curHighCut = highCut;

            if (force || std::abs (drive - lastDrive) > 1.0e-4f)
            {
                lastDrive = drive;
                const float g = driveToGain (drive);
                if (force) { driveSm.setCurrentAndTargetValue (g); compSm.setCurrentAndTargetValue (compensationFor (g)); }
                else       { driveSm.setTargetValue (g);           compSm.setTargetValue (compensationFor (g)); }
            }
            curDrive = drive;
            if (! filtersChanged) return;

            const float d = drive / 10.0f;
            for (auto& c : ch)
            {
                c.lowcut.highpass (fs, lowCut, 0.707);
                c.tiltLo.lowShelf (fs, 900.0, -tone * 6.0);
                c.tiltHi.highShelf (fs, 900.0, tone * 6.0);
                c.weight.peak (fs, 170.0, 0.75, 1.6);                     // input transformer: a little low-mid body
                c.inter.lowpass (osFs, 21000.0 * std::pow (0.42, d), 0.6); // Miller roll-off: more drive, softer top
                c.dc.highpass (osFs, 9.0, 0.707);                          // coupling cap
                c.soften.highShelf (fs, 7500.0, -0.6 - 2.2 * d);           // output transformer
                if (highCut >= 19900.0f) c.highcut.bypass();
                else                     c.highcut.lowpass (fs, highCut, 0.707);
            }
        }

        // In-place on 'block' (block size must be <= maxBlock given to prepare)
        void process (juce::dsp::AudioBlock<float> block)
        {
            const int nc = juce::jmin ((int) block.getNumChannels(), nch);
            const int n = (int) block.getNumSamples();
            if (n == 0 || os == nullptr) return;

            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* d = block.getChannelPointer ((size_t) c);
                for (int i = 0; i < n; ++i)
                {
                    float x = s.lowcut.process (d[i]);
                    x = s.tiltLo.process (x);
                    x = s.tiltHi.process (x);
                    d[i] = s.weight.process (x);
                }
            }

            auto sub = block.getSubsetChannelBlock (0, (size_t) nc);
            auto up = os->processSamplesUp (sub);
            const int un = (int) up.getNumSamples();
            for (int i = 0; i < un; ++i) { gBuf[(size_t) i] = driveSm.getNextValue(); cBuf[(size_t) i] = compSm.getNextValue(); }

            float pk = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* d = up.getChannelPointer ((size_t) c);
                for (int i = 0; i < un; ++i)
                {
                    const float x = d[i] * gBuf[(size_t) i];
                    const float ax = std::abs (x);
                    pk = std::max (pk, ax);
                    s.env += (ax > s.env ? envAtk : envRel) * (ax - s.env);
                    // supply sag: loud passages push the first triode's bias -> blooming, fatter even harmonics
                    const float bias = kBias1 + 0.10f * std::min (s.env, 2.5f);
                    float y = stage (x, bias);
                    y = s.inter.process (y);
                    y = stage (y * kStage2, kBias2);
                    y = s.dc.process (y);
                    d[i] = y * cBuf[(size_t) i];
                }
            }
            os->processSamplesDown (sub);

            for (int c = 0; c < nc; ++c)
            {
                auto& s = ch[(size_t) c];
                auto* d = block.getChannelPointer ((size_t) c);
                for (int i = 0; i < n; ++i)
                    d[i] = s.highcut.process (s.soften.process (d[i]));
            }

            // ATTITUDE: how hard the signal hits the first tube
            float prev = attitudePeak.load();
            while (pk > prev && ! attitudePeak.compare_exchange_weak (prev, pk)) {}
        }

        std::atomic<float> attitudePeak { 0.0f };

    private:
        struct Ch
        {
            Biquad lowcut, tiltLo, tiltHi, weight, inter, dc, soften, highcut;
            float env = 0.0f;
        };

        double fs = 44100.0, osFs = 176400.0;
        int nch = 2;
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        std::vector<Ch> ch;
        std::vector<float> gBuf, cBuf;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> driveSm { 1.0f }, compSm { 1.0f };
        float envAtk = 0.01f, envRel = 0.0001f;
        float curDrive = 3.0f, curLowCut = 20.0f, curTone = 0.0f, curHighCut = 20000.0f, lastDrive = -1.0f;
    };
}
