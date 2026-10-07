/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDVIBRATOPITCHRATECURVE_H
#define GOSOUNDVIBRATOPITCHRATECURVE_H

#include <cassert>
#include <cstdint>
#include <memory>

#include "sound/dsp-kernels/GOSoundResample.h"

/**
 * A precomputed curve of resampling rates in the units of GOSoundResample,
 * used for the pitch vibrato. Each cell is the increment of the resampler's
 * position per one output audio frame (GOSoundResample::ResamplingPosition::
 * Inc()), so a whole chunk of cells can be passed straight to
 * GOSoundResample::ResampleBlockVariableRatePlanar() without any conversion.
 * That is what the curve exists for: the performance of the audio thread.
 *
 * Layout: a head (a lead-in walked once), a loop (repeated forever) and a tail
 * of nFramesPerProcess - 1 cells repeating the start of the loop, which makes
 * any chunk of nFramesPerProcess cells contiguous.
 *
 * The curve is normalised: the sum of the increments over the loop is exactly
 * the advance of the resampler's position by as many whole frames as the loop
 * has, as if the rate were constant. Hence the read position of the delay
 * buffer does not drift against the write position from one pass of the loop
 * to the next, so a delay buffer of a finite length is enough, and the read
 * position never overtakes the write position however long the vibrato lasts.
 *
 * Built once by Build() on the control thread (allocates, heavy math), then
 * read-only: any number of chains may walk it at once, each with its own
 * Position. Build() also precomputes the sum of every possible chunk, so the
 * audio thread need not sum the cells of a chunk. Not copyable. It may be
 * rebuilt or destroyed only while nothing reads it (see Destroy()).
 */
class GOSoundVibratoPitchRateCurve {
public:
  /**
   * The largest pitch deviation from the original, in cents, that Build()
   * accepts, in either direction (two octaves). A greater absolute value of
   * the cents is clamped to it.
   */
  static constexpr float MAX_PITCH_DEVIATION_CENTS = 2400.0f;

  /**
   * The id of no curve: of a not built (or destroyed) curve, and what a
   * Position bound to nothing holds.
   */
  static constexpr uint64_t NO_CURVE_ID = 0;

  /** The lowest rate a built curve may hold: 1/8 frame per frame. */
  static constexpr int MIN_RATE_UNITS
    = (int)GOSoundResample::rateToFractionIncrement(0.125f);
  /** The highest rate a built curve may hold: 8 frames per frame. */
  static constexpr int MAX_RATE_UNITS
    = (int)GOSoundResample::rateToFractionIncrement(8.0f);
  /**
   * The result range of normalisePitchRateCurve() is narrowed by this on each
   * side: the final remainder step may add up to this many units to a cell.
   */
  static constexpr int RATE_MARGIN_UNITS = 2;
  static constexpr int MIN_ALLOWED_RATE_UNITS
    = MIN_RATE_UNITS + RATE_MARGIN_UNITS;
  static constexpr int MAX_ALLOWED_RATE_UNITS
    = MAX_RATE_UNITS - RATE_MARGIN_UNITS;

  /**
   * The coefficients of the affine transform y = k * x + c of a rate, with
   * k = kNum / kDen and c = cNum / kDen, so a rate x becomes
   * (kNum * x + cNum) / kDen, rounded to nearest.
   *
   * A plain shift has kNum == kDen == 1 and cNum == the shift of the mean.
   * Pressing has kNum < kDen and cNum == kDen * targetMean - kNum *
   * currentMean. The fields are int64_t because the products of the rates (up
   * to 2^16) with kNum and kDen do not fit an int.
   */
  struct AffineCoefficients {
    int64_t kNum;
    int64_t kDen;
    int64_t cNum;
  };

  /**
   * Describes the piece of the curve selected for one processing round: the
   * rates themselves are not copied, the chunk only points to them in the
   * curve. It is always m_NFramesPerProcess rates long and contiguous in
   * memory, so it can be passed straight to the resampler. Valid until the
   * curve is rebuilt or destroyed, and until the Position that returned it is
   * advanced or selects another curve.
   */
  struct ChunkDescription {
    /**
     * The first rate of the chunk; nullptr if there is no curve (the vibrato
     * is inactive).
     */
    const unsigned *pRateUnits;
    /**
     * The sum of all the rates of the chunk, i.e. the advance of the
     * resampler's position over the whole chunk; 0 if there is no curve.
     */
    unsigned nTotalUnits;

    /**
     * @return true if there is a curve, i.e. the chunk holds rates
     *   (pRateUnits is not nullptr).
     */
    inline bool HasRates() const { return pRateUnits; }
  };

  /**
   * The cursor that walks a curve: it is owned by the user of the curve (the
   * processor state), so any number of chains may walk one curve at once.
   */
  class Position {
  private:
    friend class GOTestSoundVibratoPitchRateCurve;

