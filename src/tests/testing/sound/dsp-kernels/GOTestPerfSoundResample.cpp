/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestPerfSoundResample.h"

#include <iostream>
#include <vector>

#include "sound/dsp-kernels/GOSoundResample.h"

const std::string GOTestPerfSoundResample::TEST_NAME
  = "GOTestPerfSoundResample";

// Baseline values are the worst observed run minus a 10% margin, calibrated
// separately for Release/Debug since Debug's unoptimized code runs at roughly
// a third of Release's throughput on a developer machine.
// This file reports Mitems/sec (see RunAndEvaluateTest(...,
// /* isItemsPerSecond= */ true)): 2x the frame throughput for stereo.
// Format: {buffer_size, min_MItems_per_second} - see
// GOTestPerfSoundBufferBase::RunAndEvaluateTest().

// ResampleBlockVariableRatePlanar(): stereo only (reports Mitems/sec: 2x the
// frame throughput) - mono would run the channel-outer/frame-inner loop for a
// single channel, identical total work to a constant-rate interleaved call, so
// it would not exercise the one thing this path does differently: replaying the
// position trajectory once per channel (see ResampleBlockVariableRatePlanar()'s
// doc comment).
static constexpr unsigned N_PLANAR_PERF_CHANNELS = 2;

// Calibration rule throughout this file: worst observed run, minus 10%,
// floored to 2 significant digits.
static constexpr GOTestPerfSoundBufferBaseline
  BASELINE_VARIABLE_RATE_PLANAR_LINEAR[]
  = {
#ifdef NDEBUG
    // Release, worst of 3 local runs 936.7/925.6/979.8/968.5, minus 10%.
    {32, 840},
    {128, 830},
    {512, 880},
    {2048, 870},
#else
    // Debug, single local run 217.4/233.3/236.2/235.2.
    {32, 190},
    {128, 200},
    {512, 210},
    {2048, 210},
#endif
};

static constexpr GOTestPerfSoundBufferBaseline
  BASELINE_VARIABLE_RATE_PLANAR_POLYPHASE[]
  = {
#ifdef NDEBUG
    // Release, worst of 3 local runs 427.6/427.5/440.8/439.9, minus 10%.
    {32, 380},
    {128, 380},
    {512, 390},
    {2048, 390},
#else
    // Debug, single local run 116.2/121.1/122.1/121.3.
    {32, 100},
    {128, 100},
    {512, 100},
    {2048, 100},
#endif
};

static void fill_with_ramp(std::vector<float> &data) {
  for (unsigned i = 0, n = data.size(); i < n; i++)
    data[i] = (float)i;
}

void GOTestPerfSoundResample::TestPerfResampleBlockVariableRatePlanarLinear() {
  std::cout << "\nPerformance test: "
               "LinearResampler::ResampleBlockVariableRatePlanar (stereo)\n";

  GOSoundResample resampler;
  GOSoundResample::LinearResampler linearResampler(resampler);

  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_VARIABLE_RATE_PLANAR_LINEAR) {
    const unsigned nOutFrames = baseline.m_BufferSize;
    const unsigned nSrcFrames
      = nOutFrames + GOSoundResample::LinearResampler::VECTOR_LENGTH;
    std::vector<float> src(nSrcFrames * N_PLANAR_PERF_CHANNELS);
    // Planar layout: channel c's frame f is at out[c * nOutFrames + f].
    std::vector<float> out(nOutFrames * N_PLANAR_PERF_CHANNELS);

    fill_with_ramp(src);

    GOSoundResample::ResamplingPosition resamplingPos;

    resamplingPos.Init(1.0f);

    GOSoundResample::PtrFrameVector<float, float, N_PLANAR_PERF_CHANNELS> fV(
      src.data());
    const std::vector<unsigned> increments(
      nOutFrames, resamplingPos.GetFractionIncrement());

    RunAndEvaluateTest(
      "ResampleBlockVariableRatePlanarLinear",
      baseline,
      [&resamplingPos, &linearResampler, &fV, &increments, &out, nOutFrames]() {
        resamplingPos.SetIndex(0);
        linearResampler.ResampleBlockVariableRatePlanar<
          GOSoundResample::
            PtrFrameVector<float, float, N_PLANAR_PERF_CHANNELS>>(
          resamplingPos,
          fV,
          increments.data(),
          nOutFrames,
          N_PLANAR_PERF_CHANNELS,
          out.data(),
          nOutFrames);
      },
      N_PLANAR_PERF_CHANNELS,
      true);
  }
}

void GOTestPerfSoundResample::
  TestPerfResampleBlockVariableRatePlanarPolyphase() {
  std::cout << "\nPerformance test: "
               "PolyphaseResampler::ResampleBlockVariableRatePlanar "
               "(stereo)\n";

  GOSoundResample resampler;
  GOSoundResample::PolyphaseResampler polyphaseResampler(resampler);

  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_VARIABLE_RATE_PLANAR_POLYPHASE) {
    const unsigned nOutFrames = baseline.m_BufferSize;
    const unsigned nSrcFrames
      = nOutFrames + GOSoundResample::PolyphaseResampler::VECTOR_LENGTH;
    std::vector<float> src(nSrcFrames * N_PLANAR_PERF_CHANNELS);
    std::vector<float> out(nOutFrames * N_PLANAR_PERF_CHANNELS);

    fill_with_ramp(src);

    GOSoundResample::ResamplingPosition resamplingPos;

    resamplingPos.Init(1.0f);

    GOSoundResample::PtrFrameVector<float, float, N_PLANAR_PERF_CHANNELS> fV(
      src.data());
    const std::vector<unsigned> increments(
      nOutFrames, resamplingPos.GetFractionIncrement());

    RunAndEvaluateTest(
      "ResampleBlockVariableRatePlanarPolyphase",
      baseline,
      [&resamplingPos,
       &polyphaseResampler,
       &fV,
       &increments,
       &out,
       nOutFrames]() {
        resamplingPos.SetIndex(0);
        polyphaseResampler.ResampleBlockVariableRatePlanar<
          GOSoundResample::
            PtrFrameVector<float, float, N_PLANAR_PERF_CHANNELS>>(
          resamplingPos,
          fV,
          increments.data(),
          nOutFrames,
          N_PLANAR_PERF_CHANNELS,
          out.data(),
          nOutFrames);
      },
      N_PLANAR_PERF_CHANNELS,
      true);
  }
}

void GOTestPerfSoundResample::run() {
  m_failedTests.clear();

  std::cout << "\n========== Performance Tests for GOSoundResample "
               "==========\n";
#ifdef NDEBUG
  std::cout << "Build mode: Release\n";
#else
  std::cout << "Build mode: Debug\n";
#endif
  std::cout << "Testing with " << GetNumIterations()
            << " iterations per buffer size\n";

  TestPerfResampleBlockVariableRatePlanarLinear();
  TestPerfResampleBlockVariableRatePlanarPolyphase();

  std::cout << "\n========== Performance Tests Completed ==========\n";

  ReportFailedTests();
}
