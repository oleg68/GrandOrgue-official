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

#include "sound/buffer/GOSoundBuffer.h"
#include "sound/buffer/GOSoundBufferMutableMono.h"
#include "sound/buffer/GOSoundBufferPlanarMutable.h"

GOSoundVibratoProcessor::GOSoundVibratoProcessor(
  const GOSoundResample &resample,
  GOSoundResample::InterpolationType interpolationType)
  : r_resample(resample), m_InterpolationType(interpolationType) {}

void GOSoundVibratoProcessor::EnsureSetup(
  unsigned nChannels, unsigned nFrames, unsigned sampleRate) {
  const bool isSampleRateChanged
    = !m_IsSetupCalled || sampleRate != m_SampleRate;
  const bool isRingSizeChanged
    = isSampleRateChanged || nFrames != m_NFramesPerProcess;

  if (isSampleRateChanged) {
    m_SampleRate = sampleRate;
    m_NominalLagFrames = VIBRATO_NOMINAL_LAG_MS / 1000.0f * sampleRate;
    m_LagFeedbackCoeff
      = 1.0f / (VIBRATO_LAG_FEEDBACK_TIME_CONSTANT_S * sampleRate);
  }
  if (isRingSizeChanged) {
    m_NMaxLagFrames = (unsigned)std::ceil(
      m_NominalLagFrames * VIBRATO_MAX_LAG_MS / VIBRATO_NOMINAL_LAG_MS);
    m_NMaxLagFramesF = (float)m_NMaxLagFrames;
    m_NDelayBufferRingFrames = m_NMaxLagFrames + nFrames;
    m_NUnitsRing = m_NDelayBufferRingFrames * GOSoundResample::UPSAMPLE_FACTOR;
  }
  m_NChannels = nChannels;
  m_NFramesPerProcess = nFrames;
  p_PitchRateCurve = nullptr;
  m_IsSetupCalled = true;
}

void GOSoundVibratoProcessor::SetPitchRateCurve(
  const float *pRates, unsigned nFrames) {
  assert(!pRates || nFrames == m_NFramesPerProcess);

  p_PitchRateCurve = pRates;
}

std::unique_ptr<GOSoundVibratoProcessorState> GOSoundVibratoProcessor::
  CreateTypedState() const {
  assert(
    m_IsSetupCalled && "EnsureSetup() must be called before CreateState()");

  return std::make_unique<GOSoundVibratoProcessorState>(
    m_NChannels,
    m_NDelayBufferRingFrames,
    N_DELAY_BUFFER_TAIL_FRAMES,
    m_NominalLagFrames);
}

