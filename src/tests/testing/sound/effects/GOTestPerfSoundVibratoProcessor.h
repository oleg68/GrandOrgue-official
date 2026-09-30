/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTPERFSOUNDVIBRATOPROCESSOR_H
#define GOTESTPERFSOUNDVIBRATOPROCESSOR_H

#include "../buffer/GOTestPerfSoundBufferBase.h"

#include <string>

#include "sound/dsp-kernels/GOSoundResample.h"

/**
 * Measures GOSoundVibratoProcessor::Process()'s active-path throughput -
 * the only path with real cost, since bypass is a near-free state.Reset().
 * Split by InterpolationType, since LinearResampler and PolyphaseResampler
 * have genuinely different per-sample costs (2-point vs. 8-point scalar
 * product).
 */
class GOTestPerfSoundVibratoProcessor : public GOTestPerfSoundBufferBase {
private:
  static const std::string TEST_NAME;

  /**
   * Shared implementation behind TestPerfProcessActiveLinear() and
   * TestPerfProcessActivePolyphase(): constructs a processor using
   * interpolationType, feeds it an all-1.0 rate curve (lag stays at D0,
   * so the measured cost is purely the resampler kernel's, not any
   * clamp-driven noise), and times Process() against each entry of
   * baselines.
   * @param functionName label passed through to RunAndEvaluateTest()
   * @param baselines the per-buffer-size baseline table to iterate
   * @param interpolationType which resampler the constructed processor
   *   uses
   */
  template <unsigned N>
  void RunActiveBaseline(
    const std::string &functionName,
    const GOTestPerfSoundBufferBaseline (&baselines)[N],
    GOSoundResample::InterpolationType interpolationType);

  /** Active path, GO_LINEAR_INTERPOLATION. */
  void TestPerfProcessActiveLinear();

  /** Active path, GO_POLYPHASE_INTERPOLATION. */
  void TestPerfProcessActivePolyphase();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTPERFSOUNDVIBRATOPROCESSOR_H */
