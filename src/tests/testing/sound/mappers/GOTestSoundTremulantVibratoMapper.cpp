/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundTremulantVibratoMapper.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "sound/buffer/GOSoundBufferPlanarManaged.h"
#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/effects/GOSoundVibratoPitchRateCurve.h"
#include "sound/effects/GOSoundVibratoProcessor.h"
#include "sound/effects/GOSoundVibratoProcessorState.h"
#include "sound/mappers/GOSoundTremulantVibratoMapper.h"
#include "sound/processing/GOSoundProcessor.h"

#include "../GOSoundTestSignals.h"
#include "../tasks/GOSoundTremulantTaskTestFixture.h"
#include "GOTestScope.h"

const std::string GOTestSoundTremulantVibratoMapper::TEST_NAME
  = "GOTestSoundTremulantVibratoMapper";

using Chunk = GOSoundVibratoPitchRateCurve::ChunkDescription;

namespace {

// A real task with a wave form feeding a real vibrato processor through the
// mapper under test
struct MapperFixture {
  GOSoundTremulantTaskTestFixture taskFixture;
  GOSoundResample resample;
  GOSoundVibratoProcessor processor;
  GOSoundTremulantVibratoMapper mapper;

  explicit MapperFixture(
    float pitchCents,
    unsigned nStartFrames = TASK_TEST_N_START,
    unsigned nStopFrames = TASK_TEST_N_STOP,
    unsigned nLoopFrames = TASK_TEST_N_LOOP)
    : taskFixture(pitchCents, nStartFrames, nStopFrames, nLoopFrames),
      processor(resample, GOSoundResample::GO_POLYPHASE_INTERPOLATION),
      mapper(taskFixture.task, processor) {
    processor.EnsureSetup(1, TASK_TEST_N_FRAMES, TASK_TEST_SAMPLE_RATE);
  }
};

} // namespace

// An impossible chunk: no task ever publishes it
static const Chunk CANARY_CHUNK = {(const uint16_t *)1, 0xFFFF, 0xFFFFFFFF};

static bool is_chunk_equal(const Chunk &expected, const Chunk &actual) {
  return expected.pRateUnits == actual.pRateUnits
    && expected.nFrames == actual.nFrames
    && expected.nTotalUnits == actual.nTotalUnits;
}

void GOTestSoundTremulantVibratoMapper::TestPassesChunk() {
  MapperFixture fixture(TASK_TEST_PITCH_CENTS);

  fixture.taskFixture.SetEngaged(true);
  fixture.taskFixture.RunRound();

  const Chunk &taskChunk = fixture.taskFixture.task.GetPitchRateChunk();

  GOAssert(taskChunk.pRateUnits, "the test task must publish rates");
  fixture.processor.m_PitchRateChunk = CANARY_CHUNK;
  fixture.mapper.EnsureParametersUpToDate();
  GOAssert(
    is_chunk_equal(taskChunk, fixture.processor.m_PitchRateChunk),
    "the processor must get the chunk of the task");
}

void GOTestSoundTremulantVibratoMapper::TestPassesBypassChunk() {
  MapperFixture fixture(0.0f);

  fixture.taskFixture.SetEngaged(true);
  fixture.taskFixture.RunRound();
  fixture.processor.m_PitchRateChunk = CANARY_CHUNK;
  fixture.mapper.EnsureParametersUpToDate();
  GOAssert(
    is_chunk_equal({nullptr, 0, 0}, fixture.processor.m_PitchRateChunk),
    "a task without pitch must bypass the processor");

  MapperFixture disengaged(TASK_TEST_PITCH_CENTS);

  disengaged.taskFixture.RunRound();
  disengaged.processor.m_PitchRateChunk = CANARY_CHUNK;
  disengaged.mapper.EnsureParametersUpToDate();
  GOAssert(
    is_chunk_equal({nullptr, 0, 0}, disengaged.processor.m_PitchRateChunk),
    "a disengaged task must bypass the processor");
}

void GOTestSoundTremulantVibratoMapper::TestFollowsRounds() {
  MapperFixture fixture(TASK_TEST_PITCH_CENTS);

  fixture.taskFixture.SetEngaged(true);
  // During the start ramp the chunk is in the buffer of the task and does not
  // move, so the ramp is run through first
  for (unsigned roundI = 0; roundI < TASK_TEST_N_START / TASK_TEST_N_FRAMES + 2;
       ++roundI)
    fixture.taskFixture.RunRound();
  fixture.mapper.EnsureParametersUpToDate();

  const Chunk first = fixture.processor.m_PitchRateChunk;

  fixture.taskFixture.RunRound();
  fixture.mapper.EnsureParametersUpToDate();
  GOAssert(
    is_chunk_equal(
      fixture.taskFixture.task.GetPitchRateChunk(),
      fixture.processor.m_PitchRateChunk),
    "the processor must get the chunk of the second round");
  GOAssert(
    fixture.processor.m_PitchRateChunk.pRateUnits != first.pRateUnits,
    "the chunk must move from round to round");
}

