/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundVibratoProcessor.h"

#include <cmath>
#include <vector>

#include "sound/buffer/GOSoundBufferPlanarMutable.h"
#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/effects/GOSoundVibratoProcessor.h"
#include "sound/effects/GOSoundVibratoProcessorState.h"
#include "sound/processing/GOSoundProcessor.h"

const std::string GOTestSoundVibratoProcessor::TEST_NAME
  = "GOTestSoundVibratoProcessor";

/* 5ms D0 at 48kHz lands on exactly 240.0f frames (verified: the float
 * division 5.0f/1000.0f, multiplied by 48000.0f, rounds back to exactly
 * 240.0f) - chosen specifically so TestAllZeroCurveExactDelay() can compare
 * bit-exact, not tolerance-based. */
static constexpr unsigned TEST_SAMPLE_RATE = 48000;
static constexpr unsigned TEST_N_FRAMES = 64;

static void fill_with_ramp(GOSoundBufferPlanarMutable &buffer, float start) {
  for (unsigned itemI = 0, n = buffer.GetNItems(); itemI < n; itemI++)
    buffer.GetData()[itemI] = start + (float)itemI;
}

void GOTestSoundVibratoProcessor::TestBypassExactness() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  processor.EnsureSetup(2, TEST_N_FRAMES, TEST_SAMPLE_RATE);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<GOSoundVibratoProcessorState> pState
    = processor.CreateTypedState();
  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 2, TEST_N_FRAMES);

  fill_with_ramp(buffer, 1.0f);

  std::vector<float> before(
    buffer.GetData(), buffer.GetData() + buffer.GetNItems());

  untypedProcessor.Process(*pState, buffer);

  for (unsigned itemI = 0, n = buffer.GetNItems(); itemI < n; itemI++)
    GOAssert(
      buffer.GetData()[itemI] == before[itemI],
      "bypass (no SetPitchRateCurve() call yet) must leave the buffer "
      "completely untouched, item "
        + std::to_string(itemI));
}

void GOTestSoundVibratoProcessor::TestAllZeroCurveExactDelay() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(
    resample, GOSoundResample::GO_LINEAR_INTERPOLATION);

  processor.EnsureSetup(1, TEST_N_FRAMES, TEST_SAMPLE_RATE);
  GOAssert(
    processor.m_NominalLagFrames == 240.0f,
    "test precondition: D0 must land on exactly 240.0f frames at "
    "TEST_SAMPLE_RATE - see this test's own comment");

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<GOSoundVibratoProcessorState> pState
    = processor.CreateTypedState();
  const std::vector<float> rateCurve(TEST_N_FRAMES, 1.0f);

  processor.SetPitchRateCurve(rateCurve.data(), TEST_N_FRAMES);

  static constexpr unsigned N_ROUNDS = 10;
  std::vector<float> input(N_ROUNDS * TEST_N_FRAMES);
  std::vector<float> output(N_ROUNDS * TEST_N_FRAMES);

  for (unsigned i = 0, n = (unsigned)input.size(); i < n; i++)
    input[i] = (float)i;

  for (unsigned roundI = 0; roundI < N_ROUNDS; roundI++) {
    GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 1, TEST_N_FRAMES);

    for (unsigned i = 0; i < TEST_N_FRAMES; i++)
      buffer.GetData()[i] = input[roundI * TEST_N_FRAMES + i];

    untypedProcessor.Process(*pState, buffer);

    for (unsigned i = 0; i < TEST_N_FRAMES; i++)
      output[roundI * TEST_N_FRAMES + i] = buffer.GetData()[i];
  }

  const unsigned d0 = (unsigned)processor.m_NominalLagFrames;

  for (unsigned n = d0, total = (unsigned)output.size(); n < total; n++)
    GOAssert(
      output[n] == input[n - d0],
      "an all-zero-cents (rate=1.0) curve must delay the signal by "
      "exactly D0 samples, bit-exact, at output frame "
        + std::to_string(n));
}

