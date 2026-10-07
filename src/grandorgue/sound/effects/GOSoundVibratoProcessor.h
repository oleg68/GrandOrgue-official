/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDVIBRATOPROCESSOR_H
#define GOSOUNDVIBRATOPROCESSOR_H

#include <cassert>
#include <memory>

#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/processing/GOSoundProcessorTyped.h"

#include "GOSoundVibratoPitchRateCurve.h"
#include "GOSoundVibratoProcessorState.h"

class GOTestPerfSoundVibratoProcessor;
class GOTestSoundVibratoProcessor;

/**
 * Effect processor for a windchest's processing chain: reads the audio back
 * from a delay buffer with the resampling rates of a precomputed
 * GOSoundVibratoPitchRateCurve, which makes a Doppler-shift pitch vibrato -
 * the pitch half of a synthesized tremulant (issue #709). Built on
 * GOSoundVibratoProcessorState's delay buffer and
 * GOSoundResample::RingPlanarFrameVector /
 * ResampleBlockVariableRatePlanar().
 *
 * Holds only parameters (the curve, the format, the interpolation type); every
 * mutable byte a chain needs lives in GOSoundVibratoProcessorState, since
 * Process() is const and one processor instance is shared across many chain
 * states. SetPitchRateCurve(nullptr) bypasses at zero cost - an organ with no
 * pitch tremulant pays nothing for this processor being in the chain.
 *
 * The curve is normalised, so the read position does not drift against the
 * write position. The delay is not fixed: it starts at its minimum
 * (N_LOOKAHEAD_FRAMES) and settles at what the curve needs; the portions of the
 * first cycle that would shrink the delay below the minimum are rescaled
 * (GetResamplerPositionIncrementsForUse()).
 */
