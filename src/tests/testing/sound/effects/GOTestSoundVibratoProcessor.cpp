/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundVibratoProcessor.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

#include "sound/buffer/GOSoundBufferPlanarManaged.h"
#include "sound/buffer/GOSoundBufferPlanarMutable.h"
#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/effects/GOSoundVibratoPitchRateCurve.h"
#include "sound/effects/GOSoundVibratoProcessor.h"
#include "sound/effects/GOSoundVibratoProcessorState.h"
#include "sound/processing/GOSoundProcessor.h"

const std::string GOTestSoundVibratoProcessor::TEST_NAME
  = "GOTestSoundVibratoProcessor";

using Curve = GOSoundVibratoPitchRateCurve;
using State = GOSoundVibratoProcessorState;

static constexpr unsigned TEST_SAMPLE_RATE = 48000;
static constexpr unsigned UNIT_RATE = GOSoundResample::UPSAMPLE_FACTOR;
static constexpr unsigned L = State::N_LOOKAHEAD_FRAMES;

void GOTestSoundVibratoProcessor::TestStateConstructor() {
  static_assert(State::N_LOOKAHEAD_FRAMES == GOSoundResample::MAX_POINTS - 1);
  static_assert(State::N_DELAY_BUFFER_TAIL_FRAMES == State::N_LOOKAHEAD_FRAMES);

  const unsigned ring = 30;
  State state(2, ring);

  GOAssert(
    state.m_DelayBufferRingWithTail.GetNChannels() == 2
      && state.m_DelayBufferRingWithTail.GetNFrames()
        == ring + State::N_DELAY_BUFFER_TAIL_FRAMES,
    "the buffer must have the ring and the tail for every channel");
  GOAssert(
    state.m_DelayBufferWritePos == L
      && state.m_DelayBufferReadResamplingPos.GetIndex() == 0
      && state.m_DelayBufferReadResamplingPos.GetFraction() == 0,
    "a new state starts with W = L and R = 0");
  GOAssert(!state.m_IsDirty, "the constructor's reset clears the dirty flag");
  GOAssert(!state.m_CurvePosition.IsBound(), "the cursor is unbound");
  GOAssert(state.GetNFramesAvailableToRead() == 0, "nothing to read yet");
  GOAssert(
    state.GetNFramesAvailableToWrite() == ring - L, "the room is ring - L");
}

void GOTestSoundVibratoProcessor::TestStateAvailableFrames() {
  const unsigned ring = 20;
  State state(1, ring);

  struct Row {
    unsigned writePos;
    unsigned readIndex;
    unsigned expectedToWrite;
    unsigned expectedToRead;
  };
  const Row rows[] = {
    {19, 12, 13, 0}, // the start: the maximum room
    {4, 12, 8, 5},   // after a write of 5 with a wrap
    {4, 17, 13, 0},  // after a read of 5
    {9, 9, 0, 13},   // the full ring
  };

  for (const Row &row : rows) {
    state.m_DelayBufferWritePos = row.writePos;
    state.m_DelayBufferReadResamplingPos.Init(1.0f, row.readIndex);
    GOAssert(
      state.GetNFramesAvailableToWrite() == row.expectedToWrite,
      "wrong GetNFramesAvailableToWrite()");
    GOAssert(
      state.GetNFramesAvailableToRead() == row.expectedToRead,
      "wrong GetNFramesAvailableToRead()");
    GOAssert(
      state.GetNFramesAvailableToRead() + L + state.GetNFramesAvailableToWrite()
        == ring,
      "AvailToRead + L + AvailToWrite must be the ring");
  }
}