    /** The curve being walked; not owned; null while unbound. */
    const GOSoundVibratoPitchRateCurve *p_curve = nullptr;
    /** The id of p_curve at the binding time; NO_CURVE_ID while unbound. */
    uint64_t m_CurveId = NO_CURVE_ID;
    /** The index of the next rate to give, within the head or the loop. */
    unsigned m_index = 0;

  public:
    /**
     * Selects the curve to walk. If its id differs from the bound one (also
     * from or to nullptr), rebinds to it and resets the index to 0;
     * SelectChunk(nullptr) is the reset. If the new curve is the same as the
     * bound one, the position does not change. The old curve is never
     * dereferenced. A curve that is not built is treated as no curve.
     * @param pCurve the curve to walk, or nullptr for no curve; a curve that is
     *   not built resets the position like nullptr
     * @return the chunk (m_NFramesPerProcess contiguous rates and their sum)
     *   at the current position; its pRateUnits is nullptr if there is no
     *   curve (the vibrato is inactive). Valid until the next SelectChunk()
     *   or Advance()
     */
    inline ChunkDescription SelectChunk(
      const GOSoundVibratoPitchRateCurve *pCurve) {
      const uint64_t curveId = pCurve ? pCurve->m_id : NO_CURVE_ID;

      if (curveId != m_CurveId) {
        p_curve = curveId != NO_CURVE_ID ? pCurve : nullptr;
        m_CurveId = curveId;
        m_index = 0;
      }
      /* The tail of the curve guarantees that nFramesPerProcess contiguous
       * rates exist from any index below the end of the loop */
      assert(!p_curve || m_index < p_curve->m_LoopEndIndex);
      return p_curve
        ? ChunkDescription{p_curve->mp_RateUnitsData.get() + m_index, p_curve->mp_ChunkSumUnits[m_index]}
        : ChunkDescription{nullptr, 0};
    }

    /**
     * Advances by one round. Once past the end of the loop, wraps inside the
     * loop only, never back into the head; the modulo is needed because the
     * loop may be shorter than nFrames. Requires a selected curve.
     * @param nFrames the round size; must be the one the curve was built for
     */
    inline void Advance(unsigned nFrames) {
      assert(p_curve);

      const unsigned loopBeginIndex = p_curve->m_LoopBeginIndex;
      const unsigned loopEndIndex = p_curve->m_LoopEndIndex;

      assert(m_index < loopEndIndex);
      assert(nFrames == p_curve->m_NFramesPerProcess);

      const unsigned advancedIndex = m_index + nFrames;

      m_index = advancedIndex < loopEndIndex ? advancedIndex
                                             : loopBeginIndex
          + (advancedIndex - loopEndIndex) % (loopEndIndex - loopBeginIndex);
      assert(m_index < loopEndIndex);
    }

    /** @return true if a curve is bound. */
    inline bool IsBound() const { return m_CurveId != NO_CURVE_ID; }
  };

private:
  friend class GOTestSoundVibratoPitchRateCurve;     // the unit tests
  friend class GOTestPerfSoundVibratoPitchRateCurve; // the perf tests
  friend class GOTestSoundVibratoProcessor; // the end-to-end tests call
                                            // BuildInternal()

  /**
   * Process-unique id from the last Build(); NO_CURVE_ID while not built /
   * after Destroy(). Lets Position detect a different curve even at a reused
   * address.
   */
  uint64_t m_id = NO_CURVE_ID;
  /** The sample rate the curve was built for; only recorded. */
  unsigned m_SampleRate = 0;
  /**
   * The round size the curve was built for (tail = m_NFramesPerProcess - 1);
   * asserted against the processor's.
   */
  unsigned m_NFramesPerProcess = 0;
  /** Index of the first loop frame = the head's length (0 = no head). */
  unsigned m_LoopBeginIndex = 0;
  /**
   * One past the last loop frame = the tail's start; the loop is never empty
   * once built.
   */
  unsigned m_LoopEndIndex = 0;
  /** m_LoopEndIndex + (m_NFramesPerProcess - 1); 0 when not built. */
  unsigned m_NRateUnitsFrames = 0;
  /**
   * The rates in the units of GOSoundResample: the head, the loop (the rates
   * average exactly 1.0) and the tail. The tail has m_NFramesPerProcess - 1
   * frames and repeats the start of the loop (tiled, if the loop is shorter),
   * so that any m_NFramesPerProcess consecutive rates from a position in the
   * head or the loop are contiguous in this array. Null when not built.
   */
  std::unique_ptr<unsigned[]> mp_RateUnitsData;
  /**
   * For every start index below m_LoopEndIndex: the sum of the
   * m_NFramesPerProcess rates from it (what ChunkDescription::nTotalUnits
   * returns). Null when not built.
   */
  std::unique_ptr<unsigned[]> mp_ChunkSumUnits;

