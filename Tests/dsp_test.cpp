// Pruebas del núcleo DSP de Seda (sin JUCE).
//   g++ -O2 -std=c++17 Tests/dsp_test.cpp -o dsp_test && ./dsp_test
#include "../Source/VocalReverbDSP.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#if defined(__SSE2__)
 #include <xmmintrin.h>
 #include <pmmintrin.h>
#endif

using namespace vr;

static const double FS = 48000.0;

struct Stereo { std::vector<float> L, R; };

// Mismo efecto que juce::ScopedNoDenormals
static void enableFTZ()
{
#if defined(__SSE2__)
    _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#endif
}

static Params wetOnly()
{
    Params p;
    p.mix = 1.0f; p.early = 0.0f; p.duck = 0.0f; p.deEss = 0.0f;
    p.preDelayMs = 0.0f; p.lowCutHz = 10.0f; p.highCutHz = 20000.0f;
    return p;
}

static Stereo renderIR (const Params& p, double seconds, double fs = FS)
{
    VocalReverb rev;
    rev.prepare (fs);
    rev.setParams (p);
    const int n = (int) (seconds * fs);
    Stereo s { std::vector<float> ((size_t) n, 0.0f), std::vector<float> ((size_t) n, 0.0f) };
    s.L[0] = s.R[0] = 1.0f;
    for (int pos = 0; pos < n; pos += 512)
        rev.process (&s.L[(size_t) pos], &s.R[(size_t) pos], std::min (512, n - pos));
    return s;
}

// RT60 extrapolado de la curva de Schroeder entre fromDb y toDb
static double rt60 (const std::vector<float>& x, double fs, double fromDb, double toDb)
{
    const size_t n = x.size();
    std::vector<double> edc (n);
    double acc = 0;
    for (size_t i = n; i-- > 0;) { acc += (double) x[i] * x[i]; edc[i] = acc; }
    const double total = edc[0];
    long t1 = -1, t2 = -1;
    for (size_t i = 0; i < n; ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / total + 1e-30);
        if (t1 < 0 && db <= fromDb) t1 = (long) i;
        if (t2 < 0 && db <= toDb) { t2 = (long) i; break; }
    }
    if (t1 < 0 || t2 < 0) return -1;
    const double slope = (toDb - fromDb) / ((double) (t2 - t1) / fs);   // dB/s (negativo)
    return -60.0 / slope;
}

static std::vector<float> filtered (const std::vector<float>& x, bool lowpass, float fc)
{
    SVF f; f.set (fc, (float) FS);
    SVF g; g.set (fc, (float) FS);
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        f.tick (x[i]);
        const float a = lowpass ? f.lp : f.hp;
        g.tick (a);
        y[i] = lowpass ? g.lp : g.hp;    // 24 dB/oct para aislar bien la banda
    }
    return y;
}

// Densidad de ecos normalizada (Abel & Huang): 1.0 = ruido gaussiano ideal
static double ned (const std::vector<float>& x, double fs, double tCenterSec)
{
    const int half = (int) (0.010 * fs);
    const int c = (int) (tCenterSec * fs);
    double e = 0; int cnt = 0;
    for (int i = c - half; i <= c + half; ++i) { e += (double) x[(size_t) i] * x[(size_t) i]; ++cnt; }
    const double sigma = std::sqrt (e / cnt);
    int out = 0;
    for (int i = c - half; i <= c + half; ++i) if (std::fabs (x[(size_t) i]) > sigma) ++out;
    return ((double) out / cnt) / 0.3173;
}

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Desviación (dB) del espectro de la cola respecto a su media local (1/6 de octava).
// Ruido difuso ideal ~ 5.6 dB. Valores >> 7 dB = modos "metálicos"/resonantes.
static double spectralRoughness (const std::vector<float>& x, double fs, double t0)
{
    const size_t N = 32768;
    std::vector<std::complex<double>> a (N);
    const size_t s0 = (size_t) (t0 * fs);
    for (size_t i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * (double) i / (double) N);
        a[i] = (double) x[s0 + i] * w;
    }
    fft (a);
    std::vector<double> pw (N / 2);
    for (size_t k = 0; k < N / 2; ++k) pw[k] = std::norm (a[k]) + 1e-30;
    const double df = fs / (double) N;
    double sum = 0, sum2 = 0; int cnt = 0;
    for (size_t k = (size_t) (200.0 / df); k < (size_t) (4000.0 / df); ++k)
    {
        const size_t lo = (size_t) ((double) k / 1.12), hi = (size_t) ((double) k * 1.12);
        double m = 0; for (size_t j = lo; j <= hi; ++j) m += pw[j];
        m /= (double) (hi - lo + 1);
        const double d = 10.0 * std::log10 (pw[k] / m);
        sum += d; sum2 += d * d; ++cnt;
    }
    const double mean = sum / cnt;
    return std::sqrt (sum2 / cnt - mean * mean);
}