class GOSoundVibratoProcessor
  : public GOSoundProcessorTyped<GOSoundVibratoProcessorState> {
private:
  friend class GOTestSoundVibratoProcessor;     // direct access for unit tests
  friend class GOTestPerfSoundVibratoProcessor; // the same for the perf tests

  /**
   * The maximum extra lag the read position may take on top of its minimum,
   * in milliseconds. Used only to size the delay ring in EnsureSetup().
   */
  static constexpr float VIBRATO_MAX_LAG_MS = 100.0f;

  /** The resampling kernel this processor reads from; not owned. */
  const GOSoundResample &r_resample;

  /**
   * Which resampler (linear or polyphase) Process() uses; set at construction.
   */
  GOSoundResample::InterpolationType m_InterpolationType;

  /** The channel count of the last EnsureSetup(). */
  unsigned m_NChannels = 0;

  /** The sample rate of the last EnsureSetup(). */
  unsigned m_SampleRate = 0;

  /** The round's frame count of the last EnsureSetup(); at least 2. */
  unsigned m_NFramesPerProcess = 0;

  /**
   * The delay ring's size in frames, not counting the tail:
   * nMaxLagFrames + m_NFramesPerProcess + N_LOOKAHEAD_FRAMES. The states are
   * created with it.
   */
  unsigned m_NDelayBufferRingFrames = 0;

  /** Whether EnsureSetup() has been called. */
  bool m_IsSetupCalled = false;

  /**
   * The pitch rate curve walked by Process(); nullptr means bypass. Not owned;
   * set by SetPitchRateCurve(), reset by EnsureSetup().
   */
  const GOSoundVibratoPitchRateCurve *p_PitchRateCurve = nullptr;

  /** The id of p_PitchRateCurve; NO_CURVE_ID for nullptr. */
  uint64_t m_PitchRateCurveId = GOSoundVibratoPitchRateCurve::NO_CURVE_ID;

  /**
   * The same as the public EnsureSetup(), but the maximum extra lag is given
   * in frames (0 is valid). The only place that sets the setup fields.
   * @param nMaxLagFrames the maximum extra lag in frames
   * (the other parameters are those of the public EnsureSetup())
   */
  void EnsureSetup(
    unsigned nChannels,
    unsigned nFrames,
    unsigned sampleRate,
    unsigned nMaxLagFrames);

  /**
   * Returns a pointer to the increments of the resampler's position (in the
   * units of GOSoundResample, one per output frame) to use for this round: the
   * increments of the curve chunk itself if resampling with them keeps the
   * read position within the allowed range, otherwise a rescaled copy of them
   * in pTmpIncrements whose sum lands exactly on the violated bound.
   *
   * The final read position must be at least
   * max(nFrames - nFramesAvailableToWrite, 0) whole frames ahead, so that the
   * next write does not overwrite an unread frame, and at most
   * nFramesAvailableToRead ahead with a zero fraction, so that the last read
   * (before the last increment) has every interpolator tap written.
   *
   * @param chunk the m_NFramesPerProcess increments of the curve and their sum
   *   (the advance of the read position that resampling with them would make),
   *   as returned by GOSoundVibratoPitchRateCurve::Position::SelectChunk();
   *   must have rates (HasRates()); read-only
   * @param readPosFraction the read position's current fraction
   * @param nFramesAvailableToRead state.GetNFramesAvailableToRead() after this
   *   round's write
   * @param nFramesAvailableToWrite state.GetNFramesAvailableToWrite() after
   *   this round's write
   * @param pTmpIncrements scratch space for m_NFramesPerProcess increments;
   *   written only on a rescale
   * @return chunk.pRateUnits itself if no rescale was needed, otherwise
   *   pTmpIncrements
   */
  const unsigned *GetResamplerPositionIncrementsForUse(
    const GOSoundVibratoPitchRateCurve::ChunkDescription &chunk,
    unsigned readPosFraction,
    unsigned nFramesAvailableToRead,
    unsigned nFramesAvailableToWrite,
    unsigned *pTmpIncrements) const;

  /**
   * Resamples the delay buffer into outBuffer with the given per-frame
   * increments of the resampler's position, by the resampler of
   * m_InterpolationType, and advances the state's read position by their sum.
   *
   * @param state the processor state whose delay buffer is read and whose read
   *   position is advanced
   * @param pPositionIncrements outBuffer.GetNFrames() increments of the
   *   resampler's position, as returned by
   *   GetResamplerPositionIncrementsForUse()
   * @param outBuffer the planar buffer to fill; it must have as many channels
   *   as the ring
   */
  void ReadFromDelayBuffer(
    GOSoundVibratoProcessorState &state,
    const unsigned *pPositionIncrements,
    GOSoundBufferPlanarMutable &outBuffer) const;

public:
  /**
   * Stores resample and interpolationType.
   * @param resample the shared kernel instance this processor reads from for
   *   the lifetime of this object - see r_resample
   * @param interpolationType which resampler Process() uses for the lifetime
   *   of this object. Defaulted to the higher-quality polyphase one.
   */
  explicit GOSoundVibratoProcessor(
    const GOSoundResample &resample,
    GOSoundResample::InterpolationType interpolationType
    = GOSoundResample::GO_POLYPHASE_INTERPOLATION);

  /**
   * Sets the setup fields from the format (the delay ring covers
   * VIBRATO_MAX_LAG_MS) and unbinds the pitch rate curve: its owner installs
   * it again with SetPitchRateCurve().
   * @param nChannels see m_NChannels
   * @param nFrames the round's frame count, at least 2 - see
   *   m_NFramesPerProcess
   * @param sampleRate see m_SampleRate
   */
  void EnsureSetup(
    unsigned nChannels, unsigned nFrames, unsigned sampleRate) override;

  /**
   * Installs the pitch rate curve to walk, or nullptr for bypass. A no-op if
   * the new curve is the same as the current one (compared by id, not by
   * address: the old pointer may point to a destroyed curve, and a rebuilt
   * curve has a fresh id). The curve must be built for the format of the last
   * EnsureSetup(). Does not allocate: it is safe to call from a worker thread.
   * @param pCurve the curve, or nullptr to bypass; must stay valid while it is
   *   installed
   */
  void SetPitchRateCurve(const GOSoundVibratoPitchRateCurve *pCurve);

protected:
  /**
   * @return a new state sized for the last EnsureSetup(); asserts that
   *   EnsureSetup() was called
   */
  std::unique_ptr<GOSoundVibratoProcessorState> CreateTypedState()
    const override;

  /**
   * When the processor has a curve: writes the buffer into the delay buffer
   * and reads it back with the resampling rates of the curve, rescaled if they
   * would take the read position out of its allowed range.
   *
   * When the processor has no curve: leaves the buffer untouched and resets the
   * delay positions of the state, so the next activation starts at the minimum
   * lag and the bypass costs no latency.
   */
  void Process(
    GOSoundVibratoProcessorState &state,
    GOSoundBufferPlanarMutable &buffer) const override;
};

#endif /* GOSOUNDVIBRATOPROCESSOR_H */
