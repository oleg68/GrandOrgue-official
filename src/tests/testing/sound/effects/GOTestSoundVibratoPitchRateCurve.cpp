/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundVibratoPitchRateCurve.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/effects/GOSoundVibratoPitchRateCurve.h"

const std::string GOTestSoundVibratoPitchRateCurve::TEST_NAME
  = "GOTestSoundVibratoPitchRateCurve";

using Curve = GOSoundVibratoPitchRateCurve;

static constexpr unsigned UNIT_RATE = GOSoundResample::UPSAMPLE_FACTOR;
static constexpr unsigned TEST_SAMPLE_RATE = 48000;

static uint64_t sum_of(const std::vector<unsigned> &rates) {
  return std::accumulate(rates.begin(), rates.end(), (uint64_t)0);
}

static bool are_all_in_range(const unsigned *pRates, unsigned nRates) {
  bool res = true;

  for (unsigned rateI = 0; rateI < nRates; ++rateI)
    res = res && (int)pRates[rateI] >= Curve::MIN_RATE_UNITS
      && (int)pRates[rateI] <= Curve::MAX_RATE_UNITS;
  return res;
}

static std::vector<float> make_sine_cents(
  unsigned nFrames, float amplitudeCents, float periodFrames) {
  std::vector<float> cents(nFrames);

  for (unsigned frameI = 0; frameI < nFrames; ++frameI)
    cents[frameI] = amplitudeCents
      * std::sin(2.0f * (float)M_PI * (float)frameI / periodFrames);
  return cents;
}

void GOTestSoundVibratoPitchRateCurve::TestConstants() {
  static_assert(Curve::MIN_RATE_UNITS == (int)(UNIT_RATE / 8));
  static_assert(Curve::MAX_RATE_UNITS == (int)(UNIT_RATE * 8));
  static_assert(
    Curve::MIN_ALLOWED_RATE_UNITS
    == Curve::MIN_RATE_UNITS + Curve::RATE_MARGIN_UNITS);
  static_assert(
    Curve::MAX_ALLOWED_RATE_UNITS
    == Curve::MAX_RATE_UNITS - Curve::RATE_MARGIN_UNITS);
  GOAssert(Curve::NO_CURVE_ID == 0, "NO_CURVE_ID must be 0");
  GOAssert(
    Curve::MAX_PITCH_DEVIATION_CENTS == 2400.0f,
    "the cents limit must be two octaves");

  const Curve curve;

  GOAssert(curve.GetId() == Curve::NO_CURVE_ID, "a new curve has no id");
  GOAssert(
    curve.GetSampleRate() == 0 && curve.GetNFramesPerProcess() == 0
      && curve.GetNRateUnitsFrames() == 0,
    "the getters of a new curve return 0");
}