void GOTestSoundVibratoProcessor::TestConstantRateClampSettlesToUnity() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  processor.EnsureSetup(1, TEST_N_FRAMES, TEST_SAMPLE_RATE);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<GOSoundVibratoProcessorState> pState
    = processor.CreateTypedState();
  const std::vector<float> rateCurve(TEST_N_FRAMES, 2.0f);

  processor.SetPitchRateCurve(rateCurve.data(), TEST_N_FRAMES);

  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 1, TEST_N_FRAMES);

  fill_with_ramp(buffer, 0.0f);

  const double posBeforeFirstRound = pState->m_ReadHeadPosDesired;

  untypedProcessor.Process(*pState, buffer);

  const double firstRoundAdvance
    = pState->m_ReadHeadPosDesired - posBeforeFirstRound;

  GOAssert(
    firstRoundAdvance > 1.9 * TEST_N_FRAMES,
    "a rate=2.0 curve must advance the read head at ~2x before the lag "
    "clamp engages (D0 starts well above the floor clamp)");

  /* Run enough further rounds that the read head, advancing at ~2x
   * against a write head advancing at 1x, drains D0's own headroom down
   * to the floor clamp and settles there. */
  for (unsigned roundI = 0; roundI < 200; roundI++)
    untypedProcessor.Process(*pState, buffer);

  /* GetNReadBehindWriteFrames() is measured right after Process()
   * returns, by which point m_WriteHeadPos has already advanced to the
   * start of the *next* round (step 1 runs before the per-frame clamp
   * loop) - so the steady-state value it reports is the within-round
   * floor clamp plus one full round's write-head advance, not the floor
   * alone. */
  GOAssert(
    pState->GetNReadBehindWriteFrames()
      <= GOSoundResample::MAX_POINTS + TEST_N_FRAMES + 2,
    "after many rounds of a rate=2.0 curve, lag must settle near the "
    "floor clamp (plus one round's write-head advance)");

  const unsigned writeBefore = pState->m_WriteHeadPos;
  const double posBefore = pState->m_ReadHeadPosDesired;

  untypedProcessor.Process(*pState, buffer);

  const unsigned writeAfter = pState->m_WriteHeadPos;
  const double posAfter = pState->m_ReadHeadPosDesired;
  double readAdvance = posAfter - posBefore;
  double writeAdvance = (double)writeAfter - (double)writeBefore;

  if (readAdvance < 0)
    readAdvance += processor.m_NDelayBufferRingFrames;
  if (writeAdvance < 0)
    writeAdvance += processor.m_NDelayBufferRingFrames;

  GOAssert(
    std::fabs(readAdvance - writeAdvance) < 1.0,
    "once lag is pinned at the floor clamp, the effective read rate must "
    "settle to ~1x (matching the write head's own advance), not the raw "
    "2x curve - this is the 'pathological curve flattens instead of "
    "corrupting the ring' contract");
}

void GOTestSoundVibratoProcessor::TestSinusoidalCurveRateTracksTheory() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  processor.EnsureSetup(1, TEST_N_FRAMES, TEST_SAMPLE_RATE);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<GOSoundVibratoProcessorState> pState
    = processor.CreateTypedState();
  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 1, TEST_N_FRAMES);

  /* Settle the lag-feedback term at (effectively) zero first: an all-1.0
   * curve keeps lag exactly at D0, same as TestAllZeroCurveExactDelay(),
   * so the one round measured below starts with no feedback contribution
   * to account for. */
  const std::vector<float> settleRateCurve(TEST_N_FRAMES, 1.0f);

  processor.SetPitchRateCurve(settleRateCurve.data(), TEST_N_FRAMES);
  fill_with_ramp(buffer, 0.0f);
  for (unsigned roundI = 0; roundI < 50; roundI++)
    untypedProcessor.Process(*pState, buffer);

  /* One round at a known, precomputed 2^(c/1200) rate - mirroring exactly
   * what a real mapper does ahead of time from its own cents curve (see
   * the class doc comment: this processor itself never calls exp2f()). */
  static constexpr float TEST_CENTS = 50.0f;
  const float expectedRate = powf(2.0f, TEST_CENTS / 1200.0f);
  const std::vector<float> testRateCurve(TEST_N_FRAMES, expectedRate);

  processor.SetPitchRateCurve(testRateCurve.data(), TEST_N_FRAMES);

  const double posBefore = pState->m_ReadHeadPosDesired;

  untypedProcessor.Process(*pState, buffer);

  double advance = pState->m_ReadHeadPosDesired - posBefore;

  if (advance < 0)
    advance += processor.m_NDelayBufferRingFrames;

  const float measuredRate = (float)(advance / TEST_N_FRAMES);

  GOAssert(
    std::fabs(measuredRate - expectedRate) < 0.01f,
    "a precomputed 2^(cents/1200) rate curve must be honored by the "
    "per-frame read rate (within the small lag-feedback term's own "
    "tolerance) - measured "
      + std::to_string(measuredRate) + ", expected "
      + std::to_string(expectedRate));
}

