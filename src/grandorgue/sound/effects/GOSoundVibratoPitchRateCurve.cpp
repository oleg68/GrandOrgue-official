/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundVibratoPitchRateCurve.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>

#include <wx/intl.h>
#include <wx/log.h>

// The last given curve id; Build() runs only on the control thread
static uint64_t last_curve_id = GOSoundVibratoPitchRateCurve::NO_CURVE_ID;

int64_t GOSoundVibratoPitchRateCurve::applyAffineTransform(
  unsigned *pRates, unsigned nRates, const AffineCoefficients &coefficients) {
  const int64_t kNum = coefficients.kNum;
  const int64_t kDen = coefficients.kDen;

  assert(kDen > 0);

  int64_t newTotalUnits = 0;
  // Half of the divisor is added to round to nearest by the integer division
  const int64_t cNumToRound = coefficients.cNum + kDen / 2;
  unsigned *pRate = pRates;

  for (unsigned nRatesLeft = nRates; nRatesLeft > 0; --nRatesLeft, ++pRate) {
    // The int64_t cast: kNum * unsigned would be computed in unsigned
    const int64_t newRate = std::clamp<int64_t>(
      (kNum * (int64_t)*pRate + cNumToRound) / kDen,
      MIN_RATE_UNITS,
      MAX_RATE_UNITS);

    *pRate = (unsigned)newRate;
    newTotalUnits += newRate;
  }
  return newTotalUnits;
}

