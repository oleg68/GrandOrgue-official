/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDVIBRATOPROCESSORSTATE_H
#define GOSOUNDVIBRATOPROCESSORSTATE_H

#include <cassert>

#include "sound/buffer/GOSoundBufferMutableMono.h"
#include "sound/buffer/GOSoundBufferPlanar.h"
#include "sound/buffer/GOSoundBufferPlanarManaged.h"
#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/processing/GOSoundProcessorState.h"

#include "GOSoundVibratoPitchRateCurve.h"

class GOSoundVibratoProcessor;
class GOTestSoundVibratoProcessor;

/**
 * Per-chain-state DSP memory of GOSoundVibratoProcessor. One instance per
 * chain, never shared - GOSoundVibratoProcessor::Process() is const, so
 * every byte of mutable state a chain needs lives here, following the same
 * split as GOSoundShelfFilterProcessorState.
 */
class GOSoundVibratoProcessorState : public GOSoundProcessorState {
public:
  /**
   * How many frames the interpolator reads ahead of the read position:
   * GOSoundResample::MAX_POINTS - 1. The read position must always stay this
   * far behind the write position (see GetNFramesAvailableToRead()).
   */
  static constexpr unsigned N_LOOKAHEAD_FRAMES
    = GOSoundResample::MAX_POINTS - 1;
  /**
   * The length of the mirrored tail after the ring: it lets the resampler read
   * across the ring's seam without special-casing the wraparound.
   */
  static constexpr unsigned N_DELAY_BUFFER_TAIL_FRAMES = N_LOOKAHEAD_FRAMES;

private:
  friend class GOSoundVibratoProcessor;     // Process() reads the ring and the
                                            // positions
  friend class GOTestSoundVibratoProcessor; // direct access for the unit tests

  /**
   * The ring's size in frames, not counting the tail. Stored here because
   * Reset() takes no arguments; set once at construction.
   */
  unsigned m_NDelayBufferRingFrames;
  /**
   * The delay buffer: for each channel the ring of m_NDelayBufferRingFrames
   * frames followed by the mirrored tail of N_DELAY_BUFFER_TAIL_FRAMES frames.
   * See WriteToDelayBuffer() for the invariant of the tail.
   */
  GOSoundBufferPlanarManaged m_DelayBufferRingWithTail;
  /**
   * The index of the next frame to write into the ring, in
   * [0, m_NDelayBufferRingFrames).
   */
  unsigned m_DelayBufferWritePos = 0;
  /**
   * The cursor in the pitch rate curve; unbound while the vibrato is
   * inactive. It is owned here, not by the const processor, so that every
   * chain walks the curve on its own.
   */
  GOSoundVibratoPitchRateCurve::Position m_CurvePosition;
  /**
   * The read position in the ring: the whole-frame index and the fraction.
   * It is moved only by the resampler, never by this class.
   */
  GOSoundResample::ResamplingPosition m_DelayBufferReadResamplingPos;
  /**
   * Whether an active round has happened since the last reset. A reset of a
   * clean state does nothing, so a bypassed chain pays nothing per round.
   * Starts true so that the constructor's reset does its work.
   */
  bool m_IsDirty = true;

  /**
   * @param channelI the channel index
   * @param startPos the first frame of the sub-buffer in the ring with its tail
   * @param nSubBufferFrames the sub-buffer's length in frames
   * @return the sub-buffer of the given channel of the delay buffer (the ring
   *   followed by the tail)
   */
  inline GOSoundBufferMutableMono GetDelayMonoSubBuffer(
    unsigned channelI, unsigned startPos, unsigned nSubBufferFrames) {
    return m_DelayBufferRingWithTail.GetChannelBuffer(channelI).GetSubBuffer(
      startPos, nSubBufferFrames);
  }