void GOTestSoundVibratoProcessor::TestLongRunDriftStaysBounded() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  processor.EnsureSetup(1, TEST_N_FRAMES, TEST_SAMPLE_RATE);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<GOSoundVibratoProcessorState> pState
    = processor.CreateTypedState();
  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 1, TEST_N_FRAMES);

  static constexpr float DEPTH_CENTS = 100.0f;
  static constexpr float LFO_FREQ_HZ = 3.0f;
  /* A simulated "long run": enough rounds to cover many LFO cycles and
   * several multiples of the lag-feedback time constant, without the
   * real-time cost of simulating minutes of audio at full sample-rate
   * granularity - TEST_N_FRAMES-sized rounds keep this test fast. */
  static constexpr unsigned N_ROUNDS = 2000;

  std::vector<float> rateCurve(TEST_N_FRAMES);
  unsigned minLag = ~0u;
  unsigned maxLag = 0;

  processor.SetPitchRateCurve(rateCurve.data(), TEST_N_FRAMES);
  for (unsigned roundI = 0; roundI < N_ROUNDS; roundI++) {
    for (unsigned i = 0; i < TEST_N_FRAMES; i++) {
      const float t = (float)(roundI * TEST_N_FRAMES + i) / TEST_SAMPLE_RATE;
      const float cents
        = DEPTH_CENTS * sinf(2.0f * (float)M_PI * LFO_FREQ_HZ * t);

      rateCurve[i] = powf(2.0f, cents / 1200.0f);
    }
    fill_with_ramp(buffer, 0.0f);
    untypedProcessor.Process(*pState, buffer);

    const unsigned lag = pState->GetNReadBehindWriteFrames();

    if (lag < minLag)
      minLag = lag;
    if (lag > maxLag)
      maxLag = lag;
  }

  GOAssert(
    minLag > GOSoundResample::MAX_POINTS,
    "the lag-feedback term must keep lag away from the floor clamp over "
    "a long symmetric +-100 cent run");
  GOAssert(
    maxLag < processor.m_NMaxLagFrames,
    "the lag-feedback term must keep lag away from the upper clamp over "
    "a long symmetric +-100 cent run - this is the test that would fail "
    "without the feedback term");
}

void GOTestSoundVibratoProcessor::TestBypassAfterActiveRelagsToD0() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  processor.EnsureSetup(1, TEST_N_FRAMES, TEST_SAMPLE_RATE);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<GOSoundVibratoProcessorState> pState
    = processor.CreateTypedState();
  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 1, TEST_N_FRAMES);
  const std::vector<float> rateCurve(TEST_N_FRAMES, 1.1f);

  processor.SetPitchRateCurve(rateCurve.data(), TEST_N_FRAMES);
  fill_with_ramp(buffer, 0.0f);
  for (unsigned roundI = 0; roundI < 20; roundI++)
    untypedProcessor.Process(*pState, buffer);

  GOAssert(
    pState->m_ReadHeadPosDesired
      != processor.m_NDelayBufferRingFrames - processor.m_NominalLagFrames,
    "test precondition: active processing with rate != 1.0 must have "
    "moved lag away from D0");

  processor.SetPitchRateCurve(nullptr, 0);
  untypedProcessor.Process(*pState, buffer);

  GOAssert(
    pState->m_ReadHeadPosDesired
      == processor.m_NDelayBufferRingFrames - processor.m_NominalLagFrames,
    "switching back to bypass must re-settle lag to exactly D0 on the "
    "very next Process() call");
}

void GOTestSoundVibratoProcessor::TestBothInterpolationTypesExercised() {
  for (GOSoundResample::InterpolationType interpolationType :
       {GOSoundResample::GO_LINEAR_INTERPOLATION,
        GOSoundResample::GO_POLYPHASE_INTERPOLATION}) {
    GOSoundResample resample;
    GOSoundVibratoProcessor processor(resample, interpolationType);

    processor.EnsureSetup(2, TEST_N_FRAMES, TEST_SAMPLE_RATE);

    GOSoundProcessor &untypedProcessor = processor;
    std::unique_ptr<GOSoundVibratoProcessorState> pState
      = processor.CreateTypedState();
    GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 2, TEST_N_FRAMES);
    const std::vector<float> rateCurve(TEST_N_FRAMES, 1.05f);

    processor.SetPitchRateCurve(rateCurve.data(), TEST_N_FRAMES);

    bool wasChanged = false;

    for (unsigned roundI = 0; roundI < 10; roundI++) {
      fill_with_ramp(buffer, 1.0f);
      untypedProcessor.Process(*pState, buffer);
      for (unsigned itemI = 0, n = buffer.GetNItems(); itemI < n; itemI++)
        if (!std::isfinite(buffer.GetData()[itemI]))
          GOAssert(
            false,
            "InterpolationType=" + std::to_string((int)interpolationType)
              + ": output must stay finite (no NaN/Inf)");
        else if (
          buffer.GetData()[itemI] != 1.0f + (float)(itemI % TEST_N_FRAMES))
          wasChanged = true;
    }
    GOAssert(
      wasChanged,
      "InterpolationType=" + std::to_string((int)interpolationType)
        + ": a non-unity rate curve must actually modulate the signal");
  }
}

void GOTestSoundVibratoProcessor::run() {
  TestBypassExactness();
  TestAllZeroCurveExactDelay();
  TestConstantRateClampSettlesToUnity();
  TestSinusoidalCurveRateTracksTheory();
  TestLongRunDriftStaysBounded();
  TestBypassAfterActiveRelagsToD0();
  TestBothInterpolationTypesExercised();
}