void GOTestSoundVibratoPitchRateCurve::TestApplyAffineTransform() {
  // A plain shift gives x + c exactly
  for (const int64_t shift : {5, -5, 0}) {
    std::vector<unsigned> rates = {8000, 8192, 9000};

    const int64_t total
      = Curve::applyAffineTransform(rates.data(), rates.size(), {1, 1, shift});

    GOAssert(
      rates[0] == 8000 + shift && rates[1] == 8192 + shift
        && rates[2] == 9000 + shift,
      "a plain shift must add the shift to every rate");
    GOAssert(
      (uint64_t)total == sum_of(rates), "the returned sum must be the new sum");
  }

  // 2000 / 2 = 1000 is below the range; 3001 / 2 = 1500.5, a half rounds up
  std::vector<unsigned> halves = {2000, 3001};

  Curve::applyAffineTransform(halves.data(), halves.size(), {1, 2, 0});
  GOAssert(halves[0] == (unsigned)Curve::MIN_RATE_UNITS, "clamped from below");
  GOAssert(halves[1] == 1501, "a half rounds up");

  // The clamp from above
  std::vector<unsigned> big = {60000};

  Curve::applyAffineTransform(big.data(), big.size(), {1, 1, 100000});
  GOAssert(big[0] == (unsigned)Curve::MAX_RATE_UNITS, "clamped from above");

  // A negative numerator is clamped to the minimum
  std::vector<unsigned> low = {5000};

  Curve::applyAffineTransform(low.data(), low.size(), {1, 1, -100000});
  GOAssert(low[0] == (unsigned)Curve::MIN_RATE_UNITS, "negative is clamped");

  // Compare with an independent computation on random coefficients
  std::mt19937 rng(1);

  for (unsigned iterI = 0; iterI < 500; ++iterI) {
    const int64_t kDen = 1 + rng() % 1000;
    const int64_t kNum = rng() % (kDen + 1);
    const int64_t cNum = (int64_t)(rng() % 2000000) - 1000000;
    std::vector<unsigned> rates(20);

    for (unsigned &rate : rates)
      rate = Curve::MIN_RATE_UNITS
        + rng() % (Curve::MAX_RATE_UNITS - Curve::MIN_RATE_UNITS + 1);

    const std::vector<unsigned> orig = rates;

    Curve::applyAffineTransform(rates.data(), rates.size(), {kNum, kDen, cNum});
    for (unsigned rateI = 0; rateI < rates.size(); ++rateI) {
      const long double exact
        = ((long double)kNum * orig[rateI] + cNum) / (long double)kDen;
      const long double rounded = std::floor(exact + 0.5L);
      const long double expected = std::clamp<long double>(
        rounded, Curve::MIN_RATE_UNITS, Curve::MAX_RATE_UNITS);

      GOAssert(
        (long double)rates[rateI] == expected,
        "applyAffineTransform must round to nearest and clamp");
    }
  }

  // An empty array
  unsigned nothing = 77;

  GOAssert(
    Curve::applyAffineTransform(&nothing, 0, {1, 1, 5}) == 0 && nothing == 77,
    "no rates: sum 0 and nothing changed");
}

void GOTestSoundVibratoPitchRateCurve::TestNormaliseRandom() {
  std::mt19937 rng(42);

  for (unsigned iterI = 0; iterI < 3000; ++iterI) {
    const unsigned len = 1 + rng() % 200;
    std::vector<unsigned> rates(len);

    for (unsigned &rate : rates)
      rate = Curve::MIN_RATE_UNITS
        + rng() % (Curve::MAX_RATE_UNITS - Curve::MIN_RATE_UNITS + 1);

    const uint64_t current = sum_of(rates);
    const uint64_t targetMean = Curve::MIN_ALLOWED_RATE_UNITS
      + rng()
        % (Curve::MAX_ALLOWED_RATE_UNITS - Curve::MIN_ALLOWED_RATE_UNITS + 1);
    const uint64_t target = targetMean * len + rng() % len;
    const Curve::AffineCoefficients coefficients
      = Curve::normalisePitchRateCurve(rates.data(), len, current, target);

    GOAssert(sum_of(rates) == target, "the sum must be exactly the target");
    GOAssert(
      are_all_in_range(rates.data(), len), "every rate must stay in the range");
    GOAssert(coefficients.kDen > 0, "kDen must be positive");
    GOAssert(coefficients.kNum <= coefficients.kDen, "k must not exceed 1");
    if (coefficients.kNum == coefficients.kDen)
      GOAssert(
        coefficients.kNum == 1
          && coefficients.cNum
            == (int64_t)(target / len) - (int64_t)(current / len),
        "a plain shift must have the shift of the means");
    else
      GOAssert(
        coefficients.cNum
          == coefficients.kDen * (int64_t)(target / len)
            - coefficients.kNum * (int64_t)(current / len),
        "pressing must have cNum = kDen * targetMean - kNum * currentMean");
  }
}

