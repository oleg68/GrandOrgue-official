/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundVibratoProcessorState.h"

#include <cassert>
#include <cmath>

GOSoundVibratoProcessorState::GOSoundVibratoProcessorState(
  unsigned nChannels,
  unsigned nRingFrames,
  unsigned nTailFrames,
  float nominalLagFrames)
  : m_NDelayRingBufferFrames(nRingFrames),
    m_DelayRingWithTailBuffer(nChannels, nRingFrames + nTailFrames),
    m_NominalLagFrames(nominalLagFrames) {
  Reset();
}

void GOSoundVibratoProcessorState::Reset() {
  if (m_IsDirty) {
    m_DelayRingWithTailBuffer.FillWithSilence();
    m_WriteHeadPos = 0;
    m_ReadHeadPosDesired = m_NDelayRingBufferFrames - m_NominalLagFrames;

    const unsigned wholeFrames = (unsigned)m_ReadHeadPosDesired;
    const double fraction = m_ReadHeadPosDesired - wholeFrames;

    /* factor=1.0f is a placeholder: m_ReadHeadResamplingPos's
     * m_FractionIncrement is never read, since Process() always advances
     * it via explicit Inc(unsigned) calls, never the implicit Inc() that
     * would use it - see this field's own doc comment. */
    m_ReadHeadResamplingPos.Init(1.0f, wholeFrames);
    m_ReadHeadResamplingPos.Inc(
      (unsigned)std::llround(fraction * GOSoundResample::UPSAMPLE_FACTOR));
    m_IsDirty = false;
  }
}

unsigned GOSoundVibratoProcessorState::GetNReadBehindWriteFrames() const {
  const unsigned readHeadPos = m_ReadHeadResamplingPos.GetIndex();

  assert(m_WriteHeadPos < m_NDelayRingBufferFrames);
  assert(readHeadPos < m_NDelayRingBufferFrames);

  int diff = (int)m_WriteHeadPos - (int)readHeadPos;

  if (diff < 0)
    diff += (int)m_NDelayRingBufferFrames;

  assert(diff >= 0 && (unsigned)diff < m_NDelayRingBufferFrames);

  return (unsigned)diff;
}