GOSoundVibratoPitchRateCurve::AffineCoefficients GOSoundVibratoPitchRateCurve::
  normalisePitchRateCurve(
    unsigned *pUnitCurve,
    unsigned curveLen,
    uint64_t currentTotalUnits,
    uint64_t targetTotalUnits) {
  assert(curveLen > 0);
  assert(
    currentTotalUnits < ((uint64_t)1 << 63)
    && targetTotalUnits < ((uint64_t)1 << 63));
  assert(
    std::accumulate(pUnitCurve, pUnitCurve + curveLen, (uint64_t)0)
    == currentTotalUnits);

  // Floor of the target mean; the rounding remainder is spread at the end
  const int targetMean = (int)(targetTotalUnits / curveLen);

  /* The target mean must be at least RATE_MARGIN_UNITS away from both bounds:
   * else the remainder step could push a cell out of the range even when all
   * the deviations are pressed to zero (k == 0). Curves of the processor are
   * normally far from the bounds, so a failure means a pathological curve. */
  assert(targetMean >= MIN_ALLOWED_RATE_UNITS);
  assert(targetMean <= MAX_ALLOWED_RATE_UNITS);

  // Floor of the current mean
  const int currentMean = (int)(currentTotalUnits / curveLen);
  const int meanShift = targetMean - currentMean;
  // A plain shift; the pressing branch changes it
  AffineCoefficients resCoefficients = {1, 1, meanShift};
  const auto [pMin, pMax]
    = std::minmax_element(pUnitCurve, pUnitCurve + curveLen);
  const int minCurrentRate = (int)*pMin;
  const int maxCurrentRate = (int)*pMax;

  assert(minCurrentRate >= MIN_RATE_UNITS && maxCurrentRate <= MAX_RATE_UNITS);

  /* A plain shift of every cell by meanShift is enough if both extreme cells
   * stay within the range narrowed by RATE_MARGIN_UNITS (the remainder is below
   * 1.5 * curveLen, so every cell gets at most 1 (the common part) + 1 (the
   * first cells) more). Otherwise the deviations are pressed. */
  const bool isShiftFit = minCurrentRate + meanShift >= MIN_ALLOWED_RATE_UNITS
    && maxCurrentRate + meanShift <= MAX_ALLOWED_RATE_UNITS;

  // What is still to be added to every cell, and to the sum, in the final step
  int pendingShift;
  int64_t nUnitsLeft;

  if (isShiftFit) {
    /* The good and usual case: a plain linear shift of every cell is enough.
     * Nothing is written yet, the shift is done in the final step. */
    pendingShift = meanShift;
    // The remainders of the sums by curveLen: what the whole-frame shift
    // cannot do
    nUnitsLeft = (int64_t)(targetTotalUnits % curveLen)
      - (int64_t)(currentTotalUnits % curveLen);
  } else {
    /* The bad and rare case: a linear shift would take an extreme cell out of
     * the range. First press the curve toward its current mean (scale the
     * deviations from it by k < 1), then shift it onto the target mean. Both
     * are done in one pass here. */

    // How far the deviations from the target mean may go down and up
    const int nUnitsRoomDown = targetMean - MIN_ALLOWED_RATE_UNITS;
    const int nUnitsRoomUp = MAX_ALLOWED_RATE_UNITS - targetMean;
    // The deviations of the extreme cells from the current mean
    const int minCurrentDeviation = minCurrentRate - currentMean;
    const int maxCurrentDeviation = maxCurrentRate - currentMean;

    assert(nUnitsRoomDown >= 0 && nUnitsRoomUp >= 0);
    // An integer minimum cannot exceed the floor of the mean, nor the maximum
    // fall below it
    assert(minCurrentDeviation <= 0 && maxCurrentDeviation >= 0);

    /* The pressing factor k = resCoefficients.kNum / resCoefficients.kDen, as
     * a fraction. It is the smaller of kDownNum / kDownDen (the lowest cell
     * must stay in the range) and kUpNum / kUpDen (the highest cell must stay
     * in the range), i.e. the stronger pressing. The fractions are compared,
     * not divided. A fraction 1/1 means no limit from that side. int64_t: the
     * products do not fit an int. */
    int64_t kDownNum = 1;
    int64_t kDownDen = 1;
    int64_t kUpNum = 1;
    int64_t kUpDen = 1;

    // The lowest cell: targetMean + k * minCurrentDeviation >= MIN_ALLOWED
    if (nUnitsRoomDown < -minCurrentDeviation) {
      kDownNum = nUnitsRoomDown;
      kDownDen = -minCurrentDeviation;
    }
    // The highest cell: targetMean + k * maxCurrentDeviation <= MAX_ALLOWED
    if (nUnitsRoomUp < maxCurrentDeviation) {
      kUpNum = nUnitsRoomUp;
      kUpDen = maxCurrentDeviation;
    }
    /* Compare kDownNum / kDownDen with kUpNum / kUpDen and take the smaller
     * one. Both denominators are positive, so a / b <= c / d is exactly
     * a * d <= c * b, with no division. */
    if (kDownNum * kUpDen <= kUpNum * kDownDen) {
      resCoefficients.kNum = kDownNum;
      resCoefficients.kDen = kDownDen;
    } else {
      resCoefficients.kNum = kUpNum;
      resCoefficients.kDen = kUpDen;
    }
    // A plain shift did not fit, so at least one extreme cell limits k
    assert(resCoefficients.kNum < resCoefficients.kDen);

    /* y = targetMean + k * (x - currentMean) = (kNum * x + cNum) / kDen. The
     * extreme cell that limits k lands exactly on a bound of the narrowed
     * range, with no rounding. */
    resCoefficients.cNum
      = resCoefficients.kDen * targetMean - resCoefficients.kNum * currentMean;

    /* Press all the cells toward currentMean and put them around targetMean.
     * The main shift is done by the transform; count in nUnitsLeft what is
     * still missing from the target sum. */
    pendingShift = 0;
    nUnitsLeft = (int64_t)targetTotalUnits
      - applyAffineTransform(pUnitCurve, curveLen, resCoefficients);
  }

  /* The final step: spread the remainder nUnitsLeft (|nUnitsLeft| < 1.5 *
   * curveLen) over the cells in two vectorizable passes. Every cell gets the
   * common part (plus the main shift pendingShift for a plain shift), and
   * the first nExtraCells cells one more unit of the sign of nUnitsLeft. */
  assert(std::abs(nUnitsLeft) / curveLen < 2);

  // nUnitsLeft / curveLen is 0 for a plain shift (|nUnitsLeft| < curveLen)
  const int commonDelta = pendingShift + (int)(nUnitsLeft / curveLen);
  const unsigned nExtraCells = (unsigned)(std::abs(nUnitsLeft) % curveLen);
  const int extraDelta = commonDelta + (nUnitsLeft > 0 ? 1 : -1);
  // The rates are far below 2^31, so they can be handled as signed ints
  int *pIntCurve = (int *)pUnitCurve;
  int *pExtraEnd = pIntCurve + nExtraCells;
  int *pCurveEnd = pIntCurve + curveLen;

  // The first nExtraCells cells are increased by extraDelta, the rest by
  // commonDelta
  if (pIntCurve < pExtraEnd && extraDelta != 0)
    std::transform(pIntCurve, pExtraEnd, pIntCurve, [extraDelta](int u) {
      return u + extraDelta;
    });
  if (pExtraEnd < pCurveEnd && commonDelta != 0)
    std::transform(pExtraEnd, pCurveEnd, pExtraEnd, [commonDelta](int u) {
      return u + commonDelta;
    });
  assert(
    std::accumulate(pUnitCurve, pUnitCurve + curveLen, (uint64_t)0)
    == targetTotalUnits);
  return resCoefficients;
}