void GOTestSoundVibratoPitchRateCurve::TestNormalisePressed() {
  const unsigned len = 100;
  std::vector<unsigned> rates(len);

  // A square wave of 2048 / 32768: the mean is much higher than 8192
  for (unsigned rateI = 0; rateI < len; ++rateI)
    rates[rateI] = rateI % 2 ? 32768 : 2048;

  const std::vector<unsigned> orig = rates;
  const Curve::AffineCoefficients coefficients = Curve::normalisePitchRateCurve(
    rates.data(), len, sum_of(rates), (uint64_t)len * UNIT_RATE);

  GOAssert(sum_of(rates) == (uint64_t)len * UNIT_RATE, "the sum must be exact");
  GOAssert(are_all_in_range(rates.data(), len), "the rates must be in range");
  GOAssert(coefficients.kNum < coefficients.kDen, "the deviations are pressed");
  // The shape is preserved: the low cells stay below the high ones
  for (unsigned rateI = 0; rateI + 1 < len; rateI += 2)
    GOAssert(rates[rateI] < rates[rateI + 1], "the shape is kept");
  // The deviations are shrunk
  GOAssert(rates[1] - rates[0] < orig[1] - orig[0], "the depth must shrink");
}

void GOTestSoundVibratoPitchRateCurve::TestNormaliseExamples() {
  {
    std::vector<unsigned> rates(20, UNIT_RATE + 1);

    Curve::normalisePitchRateCurve(
      rates.data(), 20, sum_of(rates), (uint64_t)20 * UNIT_RATE);
    GOAssert(
      std::all_of(
        rates.begin(), rates.end(), [](unsigned r) { return r == UNIT_RATE; }),
      "20 rates of 8193 must become 8192");
  }
  for (const unsigned constant : {8249u, 16384u}) {
    std::vector<unsigned> rates(20, constant);

    Curve::normalisePitchRateCurve(
      rates.data(), 20, sum_of(rates), (uint64_t)20 * UNIT_RATE);
    GOAssert(
      std::all_of(
        rates.begin(), rates.end(), [](unsigned r) { return r == UNIT_RATE; }),
      "a constant curve must become all 8192");
  }
  {
    std::vector<unsigned> one = {UNIT_RATE + 1};

    Curve::normalisePitchRateCurve(one.data(), 1, one[0], UNIT_RATE);
    GOAssert(one[0] == UNIT_RATE, "a single rate must hit the target");

    std::vector<unsigned> two = {8000, 8001};

    Curve::normalisePitchRateCurve(two.data(), 2, 16001, 16385);
    GOAssert(sum_of(two) == 16385, "two rates must hit the target sum");
  }
}

void GOTestSoundVibratoPitchRateCurve::TestBuildLayout() {
  struct LayoutCase {
    unsigned loopBeginIndex;
    unsigned loopEndIndex;
    unsigned nFrames;
  };
  // A head and a loop; a loop only; a loop shorter than the tail
  const LayoutCase cases[] = {{10, 110, 16}, {0, 100, 16}, {3, 8, 16}};
  uint64_t prevId = Curve::NO_CURVE_ID;

  for (const LayoutCase &layoutCase : cases) {
    const unsigned loopBegin = layoutCase.loopBeginIndex;
    const unsigned loopEnd = layoutCase.loopEndIndex;
    const unsigned nFrames = layoutCase.nFrames;
    const unsigned nLoop = loopEnd - loopBegin;
    const std::vector<float> cents = make_sine_cents(loopEnd, 100.0f, 37.0f);
    Curve curve;

    curve.Build(cents.data(), TEST_SAMPLE_RATE, loopBegin, loopEnd, nFrames);

    GOAssert(curve.GetId() > prevId, "ids must strictly increase");
    prevId = curve.GetId();
    GOAssert(
      curve.GetSampleRate() == TEST_SAMPLE_RATE
        && curve.GetNFramesPerProcess() == nFrames,
      "the format must be recorded");
    GOAssert(
      curve.GetNRateUnitsFrames() == loopEnd + nFrames - 1,
      "the length is the loop end plus the tail");
    GOAssert(
      curve.m_LoopBeginIndex == loopBegin && curve.m_LoopEndIndex == loopEnd,
      "the head and loop bounds must be recorded");

    const unsigned *pRates = curve.mp_RateUnitsData.get();

    GOAssert(
      are_all_in_range(pRates, curve.GetNRateUnitsFrames()),
      "every rate must be in the range");

    const uint64_t loopSum
      = std::accumulate(pRates + loopBegin, pRates + loopEnd, (uint64_t)0);

    GOAssert(
      loopSum == (uint64_t)nLoop * UNIT_RATE,
      "the loop must average exactly 1.0");
    for (unsigned tailI = 0; tailI + 1 < nFrames; ++tailI)
      GOAssert(
        pRates[loopEnd + tailI] == pRates[loopBegin + tailI % nLoop],
        "the tail must repeat the start of the loop (tiled)");
  }

  Curve zero;
  const std::vector<float> zeroCents(50, 0.0f);

  zero.Build(zeroCents.data(), TEST_SAMPLE_RATE, 0, 50, 8);
  for (unsigned rateI = 0; rateI < zero.GetNRateUnitsFrames(); ++rateI)
    GOAssert(
      zero.mp_RateUnitsData[rateI] == UNIT_RATE,
      "zero cents must give all the unit rates");
}

