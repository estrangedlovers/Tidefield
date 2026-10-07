#include "AutoMaster.h"

#include "../core/Denormal.h"
#include "../core/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace tf::dsp {

namespace {

// A warm ambient balance, in dB relative to the 500 Hz-4 kHz band (pink noise sits
// at about 0, -2.4 and -1.1 here). Slightly darker on top, a little less mud.
constexpr float kTargetLow = 0.0f;
constexpr float kTargetMud = -3.0f;
constexpr float kTargetHigh = -3.5f;
constexpr float kTargetSideRatio = 0.4f; // side/mid energy of a wide but solid mix
constexpr float kSilenceLufs = -55.0f;

float toDb(double energy) noexcept { return 10.0f * static_cast<float>(std::log10(std::max(1.0e-12, energy))); }

} // namespace

void AutoMaster::prepare(const ProcessSpec& s)
{
    spec = s;
    fs = static_cast<float>(spec.sampleRate);
    for (auto* b : { &kShelfL, &kShelfR, &kHpL, &kHpR, &lowSplit, &highSplit, &mudLo, &mudHi, &eqLowL, &eqLowR, &eqMudL, &eqMudR, &eqHighL, &eqHighR })
        b->prepare(spec.sampleRate);
    // ITU-R BS.1770 K-weighting, approximated with RBJ shapes.
    kShelfL.setHighShelf(1500.0f, 4.0f);
    kShelfR.setHighShelf(1500.0f, 4.0f);
    kHpL.setHighPass(38.0f, 0.5f);
    kHpR.setHighPass(38.0f, 0.5f);
    lowSplit.setLowPass(150.0f);
    highSplit.setHighPass(4000.0f);
    mudLo.setHighPass(150.0f);
    mudHi.setLowPass(500.0f);
    sideLowCut.prepare(spec.sampleRate);
    sideLowCut.setCutoff(120.0f);
    reset();
}

void AutoMaster::reset() noexcept
{
    for (auto* b : { &kShelfL, &kShelfR, &kHpL, &kHpR, &lowSplit, &highSplit, &mudLo, &mudHi, &eqLowL, &eqLowR, &eqMudL, &eqMudR, &eqHighL, &eqHighR })
        b->reset();
    sideLowCut.reset();
    accK = accLow = accMud = accMid = accHigh = accSide = accMidSig = 0.0;
    accCount = 0;
    eK = eLow = eMud = eMidBand = eHigh = eSide = eMidSig = 0.0f;
    compEnv = 0.0f;
    compGain = gainLin = gainTarget = widthCur = 1.0f;
    state = {};
    state.mix = mix;
    appliedLow = appliedMud = appliedHigh = 99.0f;
    untilControl = 0;
}

void AutoMaster::control() noexcept
{
    const float n = static_cast<float>(std::max(1, accCount));
    const float dt = n / fs;
    // ~3 s windows for balance and loudness.
    const float k = 1.0f - std::exp(-dt / 3.0f);
    auto follow = [&](float& e, double acc) { e = flushDenormal(e + k * (static_cast<float>(acc / n) - e)); };
    follow(eK, accK);
    follow(eLow, accLow);
    follow(eMud, accMud);
    follow(eMidBand, accMid);
    follow(eHigh, accHigh);
    follow(eSide, accSide);
    follow(eMidSig, accMidSig);
    accK = accLow = accMud = accMid = accHigh = accSide = accMidSig = 0.0;
    accCount = 0;

    state.loudness = -0.691f + toDb(eK);
    const bool audible = state.loudness > kSilenceLufs;
    const float amount = std::clamp(params.amount, 0.0f, 1.0f);

    // Tonal balance: move each band toward the target relative to the mid band.
    // Corrections ease over ~4 s and only while there is something to measure.
    if (audible)
    {
        const float mid = toDb(eMidBand);
        auto correction = [&](float bandDb, float target) {
            return std::clamp((target - (bandDb - mid)) * amount, -6.0f, 4.0f);
        };
        const float s = 1.0f - std::exp(-dt / 4.0f);
        state.lowDb += s * (correction(toDb(eLow), kTargetLow) - state.lowDb);
        state.mudDb += s * (correction(toDb(eMud), kTargetMud) - state.mudDb);
        state.highDb += s * (correction(toDb(eHigh), kTargetHigh) - state.highDb);

        // Width toward a natural side/mid ratio.
        const float ratio = eSide / std::max(1.0e-9f, eMidSig);
        const float wanted = std::clamp(std::sqrt(kTargetSideRatio / std::max(1.0e-4f, ratio)), 0.7f, 1.4f);
        state.width += s * (1.0f + (wanted - 1.0f) * amount - state.width);

        // Make-up toward the loudness target (measured before make-up).
        const float wantDb = std::clamp(params.targetLufs - state.loudness, -12.0f, 12.0f);
        const float g = 1.0f - std::exp(-dt / 3.0f);
        state.gainDb += g * (wantDb - state.gainDb);
    }
    gainTarget = dbToGain(state.gainDb);

    // Re-derive EQ coefficients only when a gain moved audibly.
    if (std::fabs(state.lowDb - appliedLow) > 0.05f)
    {
        eqLowL.setLowShelf(150.0f, state.lowDb);
        eqLowR.setLowShelf(150.0f, state.lowDb);
        appliedLow = state.lowDb;
    }
    if (std::fabs(state.mudDb - appliedMud) > 0.05f)
    {
        eqMudL.setPeak(350.0f, 0.8f, state.mudDb);
        eqMudR.setPeak(350.0f, 0.8f, state.mudDb);
        appliedMud = state.mudDb;
    }
    if (std::fabs(state.highDb - appliedHigh) > 0.05f)
    {
        eqHighL.setHighShelf(4000.0f, state.highDb);
        eqHighR.setHighShelf(4000.0f, state.highDb);
        appliedHigh = state.highDb;
    }
}