void GOSoundVibratoPitchRateCurve::BuildInternal(
  const float *pCentsData,
  unsigned sampleRate,
  unsigned loopBeginIndex,
  unsigned loopEndIndex,
  unsigned nFramesPerProcess,
  bool isToNormalise) {
  assert(loopEndIndex > loopBeginIndex);
  assert(nFramesPerProcess > 0);

  const unsigned nLoopFrames = loopEndIndex - loopBeginIndex;
  const unsigned nTailFrames = nFramesPerProcess - 1;

  // The curve looks built only when Build() has finished
  m_id = NO_CURVE_ID;
  m_SampleRate = sampleRate;
  m_NFramesPerProcess = nFramesPerProcess;
  m_LoopBeginIndex = loopBeginIndex;
  m_LoopEndIndex = loopEndIndex;
  m_NRateUnitsFrames = loopEndIndex + nTailFrames;
  // Every cell is written below, so it need not be zeroed
  mp_RateUnitsData
    = std::make_unique_for_overwrite<unsigned[]>(m_NRateUnitsFrames);

  unsigned *pRateUnits = mp_RateUnitsData.get();
  unsigned *pLoopRateUnits = pRateUnits + loopBeginIndex;

  // Step 1: convert the cents of the head and the loop to rates
  bool isClamped = false;
  const float *pCents = pCentsData;
  unsigned *pRate = pRateUnits;

  for (unsigned frameI = 0; frameI < loopEndIndex;
       ++frameI, ++pCents, ++pRate) {
    const float rawCents = *pCents;
    float cents = rawCents;

    // Also true for NaN
    if (!(std::fabs(rawCents) <= MAX_PITCH_DEVIATION_CENTS)) {
      if (!isClamped) {
        isClamped = true;
        wxLogWarning(
          _("The pitch tremulant curve has the value %f cents at frame %u, "
            "which is outside of the range +-%.0f cents. It is clamped; "
            "further such values are not reported."),
          rawCents,
          frameI,
          MAX_PITCH_DEVIATION_CENTS);
      }
      cents = std::isnan(rawCents)
        ? 0.0f
        : std::copysign(MAX_PITCH_DEVIATION_CENTS, rawCents);
    }
    *pRate
      = GOSoundResample::rateToFractionIncrement(std::exp2(cents / 1200.0f));
  }

  // Steps 2-4 only for a normalised curve (always for Build(), not for some
  // tests)
  if (isToNormalise) {
    // Step 2: the actual sum of the loop rates and the target one
    const uint64_t actualLoopSumUnits = std::accumulate(
      pLoopRateUnits, pLoopRateUnits + nLoopFrames, (uint64_t)0);
    /* The loop repeats forever, so it must advance the read position by
     * exactly as many frames as it has, i.e. the rates must average exactly
     * 1.0: then the read position does not drift against the write position
     * from cycle to cycle (there is no lag feedback that could correct it).
     * The sum of the increments that advance a position with a zero fraction
     * by nLoopFrames frames is exactly that. */
    const uint64_t targetLoopSumUnits
      = GOSoundResample::computeMinNUnitsToReach(0, nLoopFrames);

    // Step 3: make the loop sum exactly the target, so the rates average 1.0
    const AffineCoefficients coefficients = normalisePitchRateCurve(
      pLoopRateUnits, nLoopFrames, actualLoopSumUnits, targetLoopSumUnits);

    /* Step 4: the head goes through the same affine transform as the loop,
     * otherwise a deep curve, whose loop was pressed, would jump in frequency
     * at the seam between them. The head's own sum is not normalised; a head
     * that is deeper than the loop is clamped to the allowed range by the
     * transform. */
    applyAffineTransform(pRateUnits, loopBeginIndex, coefficients);
  }

  /* Step 5: the tail repeats the start of the loop (tiled, if the loop is
   * shorter than the tail), so that nFramesPerProcess consecutive rates from
   * any position of the head or the loop are contiguous */
  unsigned *pTail = pRateUnits + loopEndIndex;
  unsigned nTailFramesLeft = nTailFrames;

  while (nTailFramesLeft > 0) {
    const unsigned nCopyFrames = std::min(nTailFramesLeft, nLoopFrames);

    std::copy(pLoopRateUnits, pLoopRateUnits + nCopyFrames, pTail);
    pTail += nCopyFrames;
    nTailFramesLeft -= nCopyFrames;
  }

  // Step 6: precompute the sum of the rates of the chunk starting at every
  // index
  assert((uint64_t)nFramesPerProcess * MAX_RATE_UNITS < ((uint64_t)1 << 32));
  mp_ChunkSumUnits = std::make_unique_for_overwrite<unsigned[]>(loopEndIndex);

  unsigned *pChunkSumUnits = mp_ChunkSumUnits.get();
  // The sliding chunk is [pChunkBegin, pChunkEnd); it starts as the first chunk
  const unsigned *pChunkBegin = pRateUnits;
  const unsigned *pChunkEnd = pRateUnits + nFramesPerProcess;
  // The sum of the sliding chunk; initialised with the first chunk
  unsigned currChunkSumUnits
    = std::accumulate(pChunkBegin, pChunkEnd, (unsigned)0);
  unsigned nChunksLeft = loopEndIndex;

  assert(nChunksLeft > 0);
  do {
    *pChunkSumUnits++ = currChunkSumUnits;
    // The last chunk ends at the end of the tail, there is nothing to slide to
    if (--nChunksLeft == 0)
      break;
    // Slide the chunk by one rate; the unsigned wraparound of the difference
    // cancels out
    currChunkSumUnits += *pChunkEnd++ - *pChunkBegin++;
  } while (true);

  m_id = ++last_curve_id;
}

void GOSoundVibratoPitchRateCurve::Build(
  const float *pCentsData,
  unsigned sampleRate,
  unsigned loopBeginIndex,
  unsigned loopEndIndex,
  unsigned nFramesPerProcess) {
  BuildInternal(
    pCentsData,
    sampleRate,
    loopBeginIndex,
    loopEndIndex,
    nFramesPerProcess,
    true);
}

void GOSoundVibratoPitchRateCurve::Destroy() {
  // The curve looks not built first: the mirror of Build(), which gives the id
  // last
  m_id = NO_CURVE_ID;
  // The rest in the reverse order of Build()
  mp_ChunkSumUnits.reset();
  mp_RateUnitsData.reset();
  m_NRateUnitsFrames = 0;
  m_LoopEndIndex = 0;
  m_LoopBeginIndex = 0;
  m_NFramesPerProcess = 0;
  m_SampleRate = 0;
}