void GOTestSoundVibratoPitchRateCurve::TestBuildSeamOfDeepCurve() {
  const unsigned loopBegin = 40;
  const unsigned loopEnd = 140;
  std::vector<float> cents(loopEnd, 2400.0f);

  // The head and the first 30 frames of the loop are at +2400, the rest -2400
  std::fill(cents.begin() + loopBegin + 30, cents.end(), -2400.0f);

  Curve curve;

  curve.Build(cents.data(), TEST_SAMPLE_RATE, loopBegin, loopEnd, 16);

  const unsigned *pRates = curve.mp_RateUnitsData.get();
  const int seamJump = (int)pRates[loopBegin] - (int)pRates[loopBegin - 1];

  GOAssert(std::abs(seamJump) <= 2, "no jump at the seam of head and loop");
  GOAssert(
    are_all_in_range(pRates, curve.GetNRateUnitsFrames()),
    "a deep curve must stay in the range");
  GOAssert(
    pRates[loopBegin] > pRates[loopBegin + 50],
    "the shape must be preserved: high first, low later");
}

void GOTestSoundVibratoPitchRateCurve::TestBuildClampsCents() {
  const float nan = std::nanf("");
  const float inf = INFINITY;
  const std::vector<float> wild = {3000.0f, -3000.0f, nan, inf, -inf, 1.0f};
  const std::vector<float> clamped
    = {2400.0f, -2400.0f, 0.0f, 2400.0f, -2400.0f, 1.0f};
  Curve wildCurve;
  Curve clampedCurve;

  wildCurve.BuildInternal(wild.data(), TEST_SAMPLE_RATE, 0, 6, 4, false);
  clampedCurve.BuildInternal(clamped.data(), TEST_SAMPLE_RATE, 0, 6, 4, false);
  GOAssert(
    std::equal(
      wildCurve.mp_RateUnitsData.get(),
      wildCurve.mp_RateUnitsData.get() + wildCurve.GetNRateUnitsFrames(),
      clampedCurve.mp_RateUnitsData.get()),
    "out-of-range cents must be clamped, NaN is 0 cents");
}

void GOTestSoundVibratoPitchRateCurve::TestBuildInternalNotNormalised() {
  const std::vector<float> cents(10, 1200.0f);
  Curve curve;

  curve.BuildInternal(cents.data(), TEST_SAMPLE_RATE, 0, 10, 4, false);
  GOAssert(curve.GetId() != Curve::NO_CURVE_ID, "the curve must be built");
  GOAssert(curve.GetNRateUnitsFrames() == 13, "10 loop frames and 3 tail");
  for (unsigned rateI = 0; rateI < 13; ++rateI)
    GOAssert(
      curve.mp_RateUnitsData[rateI] == 2 * UNIT_RATE,
      "+1200 cents without normalisation must stay rate 2");

  const std::vector<float> down(10, -1200.0f);

  curve.BuildInternal(down.data(), TEST_SAMPLE_RATE, 2, 10, 4, false);
  for (unsigned rateI = 0; rateI < 13; ++rateI)
    GOAssert(
      curve.mp_RateUnitsData[rateI] == UNIT_RATE / 2,
      "-1200 cents (head and loop) must stay rate 0.5");
}

