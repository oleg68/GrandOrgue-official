/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTSOUNDVIBRATOPITCHRATECURVE_H
#define GOTESTSOUNDVIBRATOPITCHRATECURVE_H

#include <string>

#include "GOTest.h"

/**
 * Exercises GOSoundVibratoPitchRateCurve and its nested Position: the
 * constants, applyAffineTransform(), normalisePitchRateCurve(), Build(),
 * BuildInternal(), Destroy() and the cursor. Friended by the curve and by
 * Position for direct access.
 */
class GOTestSoundVibratoPitchRateCurve : public GOTest {
private:
  static const std::string TEST_NAME;

  /** The rate-range constants are consistent with each other and with the
   * resampler's units. */
  void TestConstants();

  /** applyAffineTransform(): a plain shift, exact rounding, the clamp, the
   * returned sum, an empty array. */
  void TestApplyAffineTransform();

  /** normalisePitchRateCurve() on random curves and targets: the sum is exact,
   * every rate stays in the range, the coefficients match what was done. */
  void TestNormaliseRandom();

  /** normalisePitchRateCurve() pressing branch: a deep square wave keeps its
   * shape, the sum is exact and the rates stay in the range. */
  void TestNormalisePressed();

  /** normalisePitchRateCurve() on the examples of the design: constants and
   * very short curves. */
  void TestNormaliseExamples();

  /** Build() layout: head, loop, tail, sizes, ids, the exact loop sum. */
  void TestBuildLayout();

  /** Build() of a deep curve: no jump at the seam between the head and the
   * loop, every rate in the range. */
  void TestBuildSeamOfDeepCurve();

  /** Build() clamps out-of-range cents and treats NaN as 0 cents. */
  void TestBuildClampsCents();

  /** BuildInternal(..., false) gives the plain converted rates. */
  void TestBuildInternalNotNormalised();

  /** Destroy() returns to the not-built state, also repeatedly. */
  void TestDestroy();

  /** Position::SelectChunk(): binding, the same curve, a new id, nullptr,
   * a not built curve. */
  void TestPositionSelectChunk();

  /** ChunkDescription: the precomputed sum equals the direct sum at every
   * start index. */
  void TestChunkSums();

  /** Position::Advance(): the wrap inside the loop, never into the head. */
  void TestPositionAdvance();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTSOUNDVIBRATOPITCHRATECURVE_H */
