/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundVibratoProcessor.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "sound/buffer/GOSoundBufferPlanarMutable.h"

GOSoundVibratoProcessor::GOSoundVibratoProcessor(
  const GOSoundResample &resample,
  GOSoundResample::InterpolationType interpolationType)
  : r_resample(resample), m_InterpolationType(interpolationType) {}

void GOSoundVibratoProcessor::EnsureSetup(
  unsigned nChannels,
  unsigned nFrames,
  unsigned sampleRate,
  unsigned nMaxLagFrames) {
  /* normalisePitchRateCurve() needs the target mean to be within the allowed
   * range, which a one-frame round cannot guarantee */
  assert(nFrames >= 2);

  m_NChannels = nChannels;
  m_SampleRate = sampleRate;
  m_NFramesPerProcess = nFrames;
  m_NDelayBufferRingFrames = nMaxLagFrames + nFrames
    + GOSoundVibratoProcessorState::N_LOOKAHEAD_FRAMES;
  p_PitchRateCurve = nullptr;
  m_PitchRateCurveId = GOSoundVibratoPitchRateCurve::NO_CURVE_ID;
  m_IsSetupCalled = true;
}

void GOSoundVibratoProcessor::EnsureSetup(
  unsigned nChannels, unsigned nFrames, unsigned sampleRate) {
  EnsureSetup(
    nChannels,
    nFrames,
    sampleRate,
    (unsigned)std::ceil(VIBRATO_MAX_LAG_MS / 1000.0f * sampleRate));
}

void GOSoundVibratoProcessor::SetPitchRateCurve(
  const GOSoundVibratoPitchRateCurve *pCurve) {
  const uint64_t curveId
    = pCurve ? pCurve->GetId() : GOSoundVibratoPitchRateCurve::NO_CURVE_ID;

  if (curveId != m_PitchRateCurveId) {
    assert(
      !pCurve
      || (pCurve->GetSampleRate() == m_SampleRate && pCurve->GetNFramesPerProcess() == m_NFramesPerProcess));
    p_PitchRateCurve = pCurve;
    m_PitchRateCurveId = curveId;
  }
}

std::unique_ptr<GOSoundVibratoProcessorState> GOSoundVibratoProcessor::
  CreateTypedState() const {
  assert(
    m_IsSetupCalled && "EnsureSetup() must be called before CreateState()");

  return std::make_unique<GOSoundVibratoProcessorState>(
    m_NChannels, m_NDelayBufferRingFrames);
}

const unsigned *GOSoundVibratoProcessor::GetResamplerPositionIncrementsForUse(
  const GOSoundVibratoPitchRateCurve::ChunkDescription &chunk,
  unsigned readPosFraction,
  unsigned nFramesAvailableToRead,
  unsigned nFramesAvailableToWrite,
  unsigned *pTmpIncrements) const {
  const unsigned nFrames = m_NFramesPerProcess;

  assert(nFramesAvailableToRead >= nFrames);
  assert(
    nFramesAvailableToRead + nFramesAvailableToWrite
      + GOSoundVibratoProcessorState::N_LOOKAHEAD_FRAMES
    == m_NDelayBufferRingFrames);
  assert(chunk.HasRates());

  const unsigned *pResult = chunk.pRateUnits;
  /* Both bounds are inclusive. At least nFrames must stay free for the next
   * period's write, so at least this many frames must be read. */
  const unsigned minNFramesToRead
    = nFramesAvailableToWrite < nFrames ? nFrames - nFramesAvailableToWrite : 0;
  // It is impossible to read more than is available
  const unsigned maxNFramesToRead = nFramesAvailableToRead;

  assert(minNFramesToRead <= maxNFramesToRead);

  /* Check where the read position would be after the chunk: first coarsely,
   * by the whole frames it advances; only on the frame max ahead exactly, by
   * the sum of the increments. If a bound is violated, take the sum to
   * rescale the chunk to. */
  const unsigned nFramesToReadWanted
    = GOSoundResample::getIndexIncrementByUnits(
      readPosFraction, chunk.nTotalUnits);
  // The sum to rescale the chunk to; 0 if no bound is violated
  uint64_t nUnitsToBeRead = 0;

  if (nFramesToReadWanted >= maxNFramesToRead) {
    /* The whole frames are not enough here: the final position on the frame
     * max ahead is allowed only with a zero fraction. The resampler reads a
     * frame before advancing, so the last read is one increment before the
     * final position. With a non-zero fraction the last read may still be on
     * the frame max ahead, whose taps are not written yet; with a zero
     * fraction it is strictly before it, since every increment is positive. */
    const uint64_t maxNUnitsToRead = GOSoundResample::computeMaxNUnitsToReach(
      readPosFraction, maxNFramesToRead);

    // Every increment is positive: the curve and the rescale keep them at least
    // MIN_RATE_UNITS
    static_assert(GOSoundVibratoPitchRateCurve::MIN_RATE_UNITS > 0);

    if (chunk.nTotalUnits > maxNUnitsToRead)
      nUnitsToBeRead = maxNUnitsToRead;
  } else if (nFramesToReadWanted < minNFramesToRead)
    nUnitsToBeRead = GOSoundResample::computeMinNUnitsToReach(
      readPosFraction, minNFramesToRead);

  if (nUnitsToBeRead) {
    /* A violated bound is never 0 units: maxNFramesToRead >= nFrames > 0, and
     * minNFramesToRead > 0 here. Rescale a copy of the chunk so that its sum
     * lands on the violated bound. */
    std::copy(pResult, pResult + nFrames, pTmpIncrements);
    GOSoundVibratoPitchRateCurve::normalisePitchRateCurve(
      pTmpIncrements, nFrames, chunk.nTotalUnits, nUnitsToBeRead);
    pResult = pTmpIncrements;
  }
  return pResult;
}