void GOTestSoundVibratoPitchRateCurve::TestDestroy() {
  Curve never;

  never.Destroy();
  GOAssert(never.GetId() == Curve::NO_CURVE_ID, "a never built curve is fine");

  const std::vector<float> cents(20, 0.0f);
  Curve curve;

  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, 20, 4);

  const uint64_t oldId = curve.GetId();

  curve.Destroy();
  curve.Destroy();
  GOAssert(
    curve.GetId() == Curve::NO_CURVE_ID && curve.GetNRateUnitsFrames() == 0
      && curve.GetSampleRate() == 0 && curve.GetNFramesPerProcess() == 0,
    "a destroyed curve is not built");
  GOAssert(
    !curve.mp_RateUnitsData && !curve.mp_ChunkSumUnits,
    "the arrays must be released");
  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, 20, 4);
  GOAssert(curve.GetId() > oldId, "a rebuild must get a fresh id");
}

void GOTestSoundVibratoPitchRateCurve::TestPositionSelectChunk() {
  const std::vector<float> cents = make_sine_cents(60, 200.0f, 23.0f);
  Curve curveA;
  Curve curveB;
  const Curve notBuilt;
  Curve::Position pos;

  curveA.Build(cents.data(), TEST_SAMPLE_RATE, 5, 60, 8);
  curveB.Build(cents.data(), TEST_SAMPLE_RATE, 5, 60, 8);

  Curve::ChunkDescription chunk = pos.SelectChunk(nullptr);

  GOAssert(
    !chunk.HasRates() && !chunk.pRateUnits && chunk.nTotalUnits == 0
      && !pos.IsBound(),
    "nullptr gives an empty chunk and an unbound position");

  chunk = pos.SelectChunk(&curveA);
  GOAssert(pos.IsBound() && chunk.HasRates(), "binding gives a chunk");
  GOAssert(
    chunk.pRateUnits == curveA.mp_RateUnitsData.get(),
    "the first chunk starts at the start of the data");
  GOAssert(
    chunk.nTotalUnits
      == std::accumulate(chunk.pRateUnits, chunk.pRateUnits + 8, 0u),
    "the chunk sum must be the sum of its rates");

  pos.Advance(8);
  chunk = pos.SelectChunk(&curveA);
  GOAssert(
    chunk.pRateUnits == curveA.mp_RateUnitsData.get() + 8,
    "the same curve must not reset the index");

  chunk = pos.SelectChunk(&curveB);
  GOAssert(
    chunk.pRateUnits == curveB.mp_RateUnitsData.get(),
    "another curve must reset the index to 0");

  pos.Advance(8);
  pos.SelectChunk(nullptr);
  GOAssert(!pos.IsBound(), "nullptr must unbind");
  chunk = pos.SelectChunk(&curveB);
  GOAssert(
    chunk.pRateUnits == curveB.mp_RateUnitsData.get(),
    "binding after nullptr must start at 0");

  // A rebuilt curve at the same address has a new id: the index restarts
  pos.Advance(8);
  curveB.Build(cents.data(), TEST_SAMPLE_RATE, 5, 60, 8);
  chunk = pos.SelectChunk(&curveB);
  GOAssert(
    chunk.pRateUnits == curveB.mp_RateUnitsData.get(),
    "a rebuilt curve must restart the index");

  // A curve that is not built is no curve
  chunk = pos.SelectChunk(&notBuilt);
  GOAssert(
    !chunk.HasRates() && !pos.IsBound(),
    "a not built curve must be treated as no curve");
}

