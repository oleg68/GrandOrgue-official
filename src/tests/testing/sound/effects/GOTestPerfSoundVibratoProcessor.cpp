/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestPerfSoundVibratoProcessor.h"

#include <iostream>
#include <vector>

#include "sound/GOSoundDefs.h"
#include "sound/buffer/GOSoundBufferPlanarMutable.h"
#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/effects/GOSoundVibratoProcessor.h"
#include "sound/processing/GOSoundProcessor.h"
#include "sound/processing/GOSoundProcessorState.h"

const std::string GOTestPerfSoundVibratoProcessor::TEST_NAME
  = "GOTestPerfSoundVibratoProcessor";

static constexpr unsigned TEST_SAMPLE_RATE = 96000;

/* Reports Mitems/sec (MAX_OUTPUT_CHANNELS items per frame), not
 * Mframes/sec, since every call processes MAX_OUTPUT_CHANNELS channels
 * together - see run()'s RunAndEvaluateTest() calls, isItemsPerSecond=true.
 * Calibration rule throughout this file: worst observed run, minus 10%,
 * floored to 2 significant digits - from a single local run each (Release
 * and Debug), at TEST_SAMPLE_RATE; not yet validated on CI (see
 * GOTestPerfSoundResample's own tables for the established
 * recalibration-from-CI caveat). */
static constexpr GOTestPerfSoundBufferBaseline BASELINE_LINEAR[] = {
#ifdef NDEBUG
  // Release, single local run 154.0/158.5/159.6/157.8 Mitems/sec, minus 10%
  {32, 130},
  {128, 140},
  {512, 140},
  {2048, 140},
#else
  // Debug, single local run 85.5/94.6/96.6/89.9 Mitems/sec, minus 10%
  {32, 76},
  {128, 85},
  {512, 86},
  {2048, 80},
#endif
};

static constexpr GOTestPerfSoundBufferBaseline BASELINE_POLYPHASE[] = {
#ifdef NDEBUG
  // Release, single local run 149.1/151.3/149.5/136.5 Mitems/sec, minus 10%
  {32, 130},
  {128, 130},
  {512, 130},
  {2048, 120},
#else
  // Debug, single local run 41.2/44.4/48.1/43.4 Mitems/sec, minus 10%
  {32, 37},
  {128, 39},
  {512, 43},
  {2048, 39},
#endif
};

static void fill_with_ramp(GOSoundBufferPlanarMutable &buffer) {
  for (unsigned itemI = 0, n = buffer.GetNItems(); itemI < n; itemI++)
    buffer.GetData()[itemI] = (float)itemI;
}

template <unsigned N>
void GOTestPerfSoundVibratoProcessor::RunActiveBaseline(
  const std::string &functionName,
  const GOTestPerfSoundBufferBaseline (&baselines)[N],
  GOSoundResample::InterpolationType interpolationType) {
  GOSoundResample resample;

  for (const GOTestPerfSoundBufferBaseline &baseline : baselines) {
    GOSoundVibratoProcessor processor(resample, interpolationType);

    processor.EnsureSetup(
      MAX_OUTPUT_CHANNELS, baseline.m_BufferSize, TEST_SAMPLE_RATE);

    GOSoundProcessor &untypedProcessor = processor;
    std::unique_ptr<GOSoundProcessorState> pState
      = untypedProcessor.CreateState();
    GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(
      buffer, MAX_OUTPUT_CHANNELS, baseline.m_BufferSize)
    const std::vector<float> rateCurve(baseline.m_BufferSize, 1.0f);

    processor.SetPitchRateCurve(rateCurve.data(), baseline.m_BufferSize);
    fill_with_ramp(buffer);

    RunAndEvaluateTest(
      functionName,
      baseline,
      [&untypedProcessor, &pState, &buffer]() {
        untypedProcessor.Process(*pState, buffer);
      },
      MAX_OUTPUT_CHANNELS,
      true);
  }
}

void GOTestPerfSoundVibratoProcessor::TestPerfProcessActiveLinear() {
  std::cout << "\nPerformance test: Process (active, Linear)\n";

  RunActiveBaseline(
    "ProcessActiveLinear",
    BASELINE_LINEAR,
    GOSoundResample::GO_LINEAR_INTERPOLATION);
}

void GOTestPerfSoundVibratoProcessor::TestPerfProcessActivePolyphase() {
  std::cout << "\nPerformance test: Process (active, Polyphase)\n";

  RunActiveBaseline(
    "ProcessActivePolyphase",
    BASELINE_POLYPHASE,
    GOSoundResample::GO_POLYPHASE_INTERPOLATION);
}

void GOTestPerfSoundVibratoProcessor::run() {
  m_failedTests.clear();

  std::cout << "\n========== Performance Tests for GOSoundVibratoProcessor "
               "==========\n";
#ifdef NDEBUG
  std::cout << "Build mode: Release\n";
#else
  std::cout << "Build mode: Debug\n";
#endif
  std::cout << "Testing with " << GetNumIterations()
            << " iterations per buffer size\n";

  TestPerfProcessActiveLinear();
  TestPerfProcessActivePolyphase();

  std::cout << "\n========== Performance Tests Completed ==========\n";

  ReportFailedTests();
}
