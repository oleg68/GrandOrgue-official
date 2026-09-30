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
    m_NDelayRingBufferFrames = m_NMaxLagFrames + nFrames;
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
    m_NDelayRingBufferFrames,
    GOSoundResample::MAX_POINTS,
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
    assert(state.m_WriteHeadPos < m_NDelayRingBufferFrames);
    assert(state.GetNReadBehindWriteFrames() <= nMaxLagFrames);

    // Mark the ring dirty so a later bypass round's Reset() actually
    // re-primes it instead of skipping as a no-op (see
    // GOSoundVibratoProcessorState's m_IsDirty doc comment).
    state.MarkDirty();

    // --- 1. Write: copy this round's input into the ring, refreshing the
    // mirrored tail whenever this round's write touched the mirrored
    // region.
    const unsigned writeHeadPosBeforeThisRound = state.m_WriteHeadPos;
    const unsigned nBeforeSeam = std::min(
      nFrames, m_NDelayRingBufferFrames - writeHeadPosBeforeThisRound);
    const unsigned nAfterSeam = nFrames - nBeforeSeam;

    for (unsigned channelI = 0; channelI < nChannels; channelI++) {
      GOSoundBuffer srcChannel = buffer.GetChannelBuffer(channelI);
      GOSoundBufferMutableMono ringChannel
        = state.m_DelayRingWithTailBuffer.GetChannelBuffer(channelI);

      ringChannel.GetSubBuffer(writeHeadPosBeforeThisRound, nBeforeSeam)
        .CopyFrom(srcChannel.GetSubBuffer(0, nBeforeSeam));
      if (nAfterSeam > 0)
        ringChannel.GetSubBuffer(0, nAfterSeam)
          .CopyFrom(srcChannel.GetSubBuffer(nBeforeSeam, nAfterSeam));

      if (
        writeHeadPosBeforeThisRound < GOSoundResample::MAX_POINTS
        || nAfterSeam > 0)
        ringChannel
          .GetSubBuffer(m_NDelayRingBufferFrames, GOSoundResample::MAX_POINTS)
          .CopyFrom(ringChannel.GetSubBuffer(0, GOSoundResample::MAX_POINTS));
    }

    state.m_WriteHeadPos += nFrames;
    if (state.m_WriteHeadPos >= m_NDelayRingBufferFrames)
      state.m_WriteHeadPos -= m_NDelayRingBufferFrames;

    // --- 2. Rate per frame: one round-constant lag-feedback term, then a
    // per-frame loop that both advances state.m_ReadHeadPosDesired and
    // fills fractionIncrements - the local, stack-allocated scratch array
    // the resampler (step 3) consumes; nothing here ever calls Inc() on
    // state.m_ReadHeadResamplingPos itself, only the resampler does that.
    double lagForFeedback
      = writeHeadPosBeforeThisRound - state.m_ReadHeadPosDesired;
    if (lagForFeedback < 0)
      lagForFeedback += m_NDelayRingBufferFrames;

    const float feedbackTerm
      = m_LagFeedbackCoeff * (float)(lagForFeedback - m_NominalLagFrames);

    unsigned fractionIncrements[nFrames];
    int64_t oldUnits = (int64_t)state.m_ReadHeadResamplingPos.GetIndex()
        * GOSoundResample::UPSAMPLE_FACTOR
      + state.m_ReadHeadResamplingPos.GetFraction();
    const float *pRate = p_PitchRateCurve;
    unsigned *pFractionIncrement = fractionIncrements;

    for (unsigned frameI = 0; frameI < nFrames; frameI++) {
      // prevLag satisfies the invariant MAX_POINTS <= prevLag <=
      // nMaxLagFrames, established by Reset() (which primes it to D0,
      // inside range) and maintained by this same clamp every frame
      // thereafter. Clamping rate itself (not the resulting position
      // directly) guarantees the clamped rate is never negative: the
      // upper bound (rate <= prevLag - MAX_POINTS) always allows rate=0
      // since prevLag >= MAX_POINTS, and the lower bound (rate >= prevLag
      // - nMaxLagFrames) always allows rate=0 since prevLag <=
      // nMaxLagFrames - so 0 is always inside the clamped range, and a
      // sane (non-negative) input rate never needs pushing below it. This
      // is what keeps fractionIncrements[] entries always representable
      // as the unsigned, forward-only advance ResamplingPosition::Inc()
      // expects, even for a pathological curve that would otherwise
      // demand a backward jump.
      double prevLag = writeHeadPosBeforeThisRound - state.m_ReadHeadPosDesired;

      if (prevLag < 0)
        prevLag += m_NDelayRingBufferFrames;

      float rate = *(pRate++) + feedbackTerm;
      const float maxRate = (float)(prevLag - GOSoundResample::MAX_POINTS);
      const float minRate = (float)(prevLag - nMaxLagFrames);

      if (rate > maxRate)
        rate = maxRate;
      if (rate < minRate)
        rate = minRate;
      if (rate < 0)
        rate = 0;

      state.m_ReadHeadPosDesired += rate;
      if (state.m_ReadHeadPosDesired >= m_NDelayRingBufferFrames)
        state.m_ReadHeadPosDesired -= m_NDelayRingBufferFrames;

      // Drift-free fixed-point conversion: each frame's increment is the
      // difference of two roundings of the same monotonically-advancing
      // exact double, so per-frame rounding error never compounds - see
      // the class doc comment's *Drift caveat* discussion. The ring-wrap
      // correction (+ring*UPSAMPLE_FACTOR when the raw diff goes
      // negative) mirrors ResamplingPosition::Inc()/NormalizePosition()'s
      // own single-wrap assumption, valid because one frame's growth is
      // always far smaller than the ring.
      const int64_t newUnits = std::llround(
        state.m_ReadHeadPosDesired * GOSoundResample::UPSAMPLE_FACTOR);
      int64_t diff = newUnits - oldUnits;

      if (diff < 0)
        diff += (int64_t)m_NDelayRingBufferFrames
          * GOSoundResample::UPSAMPLE_FACTOR;

      *(pFractionIncrement++) = (unsigned)diff;
      oldUnits = newUnits;
    }

    // --- 3. Read: one shared RingPlanarFrameVector and ResamplingPosition
    // for the whole buffer, all channels together - this is what actually
    // advances state.m_ReadHeadResamplingPos, via its own internal Inc()
    // calls fed by fractionIncrements.
    GOSoundResample::RingPlanarFrameVector<float, float> ring(
      state.m_DelayRingWithTailBuffer.GetData(),
      nChannels,
      m_NDelayRingBufferFrames,
      GOSoundResample::MAX_POINTS);
    auto readWith = [&]<class ResamplerT>(ResamplerT resampler) {
      resampler.template ResampleBlockVariableRatePlanar<
        GOSoundResample::RingPlanarFrameVector<float, float>>(
        state.m_ReadHeadResamplingPos,
        ring,
        fractionIncrements,
        buffer.GetNFrames(),
        nChannels,
        buffer.GetData(),
        buffer.GetNFrames());
    };

    if (m_InterpolationType == GOSoundResample::GO_POLYPHASE_INTERPOLATION)
      readWith(GOSoundResample::PolyphaseResampler(r_resample));
    else
      readWith(GOSoundResample::LinearResampler(r_resample));
  } else
    // No pitch tremulant wants this chain this round - either the organ
    // has none, or its tremulant is a purely amplitude one, or it is
    // switched off.
    state.Reset();
}
