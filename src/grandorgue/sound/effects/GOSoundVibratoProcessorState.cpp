/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundVibratoProcessorState.h"

#include <algorithm>
#include <cassert>

#include "sound/buffer/GOSoundBuffer.h"

GOSoundVibratoProcessorState::GOSoundVibratoProcessorState(
  unsigned nChannels, unsigned nRingFrames)
  : m_NDelayBufferRingFrames(nRingFrames),
    m_DelayBufferRingWithTail(
      nChannels, nRingFrames + N_DELAY_BUFFER_TAIL_FRAMES) {
  assert(nRingFrames > N_LOOKAHEAD_FRAMES);

  ResetDelayBufferPositions();
}

void GOSoundVibratoProcessorState::WriteToDelayBuffer(
  const GOSoundBufferPlanar &srcBuffer) {
  const unsigned nFrames = srcBuffer.GetNFrames();
  const unsigned nChannels = srcBuffer.GetNChannels();

  assert(GetNFramesAvailableToWrite() >= nFrames);

  unsigned nFramesLeft = nFrames;

  while (nFramesLeft > 0) {
    const unsigned writePos = m_DelayBufferWritePos;

    // Else m_NDelayBufferRingFrames - writePos below would underflow
    assert(writePos < m_NDelayBufferRingFrames);

    const unsigned nChunkFrames
      = std::min(nFramesLeft, m_NDelayBufferRingFrames - writePos);
    // Where this chunk starts in the source
    const unsigned srcPos = nFrames - nFramesLeft;
    // The part of this chunk that lies in [0, N_DELAY_BUFFER_TAIL_FRAMES)
    const unsigned nFramesToMirror = writePos < N_DELAY_BUFFER_TAIL_FRAMES
      ? std::min(nChunkFrames, N_DELAY_BUFFER_TAIL_FRAMES - writePos)
      : 0;

    for (unsigned channelI = 0; channelI < nChannels; channelI++) {
      GOSoundBuffer srcChannel = srcBuffer.GetChannelBuffer(channelI);

      GetDelayMonoSubBuffer(channelI, writePos, nChunkFrames)
        .CopyFrom(srcChannel.GetSubBuffer(srcPos, nChunkFrames));
      if (nFramesToMirror > 0)
        GetDelayMonoSubBuffer(
          channelI, m_NDelayBufferRingFrames + writePos, nFramesToMirror)
          .CopyFrom(GetDelayMonoSubBuffer(channelI, writePos, nFramesToMirror));
    }
    // The chunk does not cross the end of the ring, so at most it reaches it
    m_DelayBufferWritePos += nChunkFrames;
    if (m_DelayBufferWritePos >= m_NDelayBufferRingFrames)
      m_DelayBufferWritePos = 0;
    nFramesLeft -= nChunkFrames;
  }
}

void GOSoundVibratoProcessorState::Reset() {
  /* A bound cursor means that an active round has happened, so the state is
   * dirty and the reset below really re-primes the ring: the cursor and the
   * lag restart together */
  assert(!m_CurvePosition.IsBound() || m_IsDirty);
  m_CurvePosition.SelectChunk(nullptr);
  ResetDelayBufferPositions();
}
