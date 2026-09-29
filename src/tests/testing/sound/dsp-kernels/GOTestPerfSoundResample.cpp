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

#include "GOInt.h"

const std::string GOTestPerfSoundResample::TEST_NAME
  = "GOTestPerfSoundResample";

// Baseline values are the worst of 3 local isolated runs, minus a 10%
// margin, calibrated separately for Release/Debug since Debug's
// unoptimized code runs at roughly a third of Release's throughput here -
// a single shared table would either be too loose in Release or
// spuriously fail in Debug. The smallest (size=32) Debug case in any of the
// tables below is occasionally seen to dip further when the full test suite
// runs (other tests contending for the machine); it is dominated by fixed
// per-call overhead, the same effect GOTestPerfSoundShelfFilterProcessor's
// baseline comment documents for its own bypass-path case - a rerun with
// `--perf-only` isolates this test and does not show the dip.
// This file reports Mitems/sec throughout (see each run()/Test*() call's
// RunAndEvaluateTest(..., nOutChannels, /* isItemsPerSecond= */ true)) -
// for the mono cases (nOutChannels=1) that numerically equals Mframes/sec,
// so their tables are unaffected in value; the Stereo24 tables' values are
// 2x their own frame throughput, noted individually below.
// Format: {buffer_size, min_MItems_per_second} - see
// GOTestPerfSoundBufferBase::RunAndEvaluateTest().
static constexpr GOTestPerfSoundBufferBaseline BASELINE_LINEAR[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 817.1/982.4/988.1/975.5, minus 10%
  {32, 735},
  {128, 884},
  {512, 889},
  {2048, 877},
#else
  // Debug, worst of 3 runs 275.8/294.4/304.8/304.2, minus 10%. size=32 is
  // dominated by fixed per-call overhead (like
  // GOTestPerfSoundShelfFilterProcessor's own bypass-path baseline) and is
  // unusually sensitive to other tests/processes contending for the
  // machine when the full suite runs: observed as low as 221.2/247.6
  // Mitems/sec under full-suite contention in two separate runs, well
  // below the isolated worst-of-3 above, so it gets its own wider margin
  // instead of the shared -10%.
  // size=128/512/2048 spuriously failed on a GitHub Actions CI run on
  // 2026-09-28 (observed as low as 260.8/266.2/266.3, well under the
  // quiet-host -10% baseline above); this table uses those, minus 20%.
  {32, 200},
  {128, 205},
  {512, 210},
  {2048, 210},
#endif
};

static constexpr GOTestPerfSoundBufferBaseline BASELINE_POLYPHASE[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 617.3/629.3/632.6/619.5, minus 10%
  {32, 555},
  {128, 566},
  {512, 569},
  {2048, 557},
#else
  // Debug, worst of 3 runs 125.5/134.4/134.9/134.5, minus 10%. size=32
  // passed a GitHub Actions CI run on 2026-09-28 but tightly (112.7
  // observed vs. 112 baseline) and is worth watching; size=128/512/2048
  // failed that run (observed as low as 115.1/116.9/117.0), so this table
  // uses those, minus 20%.
  {32, 112},
  {128, 90},
  {512, 90},
  {2048, 90},
#endif
};

// Stereo24 (nOutChannels=2) reports Mitems/sec - 2x the frame throughput -
// see run()'s RunAndEvaluateTest() calls, isItemsPerSecond=true.
static constexpr GOTestPerfSoundBufferBaseline BASELINE_STEREO24_LINEAR[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 604.8/636.4/646.8/633.6, minus 10%
  {32, 544},
  {128, 572},
  {512, 582},
  {2048, 570},
#else
  // Debug, worst of 3 runs 296.0/306.8/309.4/308.8, minus 10% - measured
  // before ComputeOutputItem() was factored out of ResampleBlockImpl() (see
  // that method's own doc comment) to share the per-item scalar-product
  // logic with the new planar path. That extra call is inlined away in
  // Release, but in an unoptimized Debug build it is a genuine per-frame
  // cost; a single local run afterwards measured 259.6/267.1/267.0/269.1.
  // Calibration rule: worst observed run, minus 10%, floored to 2
  // significant digits.
  {32, 230},
  {128, 240},
  {512, 240},
  {2048, 240},
#endif
};