  /**
   * Applies the affine transform to nRates rates in place. The results are
   * rounded to nearest (a half rounds up) and clamped to
   * [MIN_RATE_UNITS, MAX_RATE_UNITS]. Rounding is exact for the results that
   * are not clamped; a negative numerator rounds toward zero, but such a result
   * is below the range anyway and is clamped.
   *
   * It is slower than a plain linear shift: it does an int64_t division per
   * rate and cannot be vectorized. So use it only where a plain shift is not
   * enough, or where the performance does not matter.
   *
   * @param pRates the rates to change in place
   * @param nRates how many rates; may be 0
   * @param coefficients the transform; kDen must be positive
   * @return the sum of the new rates
   */
  static int64_t applyAffineTransform(
    unsigned *pRates, unsigned nRates, const AffineCoefficients &coefficients);

  /**
   * Does the work of Build(). Differs from it by isToNormalise: if false, the
   * rates are only converted from the cents (and the tail is filled), but the
   * loop is not normalised to the average 1.0 and the head is not mapped. Such
   * a curve breaks the contract of the processor (a zero-drift loop), so it is
   * only for the tests.
   * @param isToNormalise true for Build()
   * (the other parameters are those of Build())
   */
  void BuildInternal(
    const float *pCentsData,
    unsigned sampleRate,
    unsigned loopBeginIndex,
    unsigned loopEndIndex,
    unsigned nFramesPerProcess,
    bool isToNormalise);

public:
  GOSoundVibratoPitchRateCurve() = default;
  GOSoundVibratoPitchRateCurve(const GOSoundVibratoPitchRateCurve &) = delete;
  GOSoundVibratoPitchRateCurve &operator=(const GOSoundVibratoPitchRateCurve &)
    = delete;

  /**
   * Changes the rates (in the units of GOSoundResample) in place so that their
   * sum becomes exactly targetTotalUnits and every rate stays within
   * [MIN_RATE_UNITS, MAX_RATE_UNITS]: shifts every rate by the same delta onto
   * the target mean; if that would take an extreme rate out of the range
   * (narrowed by RATE_MARGIN_UNITS), the deviations from the current mean are
   * first pressed toward it by the largest factor k < 1 that keeps the extreme
   * rates in the range. The remainder is spread on the first cells in a
   * vectorizable pass (one unit each), which is what RATE_MARGIN_UNITS is for.
   * @param pUnitCurve curveLen rates to change in place, each within the range
   * @param currentTotalUnits their sum; both totals must be < 2^63
   * @param targetTotalUnits the new sum; its mean must be at least
   *   RATE_MARGIN_UNITS away from both bounds of the range (the caller must
   *   guarantee this: the margin is what the remainder step spends)
   * @return the affine coefficients that were applied to the rates (a plain
   *   shift or a pressing), so that the caller can apply the same transform to
   *   a head that must stay continuous with the loop
   */
  static AffineCoefficients normalisePitchRateCurve(
    unsigned *pUnitCurve,
    unsigned curveLen,
    uint64_t currentTotalUnits,
    uint64_t targetTotalUnits);

  /**
   * (Re)builds the curve from raw cents data: converts the cents to rates in
   * the units of GOSoundResample and normalises the loop so that its rates
   * average exactly 1.0. Gets a new id.
   *
   * WARNING: allocates memory and does heavy math (a pow() per frame, passes
   * over the whole curve). Never call it from an audio thread, and nothing may
   * be reading this instance meanwhile.
   *
   * @param pCentsData the pitch offsets in cents per frame, at least
   *   loopEndIndex values; read-only, not stored. Values beyond +-2400 cents
   *   are clamped and NaN is treated as 0 cents, with one warning per call
   * @param sampleRate the sample rate the curve is built for
   * @param loopBeginIndex the head's length (0 = no head)
   * @param loopEndIndex one past the last loop frame; must be > loopBeginIndex
   * @param nFramesPerProcess the round size the curve will be read with
   */
  void Build(
    const float *pCentsData,
    unsigned sampleRate,
    unsigned loopBeginIndex,
    unsigned loopEndIndex,
    unsigned nFramesPerProcess);

  /**
   * Releases the rates and returns to the not-built state: the id becomes
   * NO_CURVE_ID, the sizes become 0. Does nothing harmful on a curve that is
   * not built. Never call it from an audio thread, and nothing may be reading
   * this instance meanwhile: a processor that was given this curve must first
   * be given nullptr by SetPitchRateCurve().
   */
  void Destroy();

  uint64_t GetId() const { return m_id; }
  unsigned GetSampleRate() const { return m_SampleRate; }
  unsigned GetNFramesPerProcess() const { return m_NFramesPerProcess; }
  unsigned GetNRateUnitsFrames() const { return m_NRateUnitsFrames; }
};

#endif /* GOSOUNDVIBRATOPITCHRATECURVE_H */