void GOSoundVibratoProcessor::ReadFromDelayBuffer(
  GOSoundVibratoProcessorState &state,
  const unsigned *pPositionIncrements,
  GOSoundBufferPlanarMutable &outBuffer) const {
  const unsigned nFrames = outBuffer.GetNFrames();
  const unsigned nChannels = outBuffer.GetNChannels();

  GOSoundResample::RingPlanarFrameVector<float, float> ring(
    state.m_DelayBufferRingWithTail.GetData(),
    nChannels,
    m_NDelayBufferRingFrames,
    GOSoundVibratoProcessorState::N_DELAY_BUFFER_TAIL_FRAMES);
  auto readWith = [&]<class ResamplerT>(ResamplerT resampler) {
    resampler.template ResampleBlockVariableRatePlanar<
      GOSoundResample::RingPlanarFrameVector<float, float>>(
      state.m_DelayBufferReadResamplingPos,
      ring,
      pPositionIncrements,
      nFrames,
      nChannels,
      outBuffer.GetData(),
      nFrames);
  };

  if (m_InterpolationType == GOSoundResample::GO_POLYPHASE_INTERPOLATION)
    readWith(GOSoundResample::PolyphaseResampler(r_resample));
  else
    readWith(GOSoundResample::LinearResampler(r_resample));
}

void GOSoundVibratoProcessor::Process(
  GOSoundVibratoProcessorState &state,
  GOSoundBufferPlanarMutable &buffer) const {
  const GOSoundVibratoPitchRateCurve::ChunkDescription chunk
    = state.m_CurvePosition.SelectChunk(p_PitchRateCurve);

  if (chunk.HasRates()) {
    // A pitch tremulant is actually running this round
    const unsigned nFrames = m_NFramesPerProcess;

    assert(buffer.GetNFrames() == nFrames);
    assert(buffer.GetNChannels() == m_NChannels);
    state.MarkDirty();

    // Asserts there that the free room is enough
    state.WriteToDelayBuffer(buffer);

    unsigned tmpPositionIncrements[nFrames]; // stack
    const unsigned *pPositionIncrements = GetResamplerPositionIncrementsForUse(
      chunk,
      state.m_DelayBufferReadResamplingPos.GetFraction(),
      state.GetNFramesAvailableToRead(),
      state.GetNFramesAvailableToWrite(),
      tmpPositionIncrements);

    ReadFromDelayBuffer(state, pPositionIncrements, buffer);

    // After the read: pPositionIncrements may point into the curve itself
    state.m_CurvePosition.Advance(nFrames);
    assert(state.GetNFramesAvailableToWrite() >= nFrames);
  } else
    /* No pitch tremulant wants this chain this round - either the organ has
     * none, or its tremulant is a purely amplitude one, or it is switched
     * off. */
    state.ResetDelayBufferPositions();
}