// Stereo24 (nOutChannels=2) reports Mitems/sec - 2x the frame throughput -
// see run()'s RunAndEvaluateTest() calls, isItemsPerSecond=true.
static constexpr GOTestPerfSoundBufferBaseline BASELINE_STEREO24_POLYPHASE[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 134.2/134.0/134.4/146.4, minus 10%. All four
  // sizes spuriously failed a GitHub Actions CI run on 2026-09-28 (observed
  // as low as 117.7/115.1/114.5/114.3, the size=2048 case missing by 13%);
  // this table uses those, minus 20%.
  {32, 90},
  {128, 90},
  {512, 90},
  {2048, 90},
#else
  // Debug, worst of 3 runs 93.0/98.8/98.4/99.2, minus 10%. size=32 passed
  // a GitHub Actions CI run on 2026-09-28 but tightly (83.9 observed vs. 83
  // baseline); size=128/512/2048 failed that run (observed as low as
  // 84.8/84.8/84.9), so this table uses those, minus 20%.
  {32, 83},
  {128, 65},
  {512, 65},
  {2048, 65},
#endif
};

// Same methodology as the constant-rate baselines above for Debug: worst of
// 3 local isolated runs, minus 10%. Release differs (see below).
// ResampleBlockVariableRate() is ~12-15% (Linear) / 7-10% (Polyphase) slower
// than ResampleBlock() due to the per-frame ArrayPosIncrementSource
// indirection and NormalizePosition() call that ResampleBlock()'s own
// ConstantPosIncrementSource/no-op path doesn't pay for.
static constexpr GOTestPerfSoundBufferBaseline BASELINE_VARIABLE_RATE_LINEAR[]
  = {
#ifdef NDEBUG
    // Release: the quiet-host worst-of-3 (910.9/934.6/968.1/915.9, minus
    // 10%) spuriously failed on GitHub Actions - shared/noisier CI runners
    // don't sustain the local host's throughput. Two Release CI runs on
    // 2026-09-28 measured as low as 785.4/877.8/881.9/876.7; this table uses
    // those, minus 20%, to give CI enough headroom instead of the local
    // host's -10%.
    {32, 620},
    {128, 700},
    {512, 700},
    {2048, 700},
#else
    // Debug, worst of 3 runs 277.0/297.6/300.6/302.1, minus 10% - same
    // ComputeOutputItem() extraction cost as BASELINE_STEREO24_LINEAR's
    // Debug table above (this path also goes through
    // ResampleBlockImpl()/ComputeOutputItem()). A single local run
    // afterwards measured 211.8/224.6/228.7/229.4. Calibration rule: worst
    // observed run, minus 10%, floored to 2 significant digits.
    {32, 190},
    {128, 200},
    {512, 200},
    {2048, 200},
#endif
};

static constexpr GOTestPerfSoundBufferBaseline
  BASELINE_VARIABLE_RATE_POLYPHASE[]
  = {
#ifdef NDEBUG
    // Release: same CI-vs-local-host gap as BASELINE_VARIABLE_RATE_LINEAR
    // above, wider here (up to 11% below the quiet-host -10% baseline in a
    // single CI run). Two Release CI runs on 2026-09-28 measured as low as
    // 493.2/549.6/567.4/503.2; this table uses those, minus 20%.
    {32, 390},
    {128, 430},
    {512, 450},
    {2048, 400},
#else
    // Debug, worst of 3 runs 125.6/130.6/132.1/133.1, minus 10%. All four
    // sizes spuriously failed a GitHub Actions CI run on 2026-09-28
    // (observed as low as 109.6/111.7/113.3/113.4); this table uses those,
    // minus 20%.
    {32, 85},
    {128, 85},
    {512, 90},
    {2048, 90},
#endif
};

// ResampleBlockVariableRatePlanar() vs. ResampleBlockVariableRate(): stereo
// only (reports Mitems/sec - see BASELINE_STEREO24_LINEAR's own comment) -
// mono would run the channel-outer/frame-inner loop for a single channel,
// identical total work to the interleaved path, so it would not exercise
// the one thing this path does differently: replaying the position
// trajectory once per channel (see ResampleBlockImplPlanar()'s doc
// comment).
static constexpr unsigned N_PLANAR_PERF_CHANNELS = 2;

