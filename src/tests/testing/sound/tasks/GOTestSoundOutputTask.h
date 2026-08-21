/*
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTSOUNDOUTPUTTASK_H
#define GOTESTSOUNDOUTPUTTASK_H

#include <string>

#include "GOTest.h"

/**
 * Exercises GOSoundOutputTask's per-channel mixing, clamping and metering
 * (Stage 3's planar rewrite of GOSoundOutputTask::DoRun()), using a stub
 * GOSoundBufferTaskBase input instead of a real audio-group task.
 */
class GOTestSoundOutputTask : public GOTest {
private:
  static const std::string TEST_NAME;

  /** With an identity scale-factor matrix, each output channel equals the
   * corresponding input channel, unclamped values pass through unchanged. */
  void TestIdentityMixPassesThroughUnclamped();

  /** Values outside [-1, 1] are clamped in place, independently per
   * channel. */
  void TestClampsOutOfRangeValuesPerChannel();

  /** GetMeterInfo() reports the peak absolute clamped amplitude seen so
   * far, one entry per channel, and ResetMeterInfo() clears it back to 0. */
  void TestMeterInfoTracksPeakPerChannelAndResets();

  /** A zero scale factor excludes that input channel from the mix. */
  void TestZeroScaleFactorExcludesChannel();

  /** With a per-frame ramp (distinct value per channel and frame) as input,
   * every output frame of every channel must equal the corresponding input
   * value - catches channel/frame swap or stride bugs that a constant-value
   * fill checked only at frame 0 would miss. */
  void TestIdentityMixPreservesPerFrameLayout();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTSOUNDOUTPUTTASK_H */
