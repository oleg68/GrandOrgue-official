/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestPerfSoundVibratoPitchRateCurve.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/effects/GOSoundVibratoPitchRateCurve.h"

const std::string GOTestPerfSoundVibratoPitchRateCurve::TEST_NAME
  = "GOTestPerfSoundVibratoPitchRateCurve";

using Curve = GOSoundVibratoPitchRateCurve;

static constexpr unsigned TEST_SAMPLE_RATE = 96000;
static constexpr unsigned UNIT_RATE = GOSoundResample::UPSAMPLE_FACTOR;
static constexpr unsigned N_FRAMES_PER_PROCESS = 128;

/* The unit is Mcells/s of the curve (the curve length is the "size"), for the
 * cursor Mrounds/s (the size is 1). Calibration rule throughout this file:
 * worst observed run, minus 10%, floored to 2 significant digits - from local
 * runs (Release and Debug), at TEST_SAMPLE_RATE; not yet validated on CI. */
static constexpr GOTestPerfSoundBufferBaseline BASELINE_APPLY_AFFINE[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 161.1/158.1/158.9 Mcells/s, minus 10%
  {100, 140},
  {10000, 140},
  {100000, 140},
  {1000000, 130}, // worst of 3 runs 155.2
#else
  // Debug, worst of 3 runs 116.7/153.5/140.9 Mcells/s, minus 10%
  {100, 100},
  {10000, 130},
  {100000, 120},
  {1000000, 130}, // worst of 3 runs 146.2
#endif
};

static constexpr GOTestPerfSoundBufferBaseline BASELINE_NORMALISE_SHIFT[] = {
#ifdef NDEBUG
  // Release, worst of 7 runs 873.4/985.7/1074.1/987.0/916.1 Mcells/s, minus 10%
  {100, 780},
  {1000, 880},
  {10000, 960},
  {100000, 880},
  {1000000, 820},
#else
  // Debug, worst of 6 runs 288.0/547.9/512.0/384.0/507.8 Mcells/s, minus 10%
  {100, 250},
  {1000, 490},
  {10000, 460},
  {100000, 340},
  {1000000, 450},
#endif
};

static constexpr GOTestPerfSoundBufferBaseline BASELINE_NORMALISE_PRESSED[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 136.3/142.9/141.2/139.2 Mcells/s, minus 10%
  {100, 120},
  {1000, 120},
  {10000, 120},
  {100000, 120},
  {1000000, 120}, // worst of 3 runs 137.5
#else
  // Debug, worst of 6 runs 113.1/121.0/122.6/127.6 Mcells/s, minus 10%
  {100, 100},
  {1000, 100},
  {10000, 110},
  {100000, 110},
  {1000000, 110}, // worst of 3 runs 124.3
#endif
};

static constexpr GOTestPerfSoundBufferBaseline BASELINE_BUILD_NOT_NORMALISED[]
  = {
#ifdef NDEBUG
    // Release, worst of 3 runs 184.0/198.9/206.6/166.3 Mcells/s, minus 10%
    {100, 160},
    {1000, 170},
    {10000, 180},
    {100000, 140},
    {1000000, 130}, // worst of 3 runs 153.0
#else
    // Debug, worst of 6 runs 93.0/147.6/152.3/135.0 Mcells/s, minus 10%
    {100, 83},
    {1000, 130},
    {10000, 130},
    {100000, 120},
    {1000000, 110}, // worst of 3 runs 132.9
#endif
};

static constexpr GOTestPerfSoundBufferBaseline BASELINE_BUILD_NORMALISED[] = {
#ifdef NDEBUG
  // Release, worst of 3 runs 142.7/156.3/154.2/133.3 Mcells/s, minus 10%
  {100, 120},
  {1000, 140},
  {10000, 130},
  {100000, 120},
  {1000000, 110}, // worst of 3 runs 126.3
#else
  // Debug, worst of 6 runs 99.5/112.9/112.2/105.2 Mcells/s, minus 10%
  {100, 89},
  {1000, 100},
  {10000, 100},
  {100000, 94},
  {1000000, 85}, // worst of 3 runs 94.7
#endif
};

/* One round per call; the harness prints the unit as Mframes/sec, it is
 * Mrounds/s here. The per-case baselines are in BASELINE_POSITION_ROUNDS,
 * in the order of the loops in TestPerfPositionSelectAdvance(). */
static constexpr double BASELINE_POSITION_ROUNDS[] = {
#ifdef NDEBUG
  // Release, worst of 6 runs 333.6/323.9/332.4/263.8/228.5/243.5/137.7/231.3
  // Mrounds/s, minus 10%
  300,
  290,
  290,
  230,
  200,
  210,
  120,
  200,
#else
  // Debug, worst of 6 runs 143.4/135.5/135.1/114.5/98.3/101.4/115.8/100.3
  // Mrounds/s, minus 10%
  120,
  120,
  120,
  100,
  88,
  91,
  100,
  90,
#endif
};

/** About the same total work (in cells) for every curve length */
static unsigned get_nIterations(unsigned curveLen) {
  return std::clamp(5000000u / curveLen, 5u, 50000u);
}

static std::vector<float> make_sine_cents(unsigned nFrames, float amplitude) {
  std::vector<float> cents(nFrames);

  for (unsigned frameI = 0; frameI < nFrames; ++frameI)
    cents[frameI]
      = amplitude * std::sin(2.0f * (float)M_PI * (float)frameI / 1000.0f);
  return cents;
}

std::vector<unsigned> GOTestPerfSoundVibratoPitchRateCurve::makeMasterRates(
  unsigned curveLen, const std::vector<float> &cents) {
  Curve curve;

  curve.BuildInternal(
    cents.data(), TEST_SAMPLE_RATE, 0, curveLen, N_FRAMES_PER_PROCESS, false);
  return std::vector<unsigned>(
    curve.mp_RateUnitsData.get(), curve.mp_RateUnitsData.get() + curveLen);
}

void GOTestPerfSoundVibratoPitchRateCurve::TestPerfApplyAffineTransform() {
  std::cout << "\nPerformance test: applyAffineTransform (k = 1/2)\n";
  for (const GOTestPerfSoundBufferBaseline &baseline : BASELINE_APPLY_AFFINE) {
    const unsigned len = baseline.m_BufferSize;
    const std::vector<unsigned> master
      = makeMasterRates(len, make_sine_cents(len, 100.0f));
    std::vector<unsigned> work(len);

    m_NumIterations = get_nIterations(len);
    RunAndEvaluateTest("ApplyAffineTransform", baseline, [&]() {
      std::copy(master.begin(), master.end(), work.begin());
      Curve::applyAffineTransform(
        work.data(), len, Curve::AffineCoefficients{1, 2, UNIT_RATE / 2});
    });
  }
}

void GOTestPerfSoundVibratoPitchRateCurve::TestPerfNormaliseShift() {
  std::cout << "\nPerformance test: normalisePitchRateCurve (plain shift)\n";
  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_NORMALISE_SHIFT) {
    const unsigned len = baseline.m_BufferSize;
    const std::vector<unsigned> master
      = makeMasterRates(len, make_sine_cents(len, 100.0f));
    const uint64_t masterSum
      = std::accumulate(master.begin(), master.end(), (uint64_t)0);
    // A non-zero remainder, so that both final passes work
    const uint64_t target = (uint64_t)len * UNIT_RATE + len / 2;
    std::vector<unsigned> work(len);

    m_NumIterations = get_nIterations(len);
    RunAndEvaluateTest("NormaliseShift", baseline, [&]() {
      std::copy(master.begin(), master.end(), work.begin());
      Curve::normalisePitchRateCurve(work.data(), len, masterSum, target);
    });
  }
}

void GOTestPerfSoundVibratoPitchRateCurve::TestPerfNormalisePressed() {
  std::cout << "\nPerformance test: normalisePitchRateCurve (pressing)\n";
  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_NORMALISE_PRESSED) {
    const unsigned len = baseline.m_BufferSize;
    // A +-2400 cents square wave: a plain shift does not fit the range
    std::vector<float> cents(len);

    for (unsigned frameI = 0; frameI < len; ++frameI)
      cents[frameI] = (frameI / 10) % 2 ? 2400.0f : -2400.0f;

    const std::vector<unsigned> master = makeMasterRates(len, cents);
    const uint64_t masterSum
      = std::accumulate(master.begin(), master.end(), (uint64_t)0);
    const uint64_t target = (uint64_t)len * UNIT_RATE;
    std::vector<unsigned> work(len);

    m_NumIterations = get_nIterations(len);
    RunAndEvaluateTest("NormalisePressed", baseline, [&]() {
      std::copy(master.begin(), master.end(), work.begin());
      Curve::normalisePitchRateCurve(work.data(), len, masterSum, target);
    });
  }
}