// Calibration rule throughout this file: worst observed run, minus 10%,
// floored to 2 significant digits.
static constexpr GOTestPerfSoundBufferBaseline
  BASELINE_VARIABLE_RATE_PLANAR_LINEAR[]
  = {
#ifdef NDEBUG
    // TODO(#709): this is a single local Debug run's ratio applied to
    // BASELINE_VARIABLE_RATE_LINEAR's own Release baseline as an estimate -
    // no Release run of this test has actually been done yet. Recalibrate
    // from a real Release run before relying on this table.
    {32, 560},
    {128, 630},
    {512, 630},
    {2048, 630},
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
    // TODO(#709): same caveat as BASELINE_VARIABLE_RATE_PLANAR_LINEAR's
    // Release table above - not yet measured on Release, estimated from
    // BASELINE_VARIABLE_RATE_POLYPHASE's own Debug-to-Release ratio.
    {32, 350},
    {128, 350},
    {512, 370},
    {2048, 370},
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

static void fill_with_ramp(std::vector<GOInt24> &data) {
  for (unsigned i = 0, n = data.size(); i < n; i++)
    data[i] = (int)(i % 0x7fffff);
}

void GOTestPerfSoundResample::TestPerfResampleBlockLinear() {
  std::cout << "\nPerformance test: LinearResampler::ResampleBlock\n";

  GOSoundResample resampler;
  GOSoundResample::LinearResampler linearResampler(resampler);

  for (const GOTestPerfSoundBufferBaseline &baseline : BASELINE_LINEAR) {
    const unsigned nOutFrames = baseline.m_BufferSize;
    const unsigned nSrcFrames
      = nOutFrames + GOSoundResample::LinearResampler::VECTOR_LENGTH;
    std::vector<float> src(nSrcFrames);
    std::vector<float> out(nOutFrames);

    fill_with_ramp(src);

    GOSoundResample::ResamplingPosition resamplingPos;

    resamplingPos.Init(1.0f);

    GOSoundResample::PtrFrameVector<float, float, 1> fV(src.data());

    RunAndEvaluateTest(
      "ResampleBlockLinear",
      baseline,
      [&resamplingPos, &linearResampler, &fV, &out, nOutFrames]() {
        resamplingPos.SetIndex(0);
        linearResampler
          .ResampleBlock<GOSoundResample::PtrFrameVector<float, float, 1>, 1>(
            resamplingPos, fV, out.data(), nOutFrames);
      },
      1,
      true);
  }
}

void GOTestPerfSoundResample::TestPerfResampleBlockPolyphase() {
  std::cout << "\nPerformance test: PolyphaseResampler::ResampleBlock\n";

  GOSoundResample resampler;
  GOSoundResample::PolyphaseResampler polyphaseResampler(resampler);

  for (const GOTestPerfSoundBufferBaseline &baseline : BASELINE_POLYPHASE) {
    const unsigned nOutFrames = baseline.m_BufferSize;
    const unsigned nSrcFrames
      = nOutFrames + GOSoundResample::PolyphaseResampler::VECTOR_LENGTH;
    std::vector<float> src(nSrcFrames);
    std::vector<float> out(nOutFrames);

    fill_with_ramp(src);

    GOSoundResample::ResamplingPosition resamplingPos;

    resamplingPos.Init(1.0f);

    GOSoundResample::PtrFrameVector<float, float, 1> fV(src.data());

    RunAndEvaluateTest(
      "ResampleBlockPolyphase",
      baseline,
      [&resamplingPos, &polyphaseResampler, &fV, &out, nOutFrames]() {
        resamplingPos.SetIndex(0);
        polyphaseResampler
          .ResampleBlock<GOSoundResample::PtrFrameVector<float, float, 1>, 1>(
            resamplingPos, fV, out.data(), nOutFrames);
      },
      1,
      true);
  }
}

void GOTestPerfSoundResample::TestPerfResampleBlockStereo24Linear() {
  std::cout
    << "\nPerformance test: LinearResampler::ResampleBlock (stereo GOInt24)\n";

  GOSoundResample resampler;
  GOSoundResample::LinearResampler linearResampler(resampler);

  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_STEREO24_LINEAR) {
    const unsigned nOutFrames = baseline.m_BufferSize;
    const unsigned nSrcFrames
      = nOutFrames + GOSoundResample::LinearResampler::VECTOR_LENGTH;
    std::vector<GOInt24> src(nSrcFrames * 2);
    std::vector<float> out(nOutFrames * 2);

    fill_with_ramp(src);

    GOSoundResample::ResamplingPosition resamplingPos;

    resamplingPos.Init(1.0f);

    GOSoundResample::PtrFrameVector<GOInt24, float, 2> fV(src.data());

    RunAndEvaluateTest(
      "ResampleBlockStereo24Linear",
      baseline,
      [&resamplingPos, &linearResampler, &fV, &out, nOutFrames]() {
        resamplingPos.SetIndex(0);
        linearResampler
          .ResampleBlock<GOSoundResample::PtrFrameVector<GOInt24, float, 2>, 2>(
            resamplingPos, fV, out.data(), nOutFrames);
      },
      2,
      true);
  }
}