void GOSoundVibratoProcessor::Process(
  GOSoundVibratoProcessorState &state,
  GOSoundBufferPlanarMutable &buffer) const {
  if (p_PitchRateCurve) {
    // A pitch tremulant is actually running this round.
    const unsigned nFrames = m_NFramesPerProcess;
    const unsigned nChannels = m_NChannels;
    const unsigned nMaxLagFrames = m_NMaxLagFrames;

    assert(buffer.GetNFrames() == nFrames);
    assert(buffer.GetNChannels() == nChannels);
    assert(state.m_WriteHeadPos < m_NDelayBufferRingFrames);
    assert(state.GetNReadBehindWriteFrames() <= nMaxLagFrames);

    /* Mark the ring dirty so a later bypass round's Reset() actually
     * re-primes it instead of skipping as a no-op (see
     * GOSoundVibratoProcessorState's m_IsDirty doc comment). */
    state.MarkDirty();

    /*
     * 1. Write from buffer into m_DelayRingWithTailBuffer as is
     */

    const unsigned writeHeadPosBeforeThisRound = state.m_WriteHeadPos;
    /* This round's write may span two destination ranges in the ring:
     * [writeHeadPosBeforeThisRound, writeHeadPosBeforeThisRound +
     * nFramesToCopyToUpperPart) in the ring's upper part, and - only if the
     * write crosses the seam - [0, nFramesToCopyToLowerPart) in the ring's
     * lower part. The upper-part size is capped by the room actually left
     * ahead of the write head; the lower-part size is whatever's left
     * over, zero unless this round's write actually crosses the seam - at
     * most one wrap, since nFrames < m_NDelayBufferRingFrames always. */
    const unsigned nFramesToCopyToUpperPart = std::min(
      nFrames, m_NDelayBufferRingFrames - writeHeadPosBeforeThisRound);
    const unsigned nFramesToCopyToLowerPart
      = nFrames - nFramesToCopyToUpperPart;

    for (unsigned channelI = 0; channelI < nChannels; channelI++) {
      GOSoundBuffer srcChannel = buffer.GetChannelBuffer(channelI);
      GOSoundBufferMutableMono ringChannel
        = state.m_DelayRingWithTailBuffer.GetChannelBuffer(channelI);

      ringChannel
        .GetSubBuffer(writeHeadPosBeforeThisRound, nFramesToCopyToUpperPart)
        .CopyFrom(srcChannel.GetSubBuffer(0, nFramesToCopyToUpperPart));
      if (nFramesToCopyToLowerPart > 0)
        ringChannel.GetSubBuffer(0, nFramesToCopyToLowerPart)
          .CopyFrom(srcChannel.GetSubBuffer(
            nFramesToCopyToUpperPart, nFramesToCopyToLowerPart));

      /* Refresh the mirror only if this round's write actually touched
       * ring indices [0, N_DELAY_BUFFER_TAIL_FRAMES) - directly, or via a
       * wrap (see nFramesToCopyToUpperPart/nFramesToCopyToLowerPart
       * above). */
      if (
        writeHeadPosBeforeThisRound < N_DELAY_BUFFER_TAIL_FRAMES
        || nFramesToCopyToLowerPart > 0)
        ringChannel
          .GetSubBuffer(m_NDelayBufferRingFrames, N_DELAY_BUFFER_TAIL_FRAMES)
          .CopyFrom(ringChannel.GetSubBuffer(0, N_DELAY_BUFFER_TAIL_FRAMES));
    }

    /* Advance the write position by nFrames, wrapping at most once (see
     * nFramesToCopyToUpperPart/nFramesToCopyToLowerPart above for why a
     * single wrap always suffices here). */
    state.m_WriteHeadPos = RingAdd(state.m_WriteHeadPos, nFrames);

    /*
     * 2. Fill fractionIncrements from p_PitchRateCurve, advancing
     * state.m_ReadHeadPosDesired
     */

    const unsigned readHeadIndexBeforeThisRound
      = state.m_ReadHeadResamplingPos.GetIndex();
    const unsigned nFramesLagBeforeThisRound
      = RingSub(writeHeadPosBeforeThisRound, readHeadIndexBeforeThisRound);
    const float feedbackTerm = m_LagFeedbackCoeff
      * (float)(nFramesLagBeforeThisRound - m_NominalLagFrames);
    unsigned oldUnits
      = readHeadIndexBeforeThisRound * GOSoundResample::UPSAMPLE_FACTOR
      + state.m_ReadHeadResamplingPos.GetFraction();
    const float *pRate = p_PitchRateCurve;
    unsigned fractionIncrements[nFrames];
    unsigned *pFractionIncrement = fractionIncrements;

    for (unsigned nFramesLeft = nFrames; nFramesLeft > 0; nFramesLeft--) {
      /* prevLagDesiredF stays within [N_DELAY_BUFFER_TAIL_FRAMES,
       * nMaxLagFrames] (an invariant Reset() establishes and this clamp
       * maintains every frame). Clamping rate itself, not the resulting
       * position, guarantees the clamped rate is never negative - both
       * bounds always permit rate=0 since prevLagDesiredF is always
       * within range - so fractionIncrements[] entries stay
       * representable as the unsigned, forward-only advance
       * ResamplingPosition::Inc() expects, even for a pathological
       * curve. */
      const float prevLagDesiredF = (float)RingSub(
        (double)writeHeadPosBeforeThisRound, state.m_ReadHeadPosDesired);
      const float maxRate = prevLagDesiredF - N_DELAY_BUFFER_TAIL_FRAMES_F;
      const float minRate = std::max(0.0f, prevLagDesiredF - m_NMaxLagFramesF);
      const float rate
        = std::clamp(*(pRate++) + feedbackTerm, minRate, maxRate);

      state.m_ReadHeadPosDesired
        = RingAdd(state.m_ReadHeadPosDesired, (double)rate);

      /* Drift-free fixed-point conversion: each frame's increment is the
       * difference of two flooring conversions of the same
       * monotonically-advancing exact double, so per-frame rounding
       * error never compounds - see the class doc comment's *Drift
       * caveat* discussion. RingSub() wraps the diff into the units ring
       * (m_NUnitsRing) instead of the frame ring, valid because one
       * frame's growth is always far smaller than the ring. */
      /* state.m_ReadHeadPosDesired < m_NDelayBufferRingFrames guarantees
       * newUnits < m_NUnitsRing after flooring, with no extra check. */
      const unsigned newUnits
        = (unsigned)(state.m_ReadHeadPosDesired * GOSoundResample::UPSAMPLE_FACTOR);

      *(pFractionIncrement++) = RingSub(newUnits, oldUnits, m_NUnitsRing);
      oldUnits = newUnits;
    }

    /*
     * 3. Resample from m_DelayRingWithTailBuffer into the output buffer with
     * varying increments from fractionIncrements
     */

    GOSoundResample::RingPlanarFrameVector<float, float> ring(
      state.m_DelayRingWithTailBuffer.GetData(),
      nChannels,
      m_NDelayBufferRingFrames,
      N_DELAY_BUFFER_TAIL_FRAMES);
    /* Capture a pointer to fractionIncrements, not the VLA itself: GCC's
     * generic (templated) lambdas mis-synthesize the closure's destructor
     * when a GNU VLA is captured by reference directly, deleting it -
     * reproduced in isolation outside this file. A plain pointer capture
     * sidesteps that bug entirely. */
    const unsigned *pFractionIncrementsBase = fractionIncrements;
    auto readWith = [&]<class ResamplerT>(ResamplerT resampler) {
      resampler.template ResampleBlockVariableRatePlanar<
        GOSoundResample::RingPlanarFrameVector<float, float>>(
        state.m_ReadHeadResamplingPos,
        ring,
        pFractionIncrementsBase,
        nFrames,
        nChannels,
        buffer.GetData(),
        nFrames);
    };

    if (m_InterpolationType == GOSoundResample::GO_POLYPHASE_INTERPOLATION)
      readWith(GOSoundResample::PolyphaseResampler(r_resample));
    else
      readWith(GOSoundResample::LinearResampler(r_resample));
  } else
    /* No pitch tremulant wants this chain this round - either the organ
     * has none, or its tremulant is a purely amplitude one, or it is
     * switched off. */
    state.Reset();
}