static double corr (const std::vector<float>& a, const std::vector<float>& b, size_t i0, size_t i1)
{
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = i0; i < i1; ++i) { ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
    return ab / std::sqrt (aa * bb + 1e-30);
}

static double rmsDb (const std::vector<float>& x, size_t i0, size_t i1)
{
    double e = 0; for (size_t i = i0; i < i1; ++i) e += (double) x[i] * x[i];
    return 10.0 * std::log10 (e / (double) (i1 - i0) + 1e-30);
}

static int failures = 0;
#define CHECK(cond, ...) do { const bool ok_ = (cond); std::printf ("  [%s] ", ok_ ? "OK " : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

int main()
{
    enableFTZ();

    // ---------------------------------------------------------------- 0. delay
    {
        std::printf ("0) DelayLine\n");
        DelayLine d; d.prepare (1000);
        float last = 0; bool ok = true;
        std::vector<float> hist;
        for (int n = 0; n < 3000; ++n)
        {
            const float x = std::sin (0.01f * (float) n * (float) n * 0.01f);
            if (n > 600) { const float r = d.read (500.0f); if (std::fabs (r - hist[(size_t) (n - 500)]) > 1e-6f) ok = false; }
            d.write (x); hist.push_back (x); last = x;
        }
        (void) last;
        CHECK (ok, "read(d entero) devuelve exactamente lo escrito hace d muestras");
    }

    // ---------------------------------------------------------------- 1. RT60
    std::printf ("1) RT60 en bandas planas (damping=0, bass=1) - objetivo = parámetro decay\n");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (float decay : { 0.6f, 2.0f, 6.0f })
        {
            Params p = wetOnly(); p.damping = 0; p.bass = 1.0f; p.decaySec = decay;
            auto ir = renderIR (p, 14.0, fs);
            const double r = rt60 (ir.L, fs, -5, -35);
            CHECK (std::fabs (r - decay) / decay < 0.12, "fs=%.0f  decay=%.1f s  -> RT60 medido %.2f s", fs, decay, r);
        }

    std::printf ("2) Decaimiento por bandas con los valores por defecto (graves y agudos deben caer antes)\n");
    {
        Params p = wetOnly(); p.decaySec = 3.0f;      // damping 0.55, bass 0.6 (por defecto)
        auto ir = renderIR (p, 12.0);
        const double rLo = rt60 (filtered (ir.L, true, 200.0f), FS, -5, -25) ;
        const double rMid = rt60 (filtered (filtered (ir.L, false, 600.0f), true, 1500.0f), FS, -5, -25);
        const double rHi = rt60 (filtered (ir.L, false, 6000.0f), FS, -5, -25);
        std::printf ("     RT60 <200 Hz: %.2f s | 600-1500 Hz: %.2f s | >6 kHz: %.2f s   (decay=3.0)\n", rLo, rMid, rHi);
        CHECK (rLo < rMid * 0.85, "los graves decaen antes que los medios (menos barro)");
        CHECK (rHi < rMid * 0.60, "los agudos decaen mucho antes que los medios (cola sedosa)");
        CHECK (std::fabs (rMid - 3.0) / 3.0 < 0.2, "los medios respetan el decay pedido");
    }

    // ------------------------------------------------- 3. densidad y suavidad
    std::printf ("3) Densidad de ecos y suavidad (mejor cuanto más cerca de 1.0 / 5.6 dB)\n");
    for (float size : { 0.6f, 1.0f, 1.8f })
    {
        Params p = wetOnly(); p.size = size; p.decaySec = 2.0f;
        auto ir = renderIR (p, 4.0);
        std::printf ("     size=%.1f  NED @60ms=%.2f @120ms=%.2f @250ms=%.2f @500ms=%.2f | rugosidad espectral %.1f dB\n",
                     size, ned (ir.L, FS, 0.06), ned (ir.L, FS, 0.12), ned (ir.L, FS, 0.25), ned (ir.L, FS, 0.5),
                     spectralRoughness (ir.L, FS, 0.3));
        CHECK (ned (ir.L, FS, 0.25) > 0.9, "size=%.1f: a los 250 ms la cola ya es ruido difuso", size);
        CHECK (spectralRoughness (ir.L, FS, 0.3) < 7.5, "size=%.1f: sin resonancias metálicas marcadas", size);
    }

    std::printf ("4) Estéreo: correlación L/R de la cola con entrada mono (0 = ancho total)\n");
    {
        Params p = wetOnly(); p.decaySec = 2.5f;
        auto ir = renderIR (p, 5.0);
        const double c = corr (ir.L, ir.R, (size_t) (0.2 * FS), (size_t) (3.0 * FS));
        CHECK (std::fabs (c) < 0.25, "correlación = %.3f", c);
    }

    // -------------------------------------------------------- 5. estabilidad
    std::printf ("5) Estabilidad: automatización aleatoria extrema + ruido a 0 dBFS\n");
    {
        VocalReverb rev; rev.prepare (FS);
        std::mt19937 rng (1234);
        std::uniform_real_distribution<float> U (0.0f, 1.0f);
        auto R = [&] (float lo, float hi) { return lo + (hi - lo) * (U (rng) < 0.25f ? (U (rng) < 0.5f ? 0.0f : 1.0f) : U (rng)); };
        bool finite = true; float peak = 0;
        std::vector<float> L (256), Rr (256);
        for (int blk = 0; blk < 4000; ++blk)
        {
            Params p;
            p.mix = R (0, 1); p.decaySec = R (0.2f, 15); p.preDelayMs = R (0, 250); p.size = R (0.5f, 2);
            p.damping = R (0, 1); p.bass = R (0.25f, 1.5f); p.diffusion = R (0, 1); p.early = R (0, 1);
            p.modDepth = R (0, 1); p.modRate = R (0.1f, 3); p.width = R (0, 2); p.lowCutHz = R (20, 1000);
            p.highCutHz = R (1000, 20000); p.duck = R (0, 1); p.duckReleaseMs = R (30, 1000); p.deEss = R (0, 1);
            rev.setParams (p);
            const bool silent = (blk / 50) % 3 == 2;
            for (int i = 0; i < 256; ++i) { L[(size_t) i] = silent ? 0.0f : 2.0f * U (rng) - 1.0f; Rr[(size_t) i] = silent ? 0.0f : 2.0f * U (rng) - 1.0f; }
            rev.process (L.data(), Rr.data(), 256);
            for (int i = 0; i < 256; ++i)
            {
                if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (Rr[(size_t) i])) finite = false;
                peak = std::max (peak, std::max (std::fabs (L[(size_t) i]), std::fabs (Rr[(size_t) i])));
            }
        }
        CHECK (finite, "ninguna muestra NaN/Inf");
        CHECK (peak < 12.0f, "pico máximo con ruido a fondo y parámetros extremos: %.2f", peak);
    }
    {
        Params p; p.decaySec = 15; p.bass = 1.5f; p.damping = 0; p.mix = 1; p.duck = 0;
        VocalReverb rev; rev.prepare (FS); rev.setParams (p);
        std::mt19937 rng (7); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
        std::vector<float> L (512), R (512);
        float tailPeak = 0;
        for (int blk = 0; blk < (int) (60.0 * FS / 512); ++blk)
        {
            const bool burst = blk < 200;
            for (int i = 0; i < 512; ++i) { L[(size_t) i] = burst ? U (rng) : 0.0f; R[(size_t) i] = burst ? U (rng) : 0.0f; }
            rev.process (L.data(), R.data(), 512);
            if (blk > (int) (55.0 * FS / 512)) for (int i = 0; i < 512; ++i) tailPeak = std::max (tailPeak, std::fabs (L[(size_t) i]));
        }
        CHECK (tailPeak < 1e-3f, "decay=15 s, bass=1.5: la cola muere (pico a los 55 s = %.2e)", tailPeak);
    }

    // -------------------------------------------------- 6. nivel y ducking
    std::printf ("6) Nivel: ruido 'vocal' (-20 dB RMS), wet puro, sin ER (RMS wet relativo a entrada)\n");
    auto noiseRun = [&] (Params p, double burstSec, double totalSec, std::vector<float>& outL, std::vector<float>& inL)
    {
        VocalReverb rev; rev.prepare (FS); rev.setParams (p);
        std::mt19937 rng (99); std::normal_distribution<float> N (0.0f, 0.1f);
        const size_t n = (size_t) (totalSec * FS);
        outL.assign (n, 0.0f); inL.assign (n, 0.0f);
        std::vector<float> L (512), R (512);
        float lp = 0;
        for (size_t pos = 0; pos < n; pos += 512)
        {
            const int m = (int) std::min<size_t> (512, n - pos);
            for (int i = 0; i < m; ++i)
            {
                const bool on = (double) (pos + (size_t) i) < burstSec * FS;
                lp += 0.35f * ((on ? N (rng) : 0.0f) - lp);                      // ruido rosado-ish
                L[(size_t) i] = R[(size_t) i] = lp * 2.2f;
            }
            std::copy (L.begin(), L.begin() + m, inL.begin() + (long) pos);
            rev.process (L.data(), R.data(), m);
            std::copy (L.begin(), L.begin() + m, outL.begin() + (long) pos);
        }
    };
    for (float decay : { 0.5f, 1.5f, 3.0f, 6.0f })
    {
        Params p = wetOnly(); p.decaySec = decay; p.lowCutHz = 120.0f; p.highCutHz = 12000.0f;
        std::vector<float> o, in;
        noiseRun (p, 12.0, 12.0, o, in);
        std::printf ("     decay=%.1f s -> wet %.1f dB re entrada\n", decay, rmsDb (o, (size_t) (8 * FS), (size_t) (12 * FS)) - rmsDb (in, (size_t) (8 * FS), (size_t) (12 * FS)));
    }

    std::printf ("7) Ducking: nivel de la cola cuando la voz suena (duck 0 vs 1)\n");
    {
        double lv[4];
        for (int k = 0; k < 2; ++k)
        {
            Params p = wetOnly(); p.duck = (float) k; p.decaySec = 2.5f; p.duckReleaseMs = 300.0f;
            std::vector<float> o, in;
            noiseRun (p, 3.0, 5.0, o, in);
            lv[k] = rmsDb (o, (size_t) (2.5 * FS), (size_t) (3.0 * FS));
            lv[k + 2] = rmsDb (o, (size_t) (3.8 * FS), (size_t) (4.0 * FS));
        }
        std::printf ("     0.8-1.0 s tras cortar la voz: duck=1 vs duck=0 -> %.1f dB de diferencia (la cola vuelve a florecer)\n", lv[3] - lv[2]);
        CHECK (lv[0] - lv[1] > 6.0 && lv[0] - lv[1] < 40.0, "con duck=1 la cola baja %.1f dB mientras suena la voz", lv[0] - lv[1]);
    }

    std::printf ("9) De-esser relativo: vocal (armónicos graves) vs sibilante (ruido > 6 kHz)\n");
    {
        auto run = [&] (bool sibilant, float deEss)
        {
            Params p = wetOnly(); p.deEss = deEss; p.decaySec = 1.5f;
            VocalReverb rev; rev.prepare (FS); rev.setParams (p);
            std::mt19937 rng (3); std::normal_distribution<float> N (0.0f, 0.1f);
            SVF hp1, hp2; hp1.set (6000.0f, (float) FS); hp2.set (6000.0f, (float) FS);
            const size_t n = (size_t) (4.0 * FS);
            std::vector<float> out (n);
            std::vector<float> L (512), R (512);
            for (size_t pos = 0; pos < n; pos += 512)
            {
                const int m = (int) std::min<size_t> (512, n - pos);
                for (int i = 0; i < m; ++i)
                {
                    const double t = (double) (pos + (size_t) i) / FS;
                    float x = 0;
                    if (sibilant) { hp1.tick (N (rng)); hp2.tick (hp1.hp); x = hp2.hp * 1.5f; }
                    else for (int h = 1; h <= 12; ++h) x += (float) (std::sin (2.0 * 3.14159265358979 * 220.0 * h * t) / h) * 0.12f;
                    L[(size_t) i] = R[(size_t) i] = x;
                }
                rev.process (L.data(), R.data(), m);
                std::copy (L.begin(), L.begin() + m, out.begin() + (long) pos);
            }
            return rmsDb (out, (size_t) (2.5 * FS), (size_t) (4.0 * FS));
        };
        const double voicedDelta = run (false, 1.0f) - run (false, 0.0f);
        const double sibDelta = run (true, 1.0f) - run (true, 0.0f);
        std::printf ("     vocal: %+.1f dB | sibilante: %+.1f dB (con deEss=1 vs 0)\n", voicedDelta, sibDelta);
        CHECK (voicedDelta > -1.0, "no toca la voz cantada");
        CHECK (sibDelta < -6.0, "atenúa las sibilantes en el send del reverb");
    }

    // ------------------------------------------------------------------ CPU
    {
        Params p; VocalReverb rev; rev.prepare (FS); rev.setParams (p);
        std::vector<float> L ((size_t) (FS * 20)), R (L.size());
        std::mt19937 rng (5); std::uniform_real_distribution<float> U (-0.3f, 0.3f);
        for (auto& v : L) v = U (rng);
        R = L;
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (size_t pos = 0; pos < L.size(); pos += 512) rev.process (&L[pos], &R[pos], 512);
        const double dt = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        std::printf ("8) CPU: 20 s de audio estéreo en %.3f s  (~%.2f %% de un núcleo @48 kHz)\n", dt, 100.0 * dt / 20.0);
    }

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