void GOTestSoundTremulantVibratoMapper::TestEndToEnd() {
  using State = GOSoundVibratoProcessorState;

  constexpr unsigned N_FRAMES = TASK_TEST_N_FRAMES;
  constexpr unsigned N_LOOP_FRAMES = 6000;
  constexpr unsigned N_LOOPS = 3;
  constexpr unsigned N_ROUNDS = N_LOOPS * N_LOOP_FRAMES / N_FRAMES;
  constexpr double CARRIER_HZ = 440.0;
  constexpr double TOLERANCE_CENTS = 3.0;
  const std::vector<float> input = GOSoundTestSignals::makeSine(
    N_ROUNDS * N_FRAMES, CARRIER_HZ, TASK_TEST_SAMPLE_RATE);

  // pitchCents == 0: the output is the input, bit for bit
  {
    MapperFixture fixture(0.0f);
    GOSoundProcessor &processor = fixture.processor;
    std::unique_ptr<State> oState = fixture.processor.CreateTypedState();
    GOSoundBufferPlanarManaged buffer(1, N_FRAMES);
    bool isIdentical = true;

    fixture.taskFixture.SetEngaged(true);
    for (unsigned roundI = 0; roundI < N_ROUNDS; ++roundI) {
      float *pData = buffer.GetChannelBuffer(0).GetData();

      std::copy(
        input.begin() + roundI * N_FRAMES,
        input.begin() + (roundI + 1) * N_FRAMES,
        pData);
      fixture.taskFixture.RunRound();
      fixture.mapper.EnsureParametersUpToDate();
      processor.Process(*oState, buffer);
      isIdentical = isIdentical
        && std::equal(pData,
                      pData + N_FRAMES,
                      input.begin() + roundI * N_FRAMES);
    }
    GOAssert(isIdentical, "without pitch the signal must pass unchanged");
  }

  // With pitch: the pitch of the output follows the rates of the chunks
  {
    // A negative peak first: the lag of the delay line only grows from its
    // initial minimum and never has to be limited
    MapperFixture fixture(-100.0f, TASK_TEST_N_START, 0, N_LOOP_FRAMES);
    GOSoundProcessor &processor = fixture.processor;
    std::unique_ptr<State> oState = fixture.processor.CreateTypedState();
    GOSoundBufferPlanarManaged buffer(1, N_FRAMES);
    std::vector<float> output(input.size());
    std::vector<double> rates(input.size(), 1.0);

    fixture.taskFixture.SetEngaged(true);
    for (unsigned roundI = 0; roundI < N_ROUNDS; ++roundI) {
      float *pData = buffer.GetChannelBuffer(0).GetData();

      std::copy(
        input.begin() + roundI * N_FRAMES,
        input.begin() + (roundI + 1) * N_FRAMES,
        pData);
      fixture.taskFixture.RunRound();
      fixture.mapper.EnsureParametersUpToDate();

      const Chunk &chunk = fixture.processor.m_PitchRateChunk;

      for (unsigned frameI = 0; chunk.pRateUnits && frameI < chunk.nFrames;
           ++frameI)
        rates[roundI * N_FRAMES + frameI]
          = (double)chunk.pRateUnits[frameI] / GOSoundResample::UPSAMPLE_FACTOR;
      processor.Process(*oState, buffer);
      std::copy(pData, pData + N_FRAMES, output.begin() + roundI * N_FRAMES);
    }

    unsigned nPeriods = 0;
    // The start ramp and the first loop are skipped
    const double maxErrorCents = GOSoundTestSignals::maxPitchErrorCents(
      output,
      N_LOOP_FRAMES + TASK_TEST_N_START,
      CARRIER_HZ,
      TASK_TEST_SAMPLE_RATE,
      rates,
      nPeriods);

    GOAssert(nPeriods > 20, "there must be enough periods to measure");
    GOAssert(
      maxErrorCents <= TOLERANCE_CENTS,
      "the pitch must follow the rates of the chunks");
    GOAssert(output != input, "the pitch modulation must change the signal");
  }
}

void GOTestSoundTremulantVibratoMapper::run() {
  GO_RUN_TEST(TestPassesChunk())
  GO_RUN_TEST(TestPassesBypassChunk())
  GO_RUN_TEST(TestFollowsRounds())
  GO_RUN_TEST(TestEndToEnd())
}