void GOTestSoundVibratoProcessor::TestStateWriteToDelayBuffer() {
  const unsigned ring = 20;
  const unsigned nChannels = 2;
  State state(nChannels, ring);
  std::mt19937 rng(7);
  // The model of the ring and which of the first L slots were written
  std::vector<std::vector<float>> model(
    nChannels, std::vector<float>(ring, 0.0f));
  std::vector<bool> isWritten(L, false);
  float nextValue = 1.0f;

  // Slots [0, L) are zero from the reset, so they count as written
  std::fill(isWritten.begin(), isWritten.end(), true);
  for (unsigned iterI = 0; iterI < 500; ++iterI) {
    const unsigned nFrames = 1 + rng() % 13;
    const unsigned writePos = state.m_DelayBufferWritePos;

    // Make the room for the write by placing the read index behind it
    state.m_DelayBufferReadResamplingPos.Init(
      1.0f, (writePos + (ring - L)) % ring);

    GOSoundBufferPlanarManaged src(nChannels, nFrames);

    for (unsigned channelI = 0; channelI < nChannels; ++channelI)
      for (unsigned frameI = 0; frameI < nFrames; ++frameI)
        src.GetChannelBuffer(channelI).GetData()[frameI] = nextValue++;
    state.WriteToDelayBuffer(src);
    for (unsigned frameI = 0; frameI < nFrames; ++frameI) {
      const unsigned slot = (writePos + frameI) % ring;

      for (unsigned channelI = 0; channelI < nChannels; ++channelI)
        model[channelI][slot]
          = src.GetChannelBuffer(channelI).GetData()[frameI];
      if (slot < L)
        isWritten[slot] = true;
    }
    GOAssert(
      state.m_DelayBufferWritePos == (writePos + nFrames) % ring,
      "the write position must advance by nFrames modulo the ring");
    for (unsigned channelI = 0; channelI < nChannels; ++channelI) {
      const float *pData
        = state.m_DelayBufferRingWithTail.GetChannelBuffer(channelI).GetData();

      for (unsigned slotI = 0; slotI < ring; ++slotI)
        GOAssert(
          pData[slotI] == model[channelI][slotI],
          "the ring content must match the model");
      for (unsigned tailI = 0; tailI < L; ++tailI)
        if (isWritten[tailI])
          GOAssert(
            pData[ring + tailI] == pData[tailI],
            "the tail must mirror the start of the ring");
    }
  }
}

void GOTestSoundVibratoProcessor::TestStateResetDelayBufferPositions() {
  const unsigned ring = 30;
  const unsigned nChannels = 2;
  State state(nChannels, ring);
  const float garbage = 12345.0f;

  // Fill the whole buffer, including the tail, with garbage
  for (unsigned channelI = 0; channelI < nChannels; ++channelI) {
    float *pData
      = state.m_DelayBufferRingWithTail.GetChannelBuffer(channelI).GetData();

    std::fill(pData, pData + ring + State::N_DELAY_BUFFER_TAIL_FRAMES, garbage);
  }
  // A clean state: the reset does nothing
  state.ResetDelayBufferPositions();
  GOAssert(
    state.m_DelayBufferRingWithTail.GetChannelBuffer(0).GetData()[0] == garbage,
    "a reset of a clean state must change nothing");

  state.MarkDirty();
  GOAssert(state.m_IsDirty, "MarkDirty() must set the flag");
  state.m_DelayBufferWritePos = 5;
  state.m_DelayBufferReadResamplingPos.Init(1.0f, 11);
  state.m_DelayBufferReadResamplingPos.Inc(100);
  state.ResetDelayBufferPositions();
  GOAssert(!state.m_IsDirty, "the reset clears the flag");
  GOAssert(
    state.m_DelayBufferWritePos == L
      && state.m_DelayBufferReadResamplingPos.GetIndex() == 0
      && state.m_DelayBufferReadResamplingPos.GetFraction() == 0,
    "the reset puts W = L and R = 0");
  GOAssert(state.GetNFramesAvailableToRead() == 0, "AvailToRead is 0");
  for (unsigned channelI = 0; channelI < nChannels; ++channelI) {
    const float *pData
      = state.m_DelayBufferRingWithTail.GetChannelBuffer(channelI).GetData();

    for (unsigned frameI = 0; frameI < L; ++frameI)
      GOAssert(pData[frameI] == 0.0f, "the first L frames must be zeroed");
    GOAssert(pData[L] == garbage, "the rest of the ring must not be cleared");
    GOAssert(
      pData[ring + 1] == garbage, "the tail must not be cleared by a reset");
  }
}

void GOTestSoundVibratoProcessor::TestStateFullReset() {
  const unsigned nFrames = 16;
  const std::vector<float> cents(nFrames, 0.0f);
  Curve curve;
  State state(1, nFrames + L);

  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, nFrames, nFrames);
  state.MarkDirty();
  state.m_CurvePosition.SelectChunk(&curve);
  GOAssert(state.m_CurvePosition.IsBound(), "the cursor must be bound");

  state.Reset();
  GOAssert(!state.m_CurvePosition.IsBound(), "Reset() must unbind the cursor");
  GOAssert(
    state.m_DelayBufferWritePos == L && !state.m_IsDirty,
    "Reset() must re-prime the positions");

  // A reset of a clean unbound state is harmless
  state.Reset();
  GOAssert(
    !state.m_CurvePosition.IsBound() && state.m_DelayBufferWritePos == L,
    "a repeated Reset() must be harmless");
}