void AutoMaster::process(float* left, float* right, int n) noexcept
{
    const float mixStep = 1.0f / (0.5f * fs);
    const float mixTarget = params.enabled ? 1.0f : 0.0f;
    if (mix == 0.0f && mixTarget == 0.0f)
    {
        state.mix = 0.0f;
        return; // fully bypassed: no cost, and no state drifting while off
    }

    const float attack = std::exp(-1.0f / (0.025f * fs));
    const float release = std::exp(-1.0f / (0.25f * fs));
    const float gainGlide = 1.0f - std::exp(-1.0f / (0.05f * fs));
    constexpr float kRatio = 1.6f;

    for (int i = 0; i < n; ++i)
    {
        if (--untilControl <= 0)
        {
            control();
            untilControl = kControlInterval;
        }
        const float dryL = left[i];
        const float dryR = right[i];

        // EQ.
        float l = eqHighL.process(eqMudL.process(eqLowL.process(dryL)));
        float r = eqHighR.process(eqMudR.process(eqLowR.process(dryR)));

        // Analysis of the corrected signal (balance, stereo) and of the input bands.
        const float m = 0.5f * (dryL + dryR);
        const float lo = lowSplit.process(m);
        const float hi = highSplit.process(m);
        const float mud = mudHi.process(mudLo.process(m));
        accLow += static_cast<double>(lo) * lo;
        accHigh += static_cast<double>(hi) * hi;
        accMud += static_cast<double>(mud) * mud;
        const float midBand = m - lo - hi - mud;
        accMid += static_cast<double>(midBand) * midBand;
        const float sd = 0.5f * (dryL - dryR);
        accSide += static_cast<double>(sd) * sd;
        accMidSig += static_cast<double>(m) * m;

        // Width, with the lows kept mono.
        widthCur += 0.0005f * (state.width - widthCur);
        const float mm = 0.5f * (l + r);
        const float ss = sideLowCut.processHigh(0.5f * (l - r)) * widthCur;
        l = mm + ss;
        r = mm - ss;

        // Glue: RMS envelope, soft knee, threshold 8 dB over the measured loudness.
        const float e = 0.5f * (l * l + r * r);
        compEnv = flushDenormal(e > compEnv ? attack * compEnv + (1.0f - attack) * e : release * compEnv + (1.0f - release) * e);
        const float levelDb = 10.0f * std::log10(compEnv + 1.0e-12f);
        const float threshold = state.loudness + 8.0f;
        const float over = levelDb - threshold;
        float grDb = 0.0f;
        if (over > -3.0f) // 6 dB soft knee
        {
            const float x = over < 3.0f ? (over + 3.0f) * (over + 3.0f) / 12.0f : over;
            grDb = x * (1.0f - 1.0f / kRatio);
        }
        compGain = dbToGain(-grDb);
        state.reductionDb = grDb;

        // Make-up (glides) and loudness measurement of the pre-make-up signal.
        gainLin += gainGlide * (gainTarget - gainLin);
        l *= compGain;
        r *= compGain;
        const float kl = kHpL.process(kShelfL.process(l));
        const float kr = kHpR.process(kShelfR.process(r));
        accK += static_cast<double>(kl) * kl + static_cast<double>(kr) * kr;
        ++accCount;
        l *= gainLin;
        r *= gainLin;

        mix = mixTarget > mix ? std::min(mixTarget, mix + mixStep) : std::max(mixTarget, mix - mixStep);
        left[i] = dryL + mix * (l - dryL);
        right[i] = dryR + mix * (r - dryR);
    }
    state.mix = mix;
}

} // namespace tf::dsp