void GOTestPerfSoundResample::TestPerfResampleBlockStereo24Polyphase() {
  std::cout << "\nPerformance test: PolyphaseResampler::ResampleBlock "
               "(stereo GOInt24)\n";

  GOSoundResample resampler;
  GOSoundResample::PolyphaseResampler polyphaseResampler(resampler);

  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_STEREO24_POLYPHASE) {
    const unsigned nOutFrames = baseline.m_BufferSize;
    const unsigned nSrcFrames
      = nOutFrames + GOSoundResample::PolyphaseResampler::VECTOR_LENGTH;
    std::vector<GOInt24> src(nSrcFrames * 2);
    std::vector<float> out(nOutFrames * 2);

    fill_with_ramp(src);

    GOSoundResample::ResamplingPosition resamplingPos;

    resamplingPos.Init(1.0f);

    GOSoundResample::PtrFrameVector<GOInt24, float, 2> fV(src.data());

    RunAndEvaluateTest(
      "ResampleBlockStereo24Polyphase",
      baseline,
      [&resamplingPos, &polyphaseResampler, &fV, &out, nOutFrames]() {
        resamplingPos.SetIndex(0);
        polyphaseResampler
          .ResampleBlock<GOSoundResample::PtrFrameVector<GOInt24, float, 2>, 2>(
            resamplingPos, fV, out.data(), nOutFrames);
      },
      2,
      true);
  }
}

void GOTestPerfSoundResample::TestPerfResampleBlockVariableRateLinear() {
  std::cout
    << "\nPerformance test: LinearResampler::ResampleBlockVariableRate\n";

  GOSoundResample resampler;
  GOSoundResample::LinearResampler linearResampler(resampler);

  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_VARIABLE_RATE_LINEAR) {
    const unsigned nOutFrames = baseline.m_BufferSize;
    const unsigned nSrcFrames
      = nOutFrames + GOSoundResample::LinearResampler::VECTOR_LENGTH;
    std::vector<float> src(nSrcFrames);
    std::vector<float> out(nOutFrames);

    fill_with_ramp(src);

    GOSoundResample::ResamplingPosition resamplingPos;

    resamplingPos.Init(1.0f);

    GOSoundResample::PtrFrameVector<float, float, 1> fV(src.data());
    // A ConstantPosIncrementSource-equivalent: every entry equal to the
    // constant-rate case's own increment, for the closest apples-to-apples
    // comparison against TestPerfResampleBlockLinear().
    const std::vector<unsigned> increments(
      nOutFrames, resamplingPos.GetFractionIncrement());

    RunAndEvaluateTest(
      "ResampleBlockVariableRateLinear",
      baseline,
      [&resamplingPos, &linearResampler, &fV, &increments, &out, nOutFrames]() {
        resamplingPos.SetIndex(0);
        linearResampler.ResampleBlockVariableRate<
          GOSoundResample::PtrFrameVector<float, float, 1>,
          1>(resamplingPos, fV, increments.data(), out.data(), nOutFrames);
      },
      1,
      true);
  }
}

void GOTestPerfSoundResample::TestPerfResampleBlockVariableRatePolyphase() {
  std::cout
    << "\nPerformance test: PolyphaseResampler::ResampleBlockVariableRate\n";

  GOSoundResample resampler;
  GOSoundResample::PolyphaseResampler polyphaseResampler(resampler);

  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_VARIABLE_RATE_POLYPHASE) {
    const unsigned nOutFrames = baseline.m_BufferSize;
    const unsigned nSrcFrames
      = nOutFrames + GOSoundResample::PolyphaseResampler::VECTOR_LENGTH;
    std::vector<float> src(nSrcFrames);
    std::vector<float> out(nOutFrames);

    fill_with_ramp(src);

    GOSoundResample::ResamplingPosition resamplingPos;

    resamplingPos.Init(1.0f);

    GOSoundResample::PtrFrameVector<float, float, 1> fV(src.data());
    const std::vector<unsigned> increments(
      nOutFrames, resamplingPos.GetFractionIncrement());

    RunAndEvaluateTest(
      "ResampleBlockVariableRatePolyphase",
      baseline,
      [&resamplingPos,
       &polyphaseResampler,
       &fV,
       &increments,
       &out,
       nOutFrames]() {
        resamplingPos.SetIndex(0);
        polyphaseResampler.ResampleBlockVariableRate<
          GOSoundResample::PtrFrameVector<float, float, 1>,
          1>(resamplingPos, fV, increments.data(), out.data(), nOutFrames);
      },
      1,
      true);
  }
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

  TestPerfResampleBlockLinear();
  TestPerfResampleBlockPolyphase();
  TestPerfResampleBlockStereo24Linear();
  TestPerfResampleBlockStereo24Polyphase();
  TestPerfResampleBlockVariableRateLinear();
  TestPerfResampleBlockVariableRatePolyphase();
  TestPerfResampleBlockVariableRatePlanarLinear();
  TestPerfResampleBlockVariableRatePlanarPolyphase();

  std::cout << "\n========== Performance Tests Completed ==========\n";

  ReportFailedTests();
}