  /**
   * The partial reset: if the state is dirty, zero-fills the first
   * N_LOOKAHEAD_FRAMES frames of the ring of every channel, puts the write
   * position to N_LOOKAHEAD_FRAMES and the read position to the start, and
   * clears the dirty flag. Does not touch the rest of the ring, the tail and
   * the curve position. Used by the constructor, by Reset() and by the bypass
   * branch of Process().
   */
  inline void ResetDelayBufferPositions() {
    /* The initial reset: m_IsDirty starts true, so the constructor's call does
     * the real work. Later, a bypassed chain calls this every round, and while
     * it is clean (no active round since the last reset) nothing is done. */
    if (m_IsDirty) {
      const unsigned nChannels = m_DelayBufferRingWithTail.GetNChannels();

      for (unsigned channelI = 0; channelI < nChannels; ++channelI)
        GetDelayMonoSubBuffer(channelI, 0, N_LOOKAHEAD_FRAMES)
          .FillWithSilence();
      m_DelayBufferWritePos = N_LOOKAHEAD_FRAMES;
      /* factor=1.0f is a placeholder: the position's own increment is never
       * read, since the resampler always advances it with explicit increments
       */
      m_DelayBufferReadResamplingPos.Init(1.0f, 0);
      m_IsDirty = false;
    }
  }

public:
  /**
   * Allocates the ring with its tail and primes the ring as
   * ResetDelayBufferPositions() does.
   * @param nChannels the number of channels this state will be processed with
   * @param nRingFrames the ring's size in frames, not counting the tail
   */
  GOSoundVibratoProcessorState(unsigned nChannels, unsigned nRingFrames);

  /**
   * The free room for the next write: the forward distance from the write
   * position to the read index, in [0, ring - N_LOOKAHEAD_FRAMES]. 0 means a
   * full ring; an empty one is impossible, because the read position always
   * stays at least N_LOOKAHEAD_FRAMES behind the write position.
   */
  inline unsigned GetNFramesAvailableToWrite() const {
    const unsigned writePos = m_DelayBufferWritePos;
    const unsigned readIndex = m_DelayBufferReadResamplingPos.GetIndex();

    assert(writePos < m_NDelayBufferRingFrames);
    assert(readIndex < m_NDelayBufferRingFrames);

    const unsigned nFramesAvailableToWrite = readIndex >= writePos
      ? readIndex - writePos
      : readIndex + m_NDelayBufferRingFrames - writePos;

    assert(
      nFramesAvailableToWrite <= m_NDelayBufferRingFrames - N_LOOKAHEAD_FRAMES);
    return nFramesAvailableToWrite;
  }

  /**
   * By how many whole frames the read position may advance:
   * ring - N_LOOKAHEAD_FRAMES - GetNFramesAvailableToWrite().
   */
  inline unsigned GetNFramesAvailableToRead() const {
    return m_NDelayBufferRingFrames - N_LOOKAHEAD_FRAMES
      - GetNFramesAvailableToWrite();
  }

  /** Marks the state dirty: an active round writes into the ring. */
  inline void MarkDirty() { m_IsDirty = true; }

  /**
   * Appends all frames of srcBuffer to the delay buffer at the write position,
   * wrapping at the ring's end, and keeps the mirrored tail consistent: every
   * ring frame written at an index below N_DELAY_BUFFER_TAIL_FRAMES is also
   * copied to the tail right after the ring, so after the call
   * tail[j] == ring[j] for every index j < N_DELAY_BUFFER_TAIL_FRAMES written
   * since the last reset (the other tail entries are unspecified and never
   * read). Advances the write position by srcBuffer.GetNFrames().
   * The read position is not touched.
   * @param srcBuffer the frames to write; must have as many channels as the
   *   ring, and no more frames than GetNFramesAvailableToWrite()
   */
  void WriteToDelayBuffer(const GOSoundBufferPlanar &srcBuffer);

  /**
   * The full reset required by GOSoundProcessorState: unbinds the curve
   * position and does the partial reset. Process() never calls it; it is
   * called when the chain's content is discarded.
   */
  void Reset() override;
};

#endif /* GOSOUNDVIBRATOPROCESSORSTATE_H */