void GOTestSoundVibratoProcessor::TestEnsureSetup() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  for (const unsigned sampleRate : {44100u, 48000u, 96000u}) {
    processor.EnsureSetup(2, 64, sampleRate);
    GOAssert(
      processor.m_NDelayBufferRingFrames
        == (unsigned)std::ceil(
             GOSoundVibratoProcessor::VIBRATO_MAX_LAG_MS / 1000.0f * sampleRate)
          + 64 + L,
      "the ring must cover the maximum lag, a round and the look-ahead");
  }
  processor.EnsureSetup(1, 20, TEST_SAMPLE_RATE, 0);
  GOAssert(
    processor.m_NDelayBufferRingFrames == 20 + L,
    "with no extra lag the ring is a round plus the look-ahead");

  // A repeated call unbinds the curve
  const std::vector<float> cents(20, 0.0f);
  Curve curve;

  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, 20, 20);
  processor.SetPitchRateCurve(&curve);
  GOAssert(processor.p_PitchRateCurve == &curve, "the curve must be set");
  processor.EnsureSetup(1, 20, TEST_SAMPLE_RATE, 0);
  GOAssert(
    !processor.p_PitchRateCurve
      && processor.m_PitchRateCurveId == Curve::NO_CURVE_ID,
    "EnsureSetup() must unbind the curve");
}

void GOTestSoundVibratoProcessor::TestSetPitchRateCurve() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);
  const std::vector<float> cents(20, 0.0f);
  Curve curve;

  processor.EnsureSetup(1, 20, TEST_SAMPLE_RATE, 0);
  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, 20, 20);
  processor.SetPitchRateCurve(&curve);
  GOAssert(
    processor.p_PitchRateCurve == &curve
      && processor.m_PitchRateCurveId == curve.GetId(),
    "the curve and its id must be stored");

  // The same curve: a no-op
  processor.SetPitchRateCurve(&curve);
  GOAssert(processor.p_PitchRateCurve == &curve, "an equal id is a no-op");

  // Rebuilt at the same address: a new id, so it is set again
  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, 20, 20);
  GOAssert(
    processor.m_PitchRateCurveId != curve.GetId(),
    "test precondition: the id must change on a rebuild");
  processor.SetPitchRateCurve(&curve);
  GOAssert(
    processor.m_PitchRateCurveId == curve.GetId(),
    "a rebuilt curve must be installed again");

  processor.SetPitchRateCurve(nullptr);
  GOAssert(
    !processor.p_PitchRateCurve
      && processor.m_PitchRateCurveId == Curve::NO_CURVE_ID,
    "nullptr must bypass");
}

void GOTestSoundVibratoProcessor::TestCreateTypedState() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  processor.EnsureSetup(2, 64, TEST_SAMPLE_RATE);

  const std::unique_ptr<State> pState = processor.CreateTypedState();

  GOAssert(
    pState->m_NDelayBufferRingFrames == processor.m_NDelayBufferRingFrames
      && pState->m_DelayBufferRingWithTail.GetNChannels() == 2
      && pState->m_DelayBufferRingWithTail.GetNFrames()
        == processor.m_NDelayBufferRingFrames
          + State::N_DELAY_BUFFER_TAIL_FRAMES,
    "the state must be sized for the last EnsureSetup()");
  GOAssert(
    pState->m_DelayBufferWritePos == L && !pState->m_CurvePosition.IsBound(),
    "the state must be primed with an unbound cursor");
}

/**
 * Makes nFrames increments whose sum is exactly totalUnits (spread evenly).
 */
static std::vector<unsigned> make_increments(
  unsigned nFrames, uint64_t totalUnits) {
  std::vector<unsigned> incs(nFrames, (unsigned)(totalUnits / nFrames));
  const unsigned nExtra = (unsigned)(totalUnits % nFrames);

  for (unsigned incI = 0; incI < nExtra; ++incI)
    ++incs[incI];
  return incs;
}

static uint64_t sum_of(const unsigned *pData, unsigned n) {
  return std::accumulate(pData, pData + n, (uint64_t)0);
}

