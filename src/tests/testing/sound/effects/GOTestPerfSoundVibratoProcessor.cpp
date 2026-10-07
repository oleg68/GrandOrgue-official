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
#include "sound/effects/GOSoundVibratoPitchRateCurve.h"
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
 * floored to 2 significant digits - from three local runs each (Release
 * and Debug), at TEST_SAMPLE_RATE; not yet validated on CI (see
 * GOTestPerfSoundResample's own tables for the established
 * recalibration-from-CI caveat). */
static constexpr GOTestPerfSoundBufferBaseline BASELINE_LINEAR[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 409.0/560.9/587.0/507.6 Mitems/sec, minus 10%
  {32, 360},
  {128, 500},
  {512, 520},
  {2048, 450},
#else
  // Debug, worst of 3 runs 101.4/118.4/123.8/125.0 Mitems/sec, minus 10%
  {32, 91},
  {128, 100},
  {512, 110},
  {2048, 110},
#endif
};

static constexpr GOTestPerfSoundBufferBaseline BASELINE_POLYPHASE[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 351.3/435.4/453.8/456.8 Mitems/sec, minus 10%
  {32, 310},
  {128, 390},
  {512, 400},
  {2048, 410},
#else
  // Debug, worst of 3 runs 41.0/50.7/54.6/55.5 Mitems/sec, minus 10%
  {32, 36},
  {128, 45},
  {512, 49},
  {2048, 49},
#endif
};

/* GetResamplerPositionIncrementsForUse(): Mframes/sec (one frame is one
 * increment), the same calibration rule as above. */
static constexpr GOTestPerfSoundBufferBaseline
  BASELINE_INCREMENTS_NO_VIOLATION[]
  = {
/* The call is O(1) here (a few ns), so the figures are noisy and the margin
 * is wider than the usual 10% */
#ifdef NDEBUG
    // Release, worst of 3 runs 6990/44015/136545/613115 Mframes/sec
    {32, 5000},
    {128, 30000},
    {512, 100000},
    {2048, 500000},
#else
    // Debug, worst of 3 runs 4301/19878/88269/296310 Mframes/sec
    {32, 3000},
    {128, 15000},
    {512, 70000},
    {2048, 250000},
#endif
};

static constexpr GOTestPerfSoundBufferBaseline
  BASELINE_INCREMENTS_WITH_RESCALE[]
  = {
#ifdef NDEBUG
    // Release, worst of 3 runs 525.0/995.1/1115.7/1156.7 Mframes/sec, minus 10%
    {32, 470},
    {128, 890},
    {512, 1000},
    {2048, 1040},
#else
    // Debug, worst of 3 runs 306.9/448.6/574.1/615.0 Mframes/sec, minus 10%
    {32, 270},
    {128, 400},
    {512, 510},
    {2048, 550},
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
    const std::vector<float> cents(baseline.m_BufferSize, 0.0f);
    GOSoundVibratoPitchRateCurve curve;

    curve.Build(
      cents.data(),
      TEST_SAMPLE_RATE,
      0,
      baseline.m_BufferSize,
      baseline.m_BufferSize);
    processor.SetPitchRateCurve(&curve);
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

template <unsigned N>
void GOTestPerfSoundVibratoProcessor::RunIncrementsBaseline(
  const std::string &functionName,
  const GOTestPerfSoundBufferBaseline (&baselines)[N],
  unsigned rateMultiplier,
  unsigned nExtraFramesToRead) {
  GOSoundResample resample;

  for (const GOTestPerfSoundBufferBaseline &baseline : baselines) {
    const unsigned nFrames = baseline.m_BufferSize;
    GOSoundVibratoProcessor processor(resample);

    processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE);

    /* ring - L == nMaxLagFrames + nFrames == availRead + availWrite */
    const unsigned nReadAndWriteFrames = processor.m_NDelayBufferRingFrames
      - GOSoundVibratoProcessorState::N_LOOKAHEAD_FRAMES;
    const unsigned availRead = nFrames + nExtraFramesToRead;
    const unsigned availWrite = nReadAndWriteFrames - availRead;
    const unsigned fraction = 1234;
    const std::vector<unsigned> rates(
      nFrames, rateMultiplier * GOSoundResample::UPSAMPLE_FACTOR);
    const GOSoundVibratoPitchRateCurve::ChunkDescription chunk = {
      rates.data(),
      rateMultiplier * GOSoundResample::UPSAMPLE_FACTOR * nFrames};
    std::vector<unsigned> tmpIncrements(nFrames);
    unsigned long sink = 0;

    RunAndEvaluateTest(functionName, baseline, [&]() {
      // The sum keeps the optimiser from dropping the call
      sink += *processor.GetResamplerPositionIncrementsForUse(
        chunk, fraction, availRead, availWrite, tmpIncrements.data());
    });
    if (sink == 1)
      std::cout << "";
  }
}

void GOTestPerfSoundVibratoProcessor::TestPerfIncrementsNoViolation() {
  std::cout << "\nPerformance test: GetResamplerPositionIncrementsForUse "
               "(no violation)\n";
  // The chunk advances nFrames frames, the window is up to nFrames + 4000
  RunIncrementsBaseline(
    "IncrementsNoViolation", BASELINE_INCREMENTS_NO_VIOLATION, 1, 4000);
}

void GOTestPerfSoundVibratoProcessor::TestPerfIncrementsWithRescale() {
  std::cout << "\nPerformance test: GetResamplerPositionIncrementsForUse "
               "(with rescale)\n";
  // The chunk advances 2 * nFrames frames, the window is only nFrames + 1
  RunIncrementsBaseline(
    "IncrementsWithRescale", BASELINE_INCREMENTS_WITH_RESCALE, 2, 1);
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
  TestPerfIncrementsNoViolation();
  TestPerfIncrementsWithRescale();

  std::cout << "\n========== Performance Tests Completed ==========\n";

  ReportFailedTests();
}