void GOTestPerfSoundVibratoPitchRateCurve::TestPerfBuildNotNormalised() {
  std::cout << "\nPerformance test: BuildInternal (not normalised)\n";
  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_BUILD_NOT_NORMALISED) {
    const unsigned len = baseline.m_BufferSize;
    const std::vector<float> cents = make_sine_cents(len, 100.0f);
    Curve curve;

    m_NumIterations = get_nIterations(len);
    RunAndEvaluateTest("BuildNotNormalised", baseline, [&]() {
      curve.BuildInternal(
        cents.data(),
        TEST_SAMPLE_RATE,
        len / 10,
        len,
        N_FRAMES_PER_PROCESS,
        false);
    });
  }
}

void GOTestPerfSoundVibratoPitchRateCurve::TestPerfBuildNormalised() {
  std::cout << "\nPerformance test: Build (normalised)\n";
  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_BUILD_NORMALISED) {
    const unsigned len = baseline.m_BufferSize;
    const std::vector<float> cents = make_sine_cents(len, 100.0f);
    Curve curve;

    m_NumIterations = get_nIterations(len);
    RunAndEvaluateTest("BuildNormalised", baseline, [&]() {
      curve.Build(
        cents.data(), TEST_SAMPLE_RATE, len / 10, len, N_FRAMES_PER_PROCESS);
    });
  }
}

void GOTestPerfSoundVibratoPitchRateCurve::TestPerfCopy() {
  std::cout << "\nPerformance test: copy of the master rates (reference)\n";
  for (const GOTestPerfSoundBufferBaseline &baseline :
       BASELINE_NORMALISE_SHIFT) {
    const unsigned len = baseline.m_BufferSize;
    const std::vector<unsigned> master
      = makeMasterRates(len, make_sine_cents(len, 100.0f));
    std::vector<unsigned> work(len);
    // The copy has no baseline of its own
    const GOTestPerfSoundBufferBaseline noBaseline = {len, 0.0};

    m_NumIterations = get_nIterations(len);
    RunAndEvaluateTest("Copy", noBaseline, [&]() {
      std::copy(master.begin(), master.end(), work.begin());
    });
  }
}

void GOTestPerfSoundVibratoPitchRateCurve::TestPerfPositionSelectAdvance() {
  std::cout << "\nPerformance test: Position SelectChunk + Advance\n";
  unsigned caseI = 0;

  for (const unsigned nFrames : {32u, 128u, 512u, 2048u})
    for (const unsigned loopLen : {1000u, 100000u}) {
      const std::vector<float> cents = make_sine_cents(loopLen, 100.0f);
      Curve curve;
      Curve::Position pos;
      unsigned long checksum = 0;

      curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, loopLen, nFrames);
      m_NumIterations = 10000000;
      RunAndEvaluateTest(
        "PositionRound" + std::to_string(nFrames) + "/"
          + std::to_string(loopLen),
        GOTestPerfSoundBufferBaseline{1, BASELINE_POSITION_ROUNDS[caseI++]},
        [&]() {
          // The sum is used so that the optimiser cannot drop the call
          checksum += pos.SelectChunk(&curve).nTotalUnits;
          pos.Advance(nFrames);
        });
      if (checksum == 1)
        std::cout << "";
    }
}

void GOTestPerfSoundVibratoPitchRateCurve::run() {
  m_failedTests.clear();

  std::cout << "\n========== Performance Tests for "
               "GOSoundVibratoPitchRateCurve ==========\n";
#ifdef NDEBUG
  std::cout << "Build mode: Release\n";
#else
  std::cout << "Build mode: Debug\n";
#endif

  TestPerfApplyAffineTransform();
  TestPerfNormaliseShift();
  TestPerfNormalisePressed();
  TestPerfBuildNotNormalised();
  TestPerfBuildNormalised();
  TestPerfCopy();
  TestPerfPositionSelectAdvance();

  std::cout << "\n========== Performance Tests Completed ==========\n";

  ReportFailedTests();
}
