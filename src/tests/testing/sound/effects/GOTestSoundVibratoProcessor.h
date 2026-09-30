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
 * Exercises GOSoundVibratoProcessor/GOSoundVibratoProcessorState (the
 * Doppler-shift pitch vibrato built for GrandOrgue issue #709). Friended by
 * both classes for direct field access, matching sound/effects/'s own
 * GOTestSoundShelfFilterProcessor.
 *
 * SetPitchRateCurve()'s own nFrames-mismatch assert is not exercised here:
 * this codebase has no established pattern for a test that expects an
 * assert() to fire, so that precondition is documented on
 * SetPitchRateCurve() itself rather than tested at runtime.
 */
class GOTestSoundVibratoProcessor : public GOTest {
private:
  static const std::string TEST_NAME;

  /** SetPitchRateCurve(nullptr, ...) (the default, with no prior call)
   * leaves the buffer completely untouched. */
  void TestBypassExactness();

  /** An all-1.0 (0 cents) rate curve, run across many rounds with
   * GO_LINEAR_INTERPOLATION, must delay the signal by exactly
   * round(D0) samples, bit-exact - D0 is sized to land on a whole frame
   * at the test sample rate specifically so this comparison can be
   * bit-exact rather than tolerance-based. */
  void TestAllZeroCurveExactDelay();

  /** A constant rate of 2.0 must advance the read head at ~2x before the
   * lag clamp engages, then settle to an effective ~1x (matching the
   * write head) once lag is pinned at the floor clamp - the "pathological
   * curve flattens instead of corrupting the ring" contract from the
   * class doc comment. */
  void TestConstantRateClampSettlesToUnity();

  /** A sinusoidal cents curve, converted to a rate curve via 2^(c/1200)
   * exactly as a real mapper would, must be honored by the per-frame read
   * rate (after the lag-feedback term has settled) within a tolerance
   * that accounts for the feedback term's own small correction. */
  void TestSinusoidalCurveRateTracksTheory();

  /** A long run (simulated via frame count, not wall-clock time) of a
   * symmetric +-100 cent sinusoidal curve must keep lag oscillating in a
   * bounded band around D0, never drifting to either clamp - this is the
   * test that would fail without the lag-feedback term. */
  void TestLongRunDriftStaysBounded();

  /** Switching back to bypass (SetPitchRateCurve(nullptr)) after a run of
   * active processing that moved lag away from D0 must re-settle lag to
   * exactly D0 on the very next Process() call - Reset()'s unconditional
   * re-priming contract. */
  void TestBypassAfterActiveRelagsToD0();

  /** Both InterpolationType values must process a non-trivial signal
   * without asserting/crashing and must actually modulate the signal
   * (non-identity output) for a non-unity rate curve. */
  void TestBothInterpolationTypesExercised();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTSOUNDVIBRATOPROCESSOR_H */