void GOTestSoundVibratoProcessor::TestIncrementsZeroWidthWindow() {
  const unsigned nFrames = 20;
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  // No extra lag: the window is of zero width: min == max == nFrames
  processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE, 0);

  const unsigned availRead = nFrames;
  const unsigned availWrite = 0;

  for (const unsigned fraction : {0u, UNIT_RATE - 1}) {
    const uint64_t bound
      = GOSoundResample::computeMaxNUnitsToReach(fraction, nFrames);

    /* Exactly on the bound, one above and one below. One above is rescaled:
     * a non-zero fraction on the frame max ahead is not allowed */
    for (const int64_t delta : {-1, 0, 1}) {
      const bool isRescaled = delta != 0;
      const std::vector<unsigned> chunkIncs
        = make_increments(nFrames, bound + delta);
      const Curve::ChunkDescription chunk
        = {chunkIncs.data(), (unsigned)(bound + delta)};
      unsigned tmp[nFrames];
      const unsigned *pResult = processor.GetResamplerPositionIncrementsForUse(
        chunk, fraction, availRead, availWrite, tmp);

      GOAssert(
        sum_of(chunkIncs.data(), nFrames) == bound + delta,
        "the chunk must stay unmodified");
      if (isRescaled) {
        GOAssert(pResult == tmp, "a sum off the bound must be rescaled");
        GOAssert(
          sum_of(tmp, nFrames) == bound,
          "the rescaled sum must land exactly on the bound");
        for (unsigned frameI = 0; frameI < nFrames; ++frameI)
          GOAssert(
            (int)tmp[frameI] >= Curve::MIN_RATE_UNITS
              && (int)tmp[frameI] <= Curve::MAX_RATE_UNITS,
            "every rescaled rate must be in the range");
      } else
        GOAssert(
          pResult == chunkIncs.data(), "a sum on the bound is used as is");
    }
  }

  // 20 rates of 9216 (a whole frame and two over the bound) become exactly
  // 8192 each (the sum 163840)
  const std::vector<unsigned> chunkIncs(nFrames, UNIT_RATE + UNIT_RATE / 8);
  const Curve::ChunkDescription chunk
    = {chunkIncs.data(), (unsigned)sum_of(chunkIncs.data(), nFrames)};
  unsigned tmp[nFrames];

  processor.GetResamplerPositionIncrementsForUse(
    chunk, 0, availRead, availWrite, tmp);
  for (unsigned frameI = 0; frameI < nFrames; ++frameI)
    GOAssert(
      tmp[frameI] == UNIT_RATE, "every output rate must be exactly 8192");
}

void GOTestSoundVibratoProcessor::TestIncrementsWiderWindow() {
  const unsigned nFrames = 20;
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  // 10 extra frames of lag: ring = 20 + 10 + L
  processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE, 10);

  // avail read + avail write == ring - L == 30
  const unsigned availRead = 25;
  const unsigned availWrite = 5;
  const unsigned minToRead = nFrames - availWrite; // 15
  const unsigned maxToRead = availRead;            // 25

  for (const unsigned fraction : {0u, 1u, UNIT_RATE - 1}) {
    const uint64_t minUnits
      = GOSoundResample::computeMinNUnitsToReach(fraction, minToRead);
    const uint64_t maxUnits
      = GOSoundResample::computeMaxNUnitsToReach(fraction, maxToRead);

    struct Case {
      uint64_t units;
      bool isRescaled;
      uint64_t expectedUnits; // when rescaled
    };
    const Case cases[] = {
      {minUnits, false, 0}, // exactly the lower bound: valid
      {minUnits - 1, true, minUnits},
      {maxUnits - 1, false, 0}, // just inside
      {maxUnits, false, 0},     // on the bound with a zero fraction: valid
      // on the frame max ahead with a non-zero fraction: rescaled
      {maxUnits + 1, true, maxUnits},
    };

    for (const Case &testCase : cases) {
      const std::vector<unsigned> chunkIncs
        = make_increments(nFrames, testCase.units);
      const Curve::ChunkDescription chunk
        = {chunkIncs.data(), (unsigned)testCase.units};
      unsigned tmp[nFrames];
      const unsigned *pResult = processor.GetResamplerPositionIncrementsForUse(
        chunk, fraction, availRead, availWrite, tmp);

      if (testCase.isRescaled) {
        GOAssert(pResult == tmp, "an out-of-window chunk must be rescaled");
        GOAssert(
          sum_of(tmp, nFrames) == testCase.expectedUnits,
          "the rescaled sum must land exactly on the violated bound");
      } else
        GOAssert(pResult == chunkIncs.data(), "a valid chunk is used as is");
    }
  }

  // min == 0: a tiny sum is never rescaled
  processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE, 20);

  const std::vector<unsigned> slow(nFrames, (unsigned)Curve::MIN_RATE_UNITS);
  const Curve::ChunkDescription chunk
    = {slow.data(), (unsigned)sum_of(slow.data(), nFrames)};
  unsigned tmp[nFrames];

  GOAssert(
    processor.GetResamplerPositionIncrementsForUse(chunk, 0, 20, 20, tmp)
      == slow.data(),
    "with min == 0 a slow chunk must be used as is");
}

