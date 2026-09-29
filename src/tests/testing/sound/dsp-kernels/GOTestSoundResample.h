/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTSOUNDRESAMPLE_H
#define GOTESTSOUNDRESAMPLE_H

#include <string>

#include "GOTest.h"

/**
 * Exercises GOSoundResample::ResampleBlockVariableRate() and
 * RingPlanarFrameVector, the variable-rate resampling path added for the
 * upcoming pitch-modulated tremulant (#709). Nothing consumes this path
 * yet, so these tests are this path's only coverage.
 */
class GOTestSoundResample : public GOTest {
private:
  static const std::string TEST_NAME;

  /** ResampleBlockVariableRate() fed a constant-increment array must match
   * ResampleBlock()'s own output and final ResamplingPosition state
   * bit-exactly, for both LINEAR and POLYPHASE. */
  void TestConstantRateEquivalence();

  /** ResampleBlockVariableRate() over a hand-picked varying-increment
   * array, checked against an independently hand-computed
   * index/fraction/output trajectory (LINEAR only, exact arithmetic). */
  void TestVaryingRateCorrectness();

  /** RingPlanarFrameVector reads, confined to [0, nLogicalFrames) and not
   * needing the mirrored tail, match an equivalent PtrFrameVector's
   * reads of the same logical values. */
  void TestRingNonWrappingReads();

  /** RingPlanarFrameVector reads whose nPoints-wide window crosses the
   * logical-frame-count boundary, including a read starting exactly at
   * the seam, are answered correctly from the mirrored tail. */
  void TestRingMirroredSeamReads();

  /** RingPlanarFrameVector::NormalizePosition(): driven through many
   * Inc()/NormalizePosition() call pairs (mimicking ResampleBlockImpl()'s
   * own per-frame sequence), including several full wraps, GetIndex() must
   * stay below nLogicalFrames and match an independently computed
   * (unbounded index) % nLogicalFrames trajectory. */
  void TestRingNormalizePosition();

  /** ResampleBlockVariableRate() with PolyphaseResampler, reading stereo
   * through a RingPlanarFrameVector whose window crosses the ring's seam,
   * must match the same read done via PtrFrameVector over the equivalent
   * manually-unwrapped source - the combination a modulated delay line
   * uses. Stereo also exercises the ring's per-channel stride
   * (channel * nPhysicalFrames) through the resampler, which the
   * mono-only correctness tests above never touch. */
  void TestRingResamplerIntegration();

  /** ResampleBlockVariableRatePlanar(), reading the same seam-crossing ring
   * as TestRingResamplerIntegration(), must produce exactly the same
   * values (in planar layout) and the same final ResamplingPosition as
   * ResampleBlockVariableRate()'s interleaved output over the equivalent
   * flat source. */
  void TestPlanarVariableRateEquivalence();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTSOUNDRESAMPLE_H */
