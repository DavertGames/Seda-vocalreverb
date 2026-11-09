// =============================================================================
//  Seda - reverb para voces.  Núcleo DSP, header-only, SIN dependencias de JUCE.
//
//  Cadena de señal (estéreo):
//    in -> HPF/LPF (send) -> de-esser relativo -> pre-delay
//       -> reflexiones tempranas (8 taps por canal, con absorción de aire)
//       -> difusores all-pass modulados (4 por canal)
//       -> FDN de 16 líneas (Householder), delays modulados,
//          decaimiento por línea en 3 bandas (graves / medios / agudos)
//       -> salida decorrelada con filas de Hadamard -> ancho M/S
//       -> ducking (la cola se aparta mientras la voz canta) -> mix equal-power
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vr
{

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kLn1000 = 6.90775527898214f;   // 3*ln(10): 60 dB de caída

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Seno parabólico refinado (error < 0.1 %), fase 0..1. Suficiente y barato para LFOs.
inline float lfoSin (float phase01)
{
    const float x = phase01 * 2.0f - 1.0f;
    const float y = 4.0f * x * (1.0f - std::fabs (x));
    return 0.225f * (y * std::fabs (y) - y) + y;
}

inline int parity (unsigned v)
{
    int p = 0;
    while (v) { p ^= (int) (v & 1u); v >>= 1; }
    return p;
}

// ----------------------------------------------------------------------------
//  Filtro de variable de estado TPT (12 dB/oct), estable bajo modulación
// ----------------------------------------------------------------------------
struct SVF
{
    float s1 = 0, s2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1.41421356f;
    float lp = 0, hp = 0;

    void set (float fc, float fs, float q = 0.70710678f)
    {
        fc = clampf (fc, 5.0f, 0.45f * fs);
        const float g = std::tan (kPi * fc / fs);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline void tick (float x)
    {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        lp = v2;
        hp = x - k * v1 - v2;
    }

    void reset() { s1 = s2 = lp = hp = 0; }
};

// ----------------------------------------------------------------------------
//  Seguidor de envolvente (ataque/release independientes)
// ----------------------------------------------------------------------------
struct Follower
{
    float env = 0, atk = 0.1f, rel = 0.001f;

    void setTimes (float atkMs, float relMs, float fs)
    {
        atk = 1.0f - std::exp (-1.0f / (0.001f * atkMs * fs));
        rel = 1.0f - std::exp (-1.0f / (0.001f * relMs * fs));
    }

    inline float tick (float x)
    {
        x = std::fabs (x);
        env += (x > env ? atk : rel) * (x - env);
        return env;
    }
};

// Rampa lineal por muestra dentro de cada sub-bloque de control
struct Ramp
{
    float cur = 0, inc = 0;
    void snap (float v) { cur = v; inc = 0; }
    void set (float target, int len) { inc = (target - cur) / (float) len; }
    inline float next() { cur += inc; return cur; }
};

struct Smooth { float cur = 0, target = 0; };

// ----------------------------------------------------------------------------
//  Línea de retardo con interpolación Hermite cúbica (4 puntos).
//  IMPORTANTE: llamar read() ANTES de write() en cada muestra.
//  read(d) devuelve lo escrito hace d muestras (d >= 2).
// ----------------------------------------------------------------------------
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 8;
        while (size < maxDelaySamples + 8) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }

    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }

    inline void write (float x)
    {
        buf[(size_t) w] = x;
        w = (w + 1) & mask;
    }

    inline float read (float d) const
    {
        const int di = (int) d;
        const float f = d - (float) di;
        const int i0 = w - di;
        const float xm1 = buf[(size_t) ((i0 + 1) & mask)];
        const float x0  = buf[(size_t) (i0 & mask)];
        const float x1  = buf[(size_t) ((i0 - 1) & mask)];
        const float x2  = buf[(size_t) ((i0 - 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    std::vector<float> buf;
    int mask = 7, w = 0;
};

// All-pass de Schroeder sobre línea de retardo (delay modulable)
struct Allpass
{
    DelayLine dl;
    float g = 0.5f;

    inline float tick (float x, float d)
    {
        const float vd = dl.read (d);
        const float v = x + g * vd;
        dl.write (v);
        return vd - g * v;
    }
};

// ----------------------------------------------------------------------------
//  Parámetros del reverb
// ----------------------------------------------------------------------------
struct Params
{
    float mix = 0.20f;            // 0..1   (equal-power)
    float decaySec = 1.9f;        // RT60 en medios, 0.2..15 s
    float preDelayMs = 22.0f;     // 0..250 ms
    float size = 0.9f;            // 0.5..2  (escala delays del tanque y las ER)
    float damping = 0.50f;        // 0..1    (cuánto decaen más rápido los agudos)
    float bass = 0.55f;           // 0.25..1.5 (RT de graves = decay * bass)
    float diffusion = 0.85f;      // 0..1
    float early = 0.35f;          // 0..1    (nivel de reflexiones tempranas)
    float modDepth = 0.30f;       // 0..1
    float modRate = 0.6f;         // Hz
    float width = 1.0f;           // 0..2
    float lowCutHz = 180.0f;      // HPF del send
    float highCutHz = 10000.0f;   // LPF del send
    float duck = 0.4f;            // 0..1    (cuánto se aparta la cola con la voz)
    float duckReleaseMs = 250.0f; // 30..1000 ms
    float deEss = 0.35f;          // 0..1    (de-esser en el send)
};

// ----------------------------------------------------------------------------
//  El reverb
// ----------------------------------------------------------------------------
class VocalReverb
{
public:
    static constexpr int kLines = 16;
    static constexpr int kTaps = 8;
    static constexpr int kBlock = 32;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        invTauFast = 1.0f / (0.040f * fs);
        invTauSlow = 1.0f / (0.200f * fs);

        const int modMargin = (int) (0.0012f * fs) + 8;
        for (int i = 0; i < kLines; ++i)
            fdn[i].prepare ((int) (kBaseMs[kLines - 1] * 0.001f * fs * kMaxSize) + modMargin);  // margen de sobra (size^0.8 < size)
        maxFdnDelay = kBaseMs[kLines - 1] * 0.001f * fs * kMaxSize + (float) modMargin - 4.0f;

        preL.prepare ((int) (0.25f * fs) + 16);
        preR.prepare ((int) (0.25f * fs) + 16);
        erLine.prepare ((int) (kErMsR[kTaps - 1] * 0.001f * fs * kMaxSize) + 16);

        for (int k = 0; k < kTaps; ++k)
        {
            erTapL[k] = kErMsL[k] * 0.001f * fs;
            erTapR[k] = kErMsR[k] * 0.001f * fs;
        }
        for (int k = 0; k < 4; ++k)
        {
            apBaseL[k] = kApMsL[k] * 0.001f * fs;
            apBaseR[k] = kApMsR[k] * 0.001f * fs;
            apL[k].dl.prepare ((int) apBaseL[k] + modMargin);
            apR[k].dl.prepare ((int) apBaseR[k] + modMargin);
        }

        aLow = 1.0f - std::exp (-kTwoPi * 250.0f / fs);   // cruce de graves del tanque
        erA = 1.0f - std::exp (-kTwoPi * 5500.0f / fs);   // absorción de aire en las ER

        xoL.set (5000.0f, fs);
        xoR.set (5000.0f, fs);
        fHi.setTimes (0.5f, 25.0f, fs);
        fFull.setTimes (0.5f, 25.0f, fs);

        for (int i = 0; i < kLines; ++i)
        {
            lfoMult[i] = 0.6f + 0.9f * (float) ((i * 11) % kLines) / (float) (kLines - 1);
            phase[i] = std::fmod ((float) i * 0.381966f, 1.0f);
            injSign[i] = parity ((unsigned) (i >> 1)) ? -1.0f : 1.0f;
            sgnL[i] = parity ((unsigned) (i & 5)) ? -1.0f : 1.0f;
            sgnR[i] = parity ((unsigned) (i & 10)) ? -1.0f : 1.0f;
        }

        primed = false;
        snapRamps = true;
        reset();
    }

    void reset()
    {
        for (auto& d : fdn) d.clear();
        preL.clear(); preR.clear(); erLine.clear();
        for (int k = 0; k < 4; ++k) { apL[k].dl.clear(); apR[k].dl.clear(); }
        for (int i = 0; i < kLines; ++i) { bassLp[i] = 0; hfLp[i] = 0; }
        erLpL = erLpR = 0;
        hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset(); xoL.reset(); xoR.reset();
        fHi.env = fFull.env = fDuck.env = 0;
        duckGainSm = 1.0f;
        deGainSm = 1.0f;
    }

    void setParams (const Params& p)
    {
        const float v[kNumP] = {
            clampf (p.mix, 0, 1),           clampf (p.decaySec, 0.1f, 20.0f),
            clampf (p.preDelayMs, 0, 250),  clampf (p.size, 0.5f, kMaxSize),
            clampf (p.damping, 0, 1),       clampf (p.bass, 0.2f, 2.0f),
            clampf (p.diffusion, 0, 1),     clampf (p.early, 0, 1),
            clampf (p.modDepth, 0, 1),      clampf (p.modRate, 0.05f, 6.0f),
            clampf (p.width, 0, 2),         clampf (p.lowCutHz, 10, 2000),
            clampf (p.highCutHz, 1000, 20000), clampf (p.duck, 0, 1),
            clampf (p.deEss, 0, 1)
        };
        for (int i = 0; i < kNumP; ++i)
        {
            sm[i].target = v[i];
            if (! primed) sm[i].cur = v[i];
        }
        primed = true;
        duckRelMs = clampf (p.duckReleaseMs, 10, 3000);
    }

    // Procesa in-place. L y R deben apuntar a numSamples muestras cada uno.
    void process (float* L, float* R, int numSamples)
    {
        int pos = 0;
        while (pos < numSamples)
        {
            const int len = std::min (kBlock, numSamples - pos);
            updateControl (len);
            for (int n = 0; n < len; ++n)
                tick (L[pos + n], R[pos + n]);
            pos += len;
        }
    }

private:
    enum { pMix, pDecay, pPre, pSize, pDamp, pBass, pDiff, pEarly, pModD, pModR,
           pWidth, pLow, pHigh, pDuck, pDeEss, kNumP };

    static constexpr float kMaxSize = 2.0f;
    static constexpr float kAntiDenormal = 1.0e-18f;
    static constexpr float kLateTrim = 1.95f;  // calibrado con Tests/dsp_test.cpp (wet ~ -3 dB re entrada a 2 s)
    static constexpr float kRtCal = 1.05f;     // compensa la pérdida de HF de la interpolación (RT60 medido = pedido)
    static constexpr float kSizeExp = 0.8f;    // el tanque crece menos que linealmente con size (arranque más denso)

    // Longitudes del tanque (ms @ size=1): espaciadas de forma irregular.
    static constexpr float kBaseMs[kLines] = { 29.7f, 33.1f, 36.9f, 41.3f, 44.9f, 49.7f, 53.9f, 58.3f,
                                               62.9f, 67.7f, 72.1f, 77.3f, 81.7f, 86.9f, 91.1f, 96.7f };
    // Reflexiones tempranas
    static constexpr float kErMsL[kTaps] = { 5.3f, 9.7f, 14.9f, 19.3f, 25.1f, 31.7f, 38.3f, 46.9f };
    static constexpr float kErMsR[kTaps] = { 6.9f, 11.3f, 16.1f, 22.7f, 28.3f, 34.9f, 42.1f, 51.3f };
    static constexpr float kErGL[kTaps] = { 0.80f, -0.64f, 0.57f, -0.47f, 0.40f, -0.32f, 0.26f, -0.21f };
    static constexpr float kErGR[kTaps] = { -0.74f, 0.66f, -0.52f, 0.45f, -0.38f, 0.31f, -0.25f, 0.20f };
    // Difusores de entrada
    static constexpr float kApMsL[4] = { 4.77f, 3.60f, 12.73f, 9.31f };
    static constexpr float kApMsR[4] = { 5.19f, 3.13f, 13.61f, 8.47f };

    // ------------------------------------------------------------------ control
    void updateControl (int len)
    {
        const float cF = 1.0f - std::exp (-(float) len * invTauFast);
        const float cS = 1.0f - std::exp (-(float) len * invTauSlow);
        for (int i = 0; i < kNumP; ++i)
            sm[i].cur += (sm[i].target - sm[i].cur) * ((i == pPre || i == pSize) ? cS : cF);

        const float mix = sm[pMix].cur;
        const float rt = sm[pDecay].cur * kRtCal;
        const float size = sm[pSize].cur;

        setRamp (mixDry, std::cos (mix * kPi * 0.5f), len);
        setRamp (mixWet, std::sin (mix * kPi * 0.5f), len);
        setRamp (dPre, std::max (2.0f, sm[pPre].cur * 0.001f * fs), len);
        setRamp (erScale, size, len);
        setRamp (earlyG, sm[pEarly].cur * 0.45f, len);
        // compensación suave de nivel: un decay más largo no debe "gritar" más
        setRamp (lateG, kLateTrim * std::pow (2.0f * kRtCal / rt, 0.6f), len);
        setRamp (widthG, sm[pWidth].cur, len);

        // --- absorción por línea (Jot): ganancia de medios + shelf de graves + LPF de agudos
        const float hfRatio = std::exp (-sm[pDamp].cur * 2.8f);   // 1 .. 0.06
        const float rtHF = rt * hfRatio;
        const float rtLow = rt * sm[pBass].cur;
        const float modBase = sm[pModR].cur / fs;
        const float sizeT = std::pow (size, kSizeExp);

        for (int i = 0; i < kLines; ++i)
        {
            const float dSamp = kBaseMs[i] * 0.001f * fs * sizeT;
            setRamp (dLine[i], dSamp, len);
            const float dSec = dSamp / fs;

            gMid[i] = std::exp (-kLn1000 * dSec / rt);
            const float gHF = std::exp (-kLn1000 * dSec / rtHF);
            const float r = gHF / gMid[i];                       // <= 1
            const float pole = (1.0f - r) / (1.0f + r);          // LPF 1 polo: ganancia en Nyquist = r
            poleP[i] = pole;
            poleQ[i] = 1.0f - pole;
            kLow[i] = std::exp (-kLn1000 * dSec * (1.0f / rtLow - 1.0f / rt)) - 1.0f;
            phaseInc[i] = modBase * lfoMult[i];
        }

        modAmp = sm[pModD].cur * 0.0009f * fs;
        apModAmp = sm[pModD].cur * 0.00025f * fs;
        for (int k = 0; k < 4; ++k) apInc[k] = modBase * kApMult[k];

        const float diff = sm[pDiff].cur;
        const float g1 = 0.30f + 0.45f * diff;
        const float g2 = g1 * 0.83f;
        for (int k = 0; k < 4; ++k)
        {
            apL[k].g = apR[k].g = (k < 2 ? g1 : g2);
        }

        // --- filtros del send
        hpL.set (sm[pLow].cur, fs);  hpR.set (sm[pLow].cur, fs);
        lpL.set (sm[pHigh].cur, fs); lpR.set (sm[pHigh].cur, fs);

        duckAmt = sm[pDuck].cur;
        deEssAmt = sm[pDeEss].cur;
        fDuck.setTimes (4.0f, duckRelMs, fs);

        snapRamps = false;
    }

    void setRamp (Ramp& r, float target, int len)
    {
        if (snapRamps) r.snap (target); else r.set (target, len);
    }

    // ------------------------------------------------------------------- audio
    inline void tick (float& inL, float& inR)
    {
        const float dryL = inL, dryR = inR;

        // 1) acondicionamiento del send
        hpL.tick (dryL);  hpR.tick (dryR);
        lpL.tick (hpL.hp); lpR.tick (hpR.hp);
        float sL = lpL.lp, sR = lpR.lp;

        // 2) de-esser relativo: detecta cuánta energía > 5 kHz hay respecto al total
        xoL.tick (sL); xoR.tick (sR);
        const float hiL = sL - xoL.lp, hiR = sR - xoR.lp;
        const float eH = fHi.tick (std::max (std::fabs (hiL), std::fabs (hiR)));
        const float eF = fFull.tick (std::max (std::fabs (sL), std::fabs (sR)));
        const float ratio = eH / (eF + 1.0e-5f);
        const float sib = clampf ((ratio - 0.35f) * 2.5f, 0.0f, 1.0f);
        const float gDeTarget = 1.0f - deEssAmt * 0.8f * sib;
        deGainSm += 0.02f * (gDeTarget - deGainSm);
        sL = xoL.lp + hiL * deGainSm;
        sR = xoR.lp + hiR * deGainSm;

        // 3) pre-delay
        const float pd = dPre.next();
        const float pL = preL.read (pd);
        const float pR = preR.read (pd);
        preL.write (sL);
        preR.write (sR);

        // 4) reflexiones tempranas
        const float sc = erScale.next();
        float eL = 0, eR = 0;
        for (int k = 0; k < kTaps; ++k)
        {
            eL += kErGL[k] * erLine.read (erTapL[k] * sc);
            eR += kErGR[k] * erLine.read (erTapR[k] * sc);
        }
        erLine.write (0.5f * (pL + pR));
        erLpL += erA * (eL - erLpL);
        erLpR += erA * (eR - erLpR);

        // 5) difusión de entrada (los 2 all-pass largos van modulados)
        float xL = pL, xR = pR;
        for (int k = 0; k < 4; ++k)
        {
            float dL = apBaseL[k], dR = apBaseR[k];
            if (k >= 2)
            {
                dL += apModAmp * lfoSin (apPhase[k]);
                dR += apModAmp * lfoSin (apPhase[k] + 0.37f - std::floor (apPhase[k] + 0.37f));
                apPhase[k] += apInc[k];
                if (apPhase[k] >= 1.0f) apPhase[k] -= 1.0f;
            }
            xL = apL[k].tick (xL, dL);
            xR = apR[k].tick (xR, dR);
        }

        // 6) tanque FDN
        float y[kLines];
        float sum = 0;
        for (int i = 0; i < kLines; ++i)
        {
            float d = dLine[i].next() + modAmp * lfoSin (phase[i]);
            phase[i] += phaseInc[i];
            if (phase[i] >= 1.0f) phase[i] -= 1.0f;
            d = clampf (d, 4.0f, maxFdnDelay);

            float v = fdn[i].read (d);
            bassLp[i] += aLow * (v - bassLp[i]);
            v += kLow[i] * bassLp[i];                       // shelf de graves (<= 0 => graves decaen antes)
            hfLp[i] = poleQ[i] * v + poleP[i] * hfLp[i];    // damping de agudos
            v = hfLp[i] * gMid[i];                          // ganancia de medios (RT60)
            y[i] = v;
            sum += v;
        }
        const float k2 = sum * (2.0f / (float) kLines);     // Householder: A = I - (2/N) 1 1^T
        float oL = 0, oR = 0;
        for (int i = 0; i < kLines; ++i)
        {
            const float inj = ((i & 1) ? xR : xL) * injSign[i] * 0.35355339f;
            fdn[i].write (inj + y[i] - k2 + kAntiDenormal);
            oL += sgnL[i] * y[i];
            oR += sgnR[i] * y[i];
        }
        oL *= 0.25f;
        oR *= 0.25f;

        // 7) ducking: la cola se aparta mientras la voz suena
        const float env = fDuck.tick (std::max (std::fabs (dryL), std::fabs (dryR)));
        const float t = clampf ((env - 0.0158f) / (0.16f - 0.0158f), 0.0f, 1.0f);   // -36 dB .. -16 dB
        const float depth = t * t * (3.0f - 2.0f * t);
        const float duckTarget = 1.0f - 0.97f * duckAmt * depth;   // como máximo ~ -30 dB
        duckGainSm += 0.01f * (duckTarget - duckGainSm);

        // 8) suma, ancho M/S y mezcla
        const float eg = earlyG.next(), lg = lateG.next() * duckGainSm;
        float wL = erLpL * eg + oL * lg;
        float wR = erLpR * eg + oR * lg;
        const float m = 0.5f * (wL + wR);
        const float s = 0.5f * (wL - wR) * widthG.next();
        wL = m + s;
        wR = m - s;

        const float dG = mixDry.next(), wG = mixWet.next();
        inL = dryL * dG + wL * wG;
        inR = dryR * dG + wR * wG;
    }

    static constexpr float kApMult[4] = { 0.83f, 1.17f, 1.01f, 0.77f };

    // ------------------------------------------------------------------ estado
    float fs = 48000.0f;
    bool primed = false, snapRamps = true;
    Smooth sm[kNumP];
    float duckRelMs = 280.0f, invTauFast = 0.0f, invTauSlow = 0.0f;

    Ramp dPre, erScale, mixDry, mixWet, earlyG, lateG, widthG;
    Ramp dLine[kLines];

    float gMid[kLines] {}, poleP[kLines] {}, poleQ[kLines] {}, kLow[kLines] {}, phaseInc[kLines] {};
    float bassLp[kLines] {}, hfLp[kLines] {}, phase[kLines] {};
    float lfoMult[kLines] {}, injSign[kLines] {}, sgnL[kLines] {}, sgnR[kLines] {};
    float aLow = 0, erA = 0, modAmp = 0, apModAmp = 0, maxFdnDelay = 0;
    float apPhase[4] = { 0.10f, 0.35f, 0.60f, 0.85f };
    float apInc[4] {}, apBaseL[4] {}, apBaseR[4] {};
    float erTapL[kTaps] {}, erTapR[kTaps] {};
    float erLpL = 0, erLpR = 0;
    float duckAmt = 0, deEssAmt = 0, duckGainSm = 1.0f, deGainSm = 1.0f;

    SVF hpL, hpR, lpL, lpR, xoL, xoR;
    Follower fHi, fFull, fDuck;
    DelayLine preL, preR, erLine;
    DelayLine fdn[kLines];
    Allpass apL[4], apR[4];
};

} // namespace vr