void GOTestSoundVibratoProcessor::TestIncrementsRandom() {
  const unsigned nFrames = 20;
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);
  std::mt19937 rng(99);

  for (const unsigned nMaxLag : {0u, 3u, 10u, 30u}) {
    processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE, nMaxLag);

    const unsigned ringMinusL = nFrames + nMaxLag;

    for (unsigned iterI = 0; iterI < 2000; ++iterI) {
      const unsigned availRead = nFrames + rng() % (nMaxLag + 1);
      const unsigned availWrite = ringMinusL - availRead;
      const unsigned fraction = rng() % UNIT_RATE;
      std::vector<unsigned> chunkIncs(nFrames);

      for (unsigned &inc : chunkIncs)
        inc = Curve::MIN_ALLOWED_RATE_UNITS
          + rng()
            % (Curve::MAX_ALLOWED_RATE_UNITS - Curve::MIN_ALLOWED_RATE_UNITS);

      const Curve::ChunkDescription chunk
        = {chunkIncs.data(), (unsigned)sum_of(chunkIncs.data(), nFrames)};
      unsigned tmp[nFrames];
      const unsigned *pResult = processor.GetResamplerPositionIncrementsForUse(
        chunk, fraction, availRead, availWrite, tmp);
      const uint64_t resultUnits = sum_of(pResult, nFrames);
      const unsigned minToRead
        = availWrite < nFrames ? nFrames - availWrite : 0;
      const uint64_t minUnits
        = GOSoundResample::computeMinNUnitsToReach(fraction, minToRead);
      const uint64_t maxUnits
        = GOSoundResample::computeMaxNUnitsToReach(fraction, availRead);

      GOAssert(
        resultUnits >= minUnits && resultUnits <= maxUnits,
        "the result must always lie inside the window of valid sums");
      // What the upper bound is for: every tap of the last read is written
      GOAssert(
        GOSoundResample::getIndexIncrementByUnits(
          fraction, resultUnits - pResult[nFrames - 1])
          < availRead,
        "the last read must be before the frame max ahead");
      if (pResult == chunkIncs.data())
        GOAssert(
          resultUnits == chunk.nTotalUnits, "an unchanged chunk keeps its sum");
      else
        GOAssert(
          resultUnits == minUnits || resultUnits == maxUnits,
          "a rescaled chunk must land exactly on a bound");
      for (unsigned frameI = 0; frameI < nFrames; ++frameI)
        GOAssert(
          (int)pResult[frameI] >= Curve::MIN_RATE_UNITS
            && (int)pResult[frameI] <= Curve::MAX_RATE_UNITS,
          "every increment must be in the range");
    }
  }
}

void GOTestSoundVibratoProcessor::TestReadFromDelayBuffer() {
  const unsigned nFrames = 20;
  GOSoundResample resample;

  for (const GOSoundResample::InterpolationType type :
       {GOSoundResample::GO_LINEAR_INTERPOLATION,
        GOSoundResample::GO_POLYPHASE_INTERPOLATION}) {
    GOSoundVibratoProcessor processor(resample, type);

    processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE, 0);

    const unsigned ring = processor.m_NDelayBufferRingFrames;
    std::unique_ptr<State> pState = processor.CreateTypedState();
    float *pData
      = pState->m_DelayBufferRingWithTail.GetChannelBuffer(0).GetData();

    // A ramp in the ring and the mirrored tail
    for (unsigned slotI = 0; slotI < ring; ++slotI)
      pData[slotI] = (float)(slotI + 1);
    for (unsigned tailI = 0; tailI < L; ++tailI)
      pData[ring + tailI] = pData[tailI];

    // The read position near the end of the ring: the window crosses the seam
    const unsigned startIndex = ring - 5;

    pState->m_DelayBufferReadResamplingPos.Init(1.0f, startIndex);

    const std::vector<unsigned> incs(nFrames, UNIT_RATE);
    GOSoundBufferPlanarManaged out(1, nFrames);

    processor.ReadFromDelayBuffer(*pState, incs.data(), out);

    GOAssert(
      pState->m_DelayBufferReadResamplingPos.GetIndex()
        == (startIndex + nFrames) % ring,
      "the read index must advance by the summed increments modulo the ring");
    GOAssert(
      pState->m_DelayBufferReadResamplingPos.GetFraction() == 0,
      "the fraction must stay 0");

    const float *pOut = out.GetChannelBuffer(0).GetData();

    if (type == GOSoundResample::GO_LINEAR_INTERPOLATION)
      for (unsigned frameI = 0; frameI < nFrames; ++frameI)
        GOAssert(
          pOut[frameI] == (float)((startIndex + frameI) % ring + 1),
          "linear: the output must be the ring read from the start index");
    else
      for (unsigned frameI = 0; frameI < nFrames; ++frameI)
        GOAssert(std::isfinite(pOut[frameI]), "polyphase: finite output");
  }
}

/**
 * Processes nRounds rounds of a mono signal and returns the output.
 */
static std::vector<float> run_rounds(
  GOSoundProcessor &processor,
  GOSoundProcessorState &state,
  unsigned nFrames,
  const std::vector<float> &input) {
  const unsigned nRounds = (unsigned)input.size() / nFrames;
  std::vector<float> output(input.size());
  GOSoundBufferPlanarManaged buffer(1, nFrames);

  for (unsigned roundI = 0; roundI < nRounds; ++roundI) {
    float *pData = buffer.GetChannelBuffer(0).GetData();

    std::copy(
      input.begin() + roundI * nFrames,
      input.begin() + (roundI + 1) * nFrames,
      pData);
    processor.Process(state, buffer);
    std::copy(pData, pData + nFrames, output.begin() + roundI * nFrames);
  }
  return output;
}

static std::vector<float> make_ramp(unsigned n) {
  std::vector<float> ramp(n);

  for (unsigned i = 0; i < n; ++i)
    ramp[i] = (float)(i + 1);
  return ramp;
}

void GOTestSoundVibratoProcessor::TestProcessBypass() {
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(resample);

  processor.EnsureSetup(2, 64, TEST_SAMPLE_RATE);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<State> pState = processor.CreateTypedState();
  GOSoundBufferPlanarManaged buffer(2, 64);

  for (unsigned itemI = 0, n = buffer.GetNItems(); itemI < n; ++itemI)
    buffer.GetData()[itemI] = (float)(itemI + 1);

  const std::vector<float> before(
    buffer.GetData(), buffer.GetData() + buffer.GetNItems());

  untypedProcessor.Process(*pState, buffer);
  for (unsigned itemI = 0, n = buffer.GetNItems(); itemI < n; ++itemI)
    GOAssert(
      buffer.GetData()[itemI] == before[itemI],
      "a bypass round must leave the buffer untouched");
  GOAssert(
    pState->m_DelayBufferWritePos == L && !pState->m_IsDirty
      && !pState->m_CurvePosition.IsBound(),
    "a bypass round must keep the state primed and the cursor unbound");
}

void GOTestSoundVibratoProcessor::TestProcessUnityCurveDelay() {
  const unsigned nFrames = 64;
  const unsigned nRounds = 30;
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(
    resample, GOSoundResample::GO_LINEAR_INTERPOLATION);

  processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE);

  const std::vector<float> cents(100, 0.0f);
  Curve curve;

  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, 100, nFrames);
  processor.SetPitchRateCurve(&curve);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<State> pState = processor.CreateTypedState();
  const std::vector<float> input = make_ramp(nRounds * nFrames);
  const std::vector<float> output
    = run_rounds(untypedProcessor, *pState, nFrames, input);

  for (unsigned n = 0; n < output.size(); ++n)
    GOAssert(
      output[n] == (n >= L ? input[n - L] : 0.0f),
      "a zero-cents curve must delay the signal by exactly the look-ahead");
}

void GOTestSoundVibratoProcessor::TestProcessRescaledToExactDelay() {
  GOSoundResample resample;

  for (const unsigned nFrames : {20u, 128u, 2048u})
    for (const float centsValue : {1200.0f, 12.0f, -12.0f}) {
      GOSoundVibratoProcessor processor(
        resample, GOSoundResample::GO_LINEAR_INTERPOLATION);

      /* No extra lag: a window of zero width, every round is rescaled (above
       * for positive cents, below for negative ones) */
      processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE, 0);

      const std::vector<float> cents(50, centsValue);
      Curve curve;

      // Not normalised: Build() would normalise a constant curve to 1.0
      curve.BuildInternal(
        cents.data(), TEST_SAMPLE_RATE, 0, 50, nFrames, false);
      processor.SetPitchRateCurve(&curve);

      GOSoundProcessor &untypedProcessor = processor;
      std::unique_ptr<State> pState = processor.CreateTypedState();
      const std::vector<float> input = make_ramp(40 * nFrames);
      const std::vector<float> output
        = run_rounds(untypedProcessor, *pState, nFrames, input);

      for (unsigned n = 0; n < output.size(); ++n)
        GOAssert(
          output[n] == (n >= L ? input[n - L] : 0.0f),
          "a constant curve in a zero-width window must give an exact delay "
          "of the look-ahead, with no frame dropped or repeated");
    }
}

