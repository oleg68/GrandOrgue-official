/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDVIBRATOPROCESSORSTATE_H
#define GOSOUNDVIBRATOPROCESSORSTATE_H

#include "sound/buffer/GOSoundBufferPlanarManaged.h"
#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/processing/GOSoundProcessorState.h"

class GOSoundVibratoProcessor;
class GOTestSoundVibratoProcessor;

/**
 * Per-chain-state DSP memory of GOSoundVibratoProcessor. One instance per
 * chain, never shared - GOSoundVibratoProcessor::Process() is const, so
 * every byte of mutable state a chain needs lives here, following the same
 * split as GOSoundShelfFilterProcessorState.
 */
class GOSoundVibratoProcessorState : public GOSoundProcessorState {
private:
  friend class GOSoundVibratoProcessor;     // Process() needs direct access
                                            // to the ring and position fields
  friend class GOTestSoundVibratoProcessor; // direct access for unit tests

  /**
   * The ring's size in frames of real, distinct history. Stored here, not
   * just on GOSoundVibratoProcessor, because Reset() takes no arguments and
   * still needs it to re-prime m_ReadHeadResamplingPos/m_ReadHeadPosDesired;
   * set once at construction and never changes afterwards.
   */
  unsigned m_NDelayRingBufferFrames;

  /**
   * The delay line. Each channel's sub-buffer is a ring of
   * m_NDelayRingBufferFrames items followed by a mirrored tail - it lets a
   * resampler read across the ring's seam without special-casing the
   * wraparound (see GOSoundResample::RingPlanarFrameVector). Written every
   * active round (refreshing the mirrored tail when the write touches it -
   * see GOSoundVibratoProcessor::Process()); read back, ring+tail together,
   * through GOSoundResample::RingPlanarFrameVector.
   */
  GOSoundBufferPlanarManaged m_DelayRingWithTailBuffer;

  /**
   * D0, in frames: the read-head-behind-write-head distance Reset()
   * re-primes m_ReadHeadResamplingPos/m_ReadHeadPosDesired to. Stored here
   * for the same reason as m_NDelayRingBufferFrames - Reset() takes no
   * arguments, so this state must carry its own copy of
   * GOSoundVibratoProcessor::m_NominalLagFrames rather than being handed it
   * each time. float, not double: it is set once at construction and only
   * ever read afterward, never accumulated, so it has none of
   * m_ReadHeadPosDesired's precision requirements.
   */
  float m_NominalLagFrames;

  /**
   * Index of the next frame to be written into m_DelayRingWithTailBuffer,
   * in [0, m_NDelayRingBufferFrames). Advances by exactly nFrames every
   * active round (wrapping at m_NDelayRingBufferFrames); unlike the read
   * head it never skips or repeats a slot, since writing is not modulated.
   */
  unsigned m_WriteHeadPos = 0;

  /**
   * The read head's exact, unquantized position in
   * m_DelayRingWithTailBuffer, in fractional frames, in [0,
   * m_NDelayRingBufferFrames). Advanced every output frame by the
   * modulated rate (see GOSoundVibratoProcessor::Process()). Kept as a
   * double specifically so quantization to GOSoundResample's fixed-point
   * ResamplingPosition happens fresh from this exact value every frame,
   * rather than being carried cumulatively inside a fixed-point register -
   * see m_ReadHeadResamplingPos, which holds only the quantized result, not
   * a substitute for this field.
   */
  double m_ReadHeadPosDesired = 0;

  /**
   * The read head's quantized position: GetIndex() (whole frames, in [0,
   * m_NDelayRingBufferFrames)) and GetFraction() (sub-frame remainder, in
   * UPSAMPLE_FACTOR units). The exact object Process() hands to
   * ResampleBlockVariableRatePlanar(), which is the only thing that ever
   * calls Inc() on it - see Process() steps 2-3 for why that avoids
   * compounding rounding bias frame over frame.
   */
  GOSoundResample::ResamplingPosition m_ReadHeadResamplingPos;

  /**
   * Whether an active round has written into the ring since the last
   * Reset(). Starts true so the constructor's own Reset() call still does
   * its real priming work; GOSoundVibratoProcessor::Process()'s active
   * branch sets it back to true as soon as it starts writing. Reset()
   * skips its own work entirely when this is already false - otherwise a
   * bypassed chain (Process()'s bypass branch calls Reset() every round)
   * would pay a full memset of the ring+tail buffer every single round it
   * stays bypassed, contradicting the "bypass costs nothing" contract
   * from GOSoundVibratoProcessor's own class doc comment.
   */
  bool m_IsDirty = true;

public:
  /**
   * Allocates m_DelayRingWithTailBuffer and stores nRingFrames/
   * nominalLagFrames (see m_NDelayRingBufferFrames/m_NominalLagFrames for
   * why) - equivalent to calling Reset() immediately after construction.
   * @param nChannels number of channels this state will be Process()'d with
   * @param nRingFrames see m_NDelayRingBufferFrames
   * @param nTailFrames length, in frames, of the mirrored tail appended
   *   after the ring in m_DelayRingWithTailBuffer - see
   *   m_DelayRingWithTailBuffer. Not stored: only needed once, here, to
   *   size m_DelayRingWithTailBuffer.
   * @param nominalLagFrames D0 - see m_NominalLagFrames
   */
  GOSoundVibratoProcessorState(
    unsigned nChannels,
    unsigned nRingFrames,
    unsigned nTailFrames,
    float nominalLagFrames);

  /**
   * Zero-fills m_DelayRingWithTailBuffer, resets m_WriteHeadPos to 0, and
   * re-primes m_ReadHeadPosDesired/m_ReadHeadResamplingPos to exactly
   * m_NominalLagFrames behind the write head - as if no audio had been
   * processed yet. Takes no arguments, per GOSoundProcessorState::Reset()'s
   * contract, which is why m_NDelayRingBufferFrames and m_NominalLagFrames
   * must be stored fields rather than call parameters. Called by Process()
   * on every bypass round, which is what guarantees a bypass-to-active
   * transition always starts at lag D0. A no-op, skipping all of the
   * above, when m_IsDirty is already false - see that field's own doc
   * comment for why repeated calls on an already-silent state must be
   * cheap.
   */
  void Reset() override;

  /**
   * Marks the ring as touched since the last Reset() - see m_IsDirty.
   * Called by GOSoundVibratoProcessor::Process()'s active branch, the
   * counterpart to Reset() clearing the flag, so the set/clear pair stays
   * symmetric instead of Process() reaching past this class's own API to
   * poke the field directly.
   */
  inline void MarkDirty() { m_IsDirty = true; }

  /**
   * How many whole frames the read head currently trails the write head,
   * wrapped into [0, m_NDelayRingBufferFrames). A plain unsigned
   * subtraction of m_WriteHeadPos and m_ReadHeadResamplingPos.GetIndex()
   * (both already integer, no flooring needed here), deliberately not
   * multiplied by any feedback coefficient - it is a coarse invariant
   * check (see Process()'s "Write" step), not part of the continuous
   * rate/feedback formula, which needs the fractional distance computed
   * fresh from m_ReadHeadPosDesired instead (see Process()'s "Rate per
   * frame" step).
   * @return m_ReadHeadResamplingPos's distance behind m_WriteHeadPos, in
   *   whole frames
   */
  unsigned GetNReadBehindWriteFrames() const;
};

#endif /* GOSOUNDVIBRATOPROCESSORSTATE_H */
