/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTSOUNDVIBRATOPROCESSOR_H
#define GOTESTSOUNDVIBRATOPROCESSOR_H

#include <string>

#include "GOTest.h"

/**
 * Exercises GOSoundVibratoProcessor and GOSoundVibratoProcessorState: the
 * delay buffer geometry, the write into the ring, the resets, the choice of
 * the resampler position increments, the read back and the end-to-end
 * behaviour of Process() with curves built by GOSoundVibratoPitchRateCurve.
 * Friended by the processor, the state and the curve for direct access.
 */
class GOTestSoundVibratoProcessor : public GOTest {
private:
  static const std::string TEST_NAME;

  /** The constants and the sizes of a new state. */
  void TestStateConstructor();

  /** GetNFramesAvailableToWrite/Read() on the worked table. */
  void TestStateAvailableFrames();

  /** WriteToDelayBuffer(): the ring content, the mirrored tail, the write
   * position, over random positions and sizes, including the seam. */
  void TestStateWriteToDelayBuffer();

  /** ResetDelayBufferPositions(), MarkDirty() and the dirty flag. */
  void TestStateResetDelayBufferPositions();

  /** The full Reset() unbinds the curve and re-primes the positions. */
  void TestStateFullReset();

  /** EnsureSetup(): the ring size, the unbound curve. */
  void TestEnsureSetup();

  /** SetPitchRateCurve(): equal ids are a no-op, a rebuilt curve is not. */
  void TestSetPitchRateCurve();

  /** CreateTypedState() sizes the state for the last EnsureSetup(). */
  void TestCreateTypedState();

  /** GetResamplerPositionIncrementsForUse() with a zero-width window. */
  void TestIncrementsZeroWidthWindow();

  /** GetResamplerPositionIncrementsForUse() with a wider window and the
   * bounds. */
  void TestIncrementsWiderWindow();

  /** GetResamplerPositionIncrementsForUse() on random chunks: the result
   * always lands inside the window and a no-violation chunk is returned as is.
   */
  void TestIncrementsRandom();

  /** ReadFromDelayBuffer(): a ramp across the ring seam. */
  void TestReadFromDelayBuffer();

  /** Process() without a curve leaves the buffer untouched. */
  void TestProcessBypass();

  /** Process() with a zero-cents curve delays the signal by the look-ahead. */
  void TestProcessUnityCurveDelay();

  /** Process() with +1200 / +12 cents not normalised curves is rescaled onto
   * the exact delay. */
  void TestProcessRescaledToExactDelay();

  /** Process() with -1200 cents: the lag grows to its maximum and settles. */
  void TestProcessOctaveDownSettles();

  /** Process() with a normalised curve from Build() over several laps. */
  void TestProcessNormalisedCurve();

  /** Bypass after active rounds and a restart rewind the state. */
  void TestProcessBypassAfterActiveAndRestart();

  /** Garbage in the delay buffer never reaches the output. */
  void TestProcessGarbageNotLeaked();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTSOUNDVIBRATOPROCESSOR_H */