void GOTestSoundVibratoProcessor::TestProcessOctaveDownSettles() {
  const unsigned nFrames = 20;
  const unsigned nMaxLag = 50;
  const unsigned nRounds = 100;
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(
    resample, GOSoundResample::GO_LINEAR_INTERPOLATION);

  processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE, nMaxLag);

  const std::vector<float> cents(50, -1200.0f);
  Curve curve;

  curve.BuildInternal(cents.data(), TEST_SAMPLE_RATE, 0, 50, nFrames, false);
  processor.SetPitchRateCurve(&curve);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<State> pState = processor.CreateTypedState();
  const std::vector<float> input = make_ramp(nRounds * nFrames);
  const std::vector<float> output
    = run_rounds(untypedProcessor, *pState, nFrames, input);

  for (unsigned n = 1; n < output.size(); ++n) {
    GOAssert(output[n] >= output[n - 1], "the output must never go backwards");
    GOAssert(output[n] <= input[n], "the output must not read the future");
    if (output[n] > 0.0f) {
      const float lag = input[n] - output[n];

      GOAssert(
        lag >= (float)L - 1.0f && lag <= (float)(nMaxLag + L + nFrames),
        "the lag must stay between the look-ahead and the maximum");
    }
  }
  // Settled: the last rounds are read at the unit rate
  for (unsigned n = (nRounds - 5) * nFrames; n + 1 < output.size(); ++n)
    GOAssert(
      std::fabs(output[n + 1] - output[n] - 1.0f) < 1e-3f,
      "after the lag has grown to its maximum the slope must be 1");
}

void GOTestSoundVibratoProcessor::TestProcessNormalisedCurve() {
  const unsigned nFrames = 64;
  const unsigned loopLen = 9600; // one cycle of 5 Hz at 48 kHz
  const unsigned nRounds = 450;  // three cycles
  GOSoundResample resample;
  std::vector<float> cents(loopLen);

  for (unsigned frameI = 0; frameI < loopLen; ++frameI)
    cents[frameI]
      = 100.0f * std::sin(2.0f * (float)M_PI * (float)frameI / loopLen);

  Curve curve;

  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, loopLen, nFrames);
  for (const GOSoundResample::InterpolationType type :
       {GOSoundResample::GO_LINEAR_INTERPOLATION,
        GOSoundResample::GO_POLYPHASE_INTERPOLATION}) {
    GOSoundVibratoProcessor processor(resample, type);

    processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE);
    processor.SetPitchRateCurve(&curve);

    GOSoundProcessor &untypedProcessor = processor;
    std::unique_ptr<State> pState = processor.CreateTypedState();
    const std::vector<float> input = make_ramp(nRounds * nFrames);
    std::vector<float> output(input.size());
    GOSoundBufferPlanarManaged buffer(1, nFrames);
    const float maxLag
      = (float)(processor.m_NDelayBufferRingFrames + nFrames + 16);

    for (unsigned roundI = 0; roundI < nRounds; ++roundI) {
      float *pData = buffer.GetChannelBuffer(0).GetData();

      std::copy(
        input.begin() + roundI * nFrames,
        input.begin() + (roundI + 1) * nFrames,
        pData);
      untypedProcessor.Process(*pState, buffer);
      std::copy(pData, pData + nFrames, output.begin() + roundI * nFrames);

      /* The geometry invariant, independent of the kernel: the read position
       * after the round is at or before W - L (a non-zero fraction needs at
       * least one whole frame to spare), so that every tap of every used
       * position was a written frame */
      GOAssert(
        pState->GetNFramesAvailableToRead()
          >= (pState->m_DelayBufferReadResamplingPos.GetFraction() ? 1u : 0u),
        "the read position must never get ahead of the written frames");
    }
    // The ramp checks are exact only for the linear interpolation: the
    // polyphase kernel deviates from the ramp where the position has a
    // fraction
    if (type == GOSoundResample::GO_LINEAR_INTERPOLATION)
      for (unsigned n = L + 16; n < output.size(); ++n) {
        /* A round writes all its frames before reading, so the output may be
         * ahead of input[n] by the rest of the round, but not beyond it */
        const unsigned roundLastFrame = (n / nFrames + 1) * nFrames - 1;

        GOAssert(
          output[n] >= output[n - 1],
          "no stale or unwritten ring slot may be read: the ramp must stay "
          "monotonic");
        GOAssert(
          output[n] <= input[roundLastFrame] && input[n] - output[n] <= maxLag,
          "the lag must stay bounded over several cycles");
      }
  }
}