void GOTestSoundVibratoPitchRateCurve::TestChunkSums() {
  struct SumCase {
    unsigned loopBeginIndex;
    unsigned loopEndIndex;
    unsigned nFrames;
    bool isToNormalise;
  };
  // A head; a loop shorter than a chunk; one frame per round; not normalised
  const SumCase cases[]
    = {{7, 37, 8, true}, {2, 5, 8, true}, {0, 20, 1, true}, {4, 30, 6, false}};

  for (const SumCase &sumCase : cases) {
    const unsigned nFrames = sumCase.nFrames;
    const unsigned loopEnd = sumCase.loopEndIndex;
    const std::vector<float> cents = make_sine_cents(loopEnd, 300.0f, 11.0f);
    Curve curve;

    curve.BuildInternal(
      cents.data(),
      TEST_SAMPLE_RATE,
      sumCase.loopBeginIndex,
      loopEnd,
      nFrames,
      sumCase.isToNormalise);

    Curve::Position pos;

    for (unsigned roundI = 0; roundI < 200; ++roundI) {
      const Curve::ChunkDescription chunk = pos.SelectChunk(&curve);

      GOAssert(
        chunk.pRateUnits == curve.mp_RateUnitsData.get() + pos.m_index,
        "the chunk must point at the cursor");
      GOAssert(
        chunk.nTotalUnits
          == std::accumulate(chunk.pRateUnits, chunk.pRateUnits + nFrames, 0u),
        "the precomputed sum must equal the direct one");
      pos.Advance(nFrames);
    }
    // Every start index, not only the reached ones
    for (unsigned indexI = 0; indexI < loopEnd; ++indexI) {
      const unsigned *pFirst = curve.mp_RateUnitsData.get() + indexI;

      GOAssert(
        curve.mp_ChunkSumUnits[indexI]
          == std::accumulate(pFirst, pFirst + nFrames, 0u),
        "the table must hold the direct sum at every start index");
    }
  }
}

void GOTestSoundVibratoPitchRateCurve::TestPositionAdvance() {
  struct AdvanceCase {
    unsigned loopBeginIndex;
    unsigned loopEndIndex;
    unsigned nFrames;
  };
  // A head; no head; a loop shorter than a round
  const AdvanceCase cases[] = {{5, 15, 4}, {0, 12, 5}, {3, 6, 8}};

  for (const AdvanceCase &advanceCase : cases) {
    const unsigned loopBegin = advanceCase.loopBeginIndex;
    const unsigned loopEnd = advanceCase.loopEndIndex;
    const unsigned nFrames = advanceCase.nFrames;
    const std::vector<float> cents(loopEnd, 0.0f);
    Curve curve;
    Curve::Position pos;

    curve.Build(cents.data(), TEST_SAMPLE_RATE, loopBegin, loopEnd, nFrames);
    pos.SelectChunk(&curve);
    for (unsigned roundI = 0; roundI < 100; ++roundI) {
      // The independent model: the unwrapped time, wrapped inside the loop
      const unsigned long time = (unsigned long)roundI * nFrames;
      const unsigned long expected = time < loopEnd
        ? time
        : loopBegin + (time - loopBegin) % (loopEnd - loopBegin);

      GOAssert(pos.m_index == expected, "Advance() must follow the model");
      pos.Advance(nFrames);
    }
  }
}

void GOTestSoundVibratoPitchRateCurve::run() {
  TestConstants();
  TestApplyAffineTransform();
  TestNormaliseRandom();
  TestNormalisePressed();
  TestNormaliseExamples();
  TestBuildLayout();
  TestBuildSeamOfDeepCurve();
  TestBuildClampsCents();
  TestBuildInternalNotNormalised();
  TestDestroy();
  TestPositionSelectChunk();
  TestChunkSums();
  TestPositionAdvance();
}