void GOTestSoundVibratoProcessor::TestProcessBypassAfterActiveAndRestart() {
  const unsigned nFrames = 64;
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(
    resample, GOSoundResample::GO_LINEAR_INTERPOLATION);

  processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE);

  std::vector<float> cents(100);

  for (unsigned frameI = 0; frameI < 100; ++frameI)
    cents[frameI] = 200.0f * std::sin(0.3f * (float)frameI);

  Curve curve;

  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, 100, nFrames);
  processor.SetPitchRateCurve(&curve);

  GOSoundProcessor &untypedProcessor = processor;
  const std::vector<float> input = make_ramp(20 * nFrames);
  std::unique_ptr<State> pFresh = processor.CreateTypedState();
  const std::vector<float> reference
    = run_rounds(untypedProcessor, *pFresh, nFrames, input);

  // Active rounds, then a bypass round
  std::unique_ptr<State> pState = processor.CreateTypedState();

  run_rounds(untypedProcessor, *pState, nFrames, make_ramp(3 * nFrames));
  GOAssert(pState->m_CurvePosition.IsBound(), "active: the cursor is bound");
  processor.SetPitchRateCurve(nullptr);

  GOSoundBufferPlanarManaged buffer(1, nFrames);

  for (unsigned i = 0; i < nFrames; ++i)
    buffer.GetChannelBuffer(0).GetData()[i] = (float)(i + 1);
  untypedProcessor.Process(*pState, buffer);
  for (unsigned i = 0; i < nFrames; ++i)
    GOAssert(
      buffer.GetChannelBuffer(0).GetData()[i] == (float)(i + 1),
      "a bypass round must leave the buffer untouched");
  GOAssert(
    pState->m_DelayBufferWritePos == L
      && pState->m_DelayBufferReadResamplingPos.GetIndex() == 0
      && !pState->m_IsDirty,
    "a bypass round must re-prime the delay positions");

  // Active again: it must start as a fresh state does
  processor.SetPitchRateCurve(&curve);
  GOAssert(
    run_rounds(untypedProcessor, *pState, nFrames, input) == reference,
    "an activation after a bypass must start from the minimum lag and the "
    "start of the curve");

  // A restart of the chain: the full Reset() rewinds the cursor too
  pState->Reset();
  GOAssert(!pState->m_CurvePosition.IsBound(), "Reset() unbinds the cursor");
  GOAssert(
    run_rounds(untypedProcessor, *pState, nFrames, input) == reference,
    "after a full reset the curve must restart at its beginning");
}

void GOTestSoundVibratoProcessor::TestProcessGarbageNotLeaked() {
  const unsigned nFrames = 20;
  GOSoundResample resample;
  GOSoundVibratoProcessor processor(
    resample, GOSoundResample::GO_LINEAR_INTERPOLATION);

  // A tiny ring: many laps in a few rounds
  processor.EnsureSetup(1, nFrames, TEST_SAMPLE_RATE, 0);

  const std::vector<float> cents(50, 0.0f);
  Curve curve;

  curve.Build(cents.data(), TEST_SAMPLE_RATE, 0, 50, nFrames);
  processor.SetPitchRateCurve(&curve);

  GOSoundProcessor &untypedProcessor = processor;
  std::unique_ptr<State> pState = processor.CreateTypedState();
  float *pData
    = pState->m_DelayBufferRingWithTail.GetChannelBuffer(0).GetData();

  // Garbage everywhere, including the tail, then a full reset
  std::fill(
    pData,
    pData + processor.m_NDelayBufferRingFrames
      + State::N_DELAY_BUFFER_TAIL_FRAMES,
    1.0e6f);
  pState->MarkDirty();
  pState->Reset();

  std::vector<float> input(40 * nFrames);

  for (unsigned i = 0; i < input.size(); ++i)
    input[i] = (float)(i % 100 + 1);

  const std::vector<float> output
    = run_rounds(untypedProcessor, *pState, nFrames, input);

  for (unsigned n = 0; n < output.size(); ++n)
    GOAssert(
      std::fabs(output[n]) <= 100.0f,
      "no garbage of the delay buffer may reach the output");
}

void GOTestSoundVibratoProcessor::run() {
  TestStateConstructor();
  TestStateAvailableFrames();
  TestStateWriteToDelayBuffer();
  TestStateResetDelayBufferPositions();
  TestStateFullReset();
  TestEnsureSetup();
  TestSetPitchRateCurve();
  TestCreateTypedState();
  TestIncrementsZeroWidthWindow();
  TestIncrementsWiderWindow();
  TestIncrementsRandom();
  TestReadFromDelayBuffer();
  TestProcessBypass();
  TestProcessUnityCurveDelay();
  TestProcessRescaledToExactDelay();
  TestProcessOctaveDownSettles();
  TestProcessNormalisedCurve();
  TestProcessBypassAfterActiveAndRestart();
  TestProcessGarbageNotLeaked();
}
