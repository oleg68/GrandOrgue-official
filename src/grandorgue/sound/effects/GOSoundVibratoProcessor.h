/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDVIBRATOPROCESSOR_H
#define GOSOUNDVIBRATOPROCESSOR_H

#include <cassert>

#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/processing/GOSoundProcessorTyped.h"

#include "GOSoundVibratoProcessorState.h"

class GOTestSoundVibratoProcessor;

/**
 * Effect processor for a windchest's processing chain: applies a
 * precomputed per-frame pitch-rate curve (the caller's Doppler multiplier,
 * derived ahead of time from a cents-based LFO - see p_PitchRateCurve) to a
 * buffer via a modulated delay line, inducing a Doppler-shift vibrato - the
 * pitch half of a synthesized tremulant (issue #709). Built on
 * GOSoundVibratoProcessorState's ring and
 * GOSoundResample::RingPlanarFrameVector /
 * ResampleBlockVariableRatePlanar(). Holds only parameters (the current
 * rate curve pointer, format, and tuning constants); every mutable byte a
 * chain needs lives in GOSoundVibratoProcessorState, since Process() is
 * const and one processor instance is shared across many chain states.
 * SetPitchRateCurve(nullptr, ...) bypasses at zero cost - an organ with no
 * pitch tremulant pays nothing for this processor being in the chain.
 *
 * Per-frame read rate (see Process()): p_PitchRateCurve[i] already holds
 * the Doppler multiplier 2^(c[i] / 1200) for this frame's cents value c[i],
 * computed by the caller (a mapper driving this processor from a cents
 * curve) outside the audio thread - exp2f() must never run here. Process()
 * adds one lag-feedback term per round, not per frame (lag barely moves
 * within a single round - see Process()'s "Rate per frame" step): given
 * current lag (read head distance behind the write head), nominal lag D0
 * (m_NominalLagFrames) and feedback coefficient k (m_LagFeedbackCoeff),
 *
 * rate[i] = p_PitchRateCurve[i] + k * (lag - D0)
 *
 * The first term is the Doppler shift itself; the second is a slow
 * feedback pulling lag back toward D0, correcting the convexity drift a
 * symmetric cents curve would otherwise accumulate (2^(c/1200) does not
 * average to 1 over a symmetric c). state.m_ReadHeadPosDesired accumulates
 * rate every frame; D0 is also where GOSoundVibratoProcessorState::Reset()
 * re-primes lag on every bypass round.
 */
class GOSoundVibratoProcessor
  : public GOSoundProcessorTyped<GOSoundVibratoProcessorState> {
  friend class GOTestSoundVibratoProcessor; // direct access for unit tests

  /**
   * The ring's logical size, in milliseconds: how far behind the write
   * head the read head may fall before being clamped. Sized from a
   * worst-case sizing check (100 cents at 3 Hz needs 3.2 ms of lag
   * headroom) with generous margin for pathological settings; a
   * pathological setting degrades gracefully via the clamp rather than
   * corrupting the ring.
   */
  static constexpr float VIBRATO_MAX_LAG_MS = 100.0f;

  /**
   * D0: the read-head-behind-write-head distance, in milliseconds, the
   * ring starts and re-settles at whenever it (re)primes (see
   * GOSoundVibratoProcessorState::Reset()). Small on purpose - a windchest
   * with vibrato must not run audibly behind one without it - yet large
   * enough to cover the sizing check above plus
   * N_DELAY_BUFFER_TAIL_FRAMES's mirror margin (see
   * m_NDelayBufferRingFrames).
   */
  static constexpr float VIBRATO_NOMINAL_LAG_MS = 5.0f;

  /**
   * Time constant, in seconds, of the lag-feedback term that corrects the
   * convexity drift from a symmetric cents curve not averaging to a
   * zero-mean read rate. Chosen far longer than any sane LFO period, so
   * the feedback term never fights the vibrato itself, only its slow
   * drift.
   */
  static constexpr float VIBRATO_LAG_FEEDBACK_TIME_CONSTANT_S = 1.5f;

  /**
   * The ring's mirrored tail length, in frames - must equal
   * GOSoundResample::MAX_POINTS, since the resampler's Seek()/NextItem()
   * can read up to that many items past the current index, and the
   * mirrored tail is what makes such a read always valid.
   */
  static constexpr unsigned N_DELAY_BUFFER_TAIL_FRAMES
    = GOSoundResample::MAX_POINTS;

  /**
   * N_DELAY_BUFFER_TAIL_FRAMES, as a float - precomputed for the same
   * reason as m_NMaxLagFramesF, so Process()'s per-frame loop does the
   * clamp bounds in pure float arithmetic.
   */
  static constexpr float N_DELAY_BUFFER_TAIL_FRAMES_F
    = (float)N_DELAY_BUFFER_TAIL_FRAMES;

  // --- Set by the constructor, fixed for this object's lifetime ---

  /**
   * The shared resampling kernel (coefficient tables + resample
   * algorithms) this processor reads from. Injected by reference rather
   * than owned, so every windchest's vibrato processor shares one set of
   * coefficient tables instead of duplicating ~300 KB each - the same
   * sharing pattern as GOSoundSamplerPlayer::m_resample.
   */
  const GOSoundResample &r_resample;

  /**
   * Which GOSoundResample resampler (Linear/Polyphase) Process() uses to
   * read the ring. Fixed for this object's lifetime, alongside r_resample
   * - the code that builds this processor's chain passes both to the
   * constructor together, from the same one-time GOConfig read
   * GOSoundSamplerPlayer already does for its own streams. Not a
   * mapper-driven value (unlike p_PitchRateCurve below): nothing touches
   * it after construction. The ring's mirrored tail is always sized for
   * N_DELAY_BUFFER_TAIL_FRAMES regardless of which resampler this selects
   * (see m_NDelayBufferRingFrames) - Linear just leaves a few frames of
   * that margin unused, which is simpler than carrying a separate
   * interpolation-type-dependent tail length.
   */
  GOSoundResample::InterpolationType m_InterpolationType;

  // --- Set by EnsureSetup(), recomputed only when their inputs change ---

  /**
   * Channel count from the most recent EnsureSetup() call; sizes the
   * state's ring.
   */
  unsigned m_NChannels = 0;

  /**
   * Sample rate from the most recent EnsureSetup() call; drives every
   * millisecond-based constant's conversion to frames.
   */
  unsigned m_SampleRate = 0;

  /**
   * The round's frame count, from the most recent EnsureSetup() call -
   * i.e. how many frames a single Process() call is expected to handle
   * (matching buffer.GetNFrames() every round). This is the length
   * SetPitchRateCurve()'s pRates must have whenever it is non-null:
   * p_PitchRateCurve is read as p_PitchRateCurve[0..m_NFramesPerProcess),
   * one value per output frame, so a shorter array would read past its end
   * and a longer one would silently waste entries no one reads.
   * SetPitchRateCurve() asserts its own nFrames argument equals this.
   */
  unsigned m_NFramesPerProcess = 0;

  /**
   * D0 in frames: VIBRATO_NOMINAL_LAG_MS/1000 * m_SampleRate. float, not
   * double - set once by EnsureSetup() and only read afterward, never
   * accumulated, unlike GOSoundVibratoProcessorState::m_ReadHeadPosDesired.
   */
  float m_NominalLagFrames = 0;

  /**
   * The read head's upper lag clamp, in frames: ceil(m_NominalLagFrames *
   * VIBRATO_MAX_LAG_MS / VIBRATO_NOMINAL_LAG_MS) (the two constants share
   * a fixed ratio, so this reuses m_NominalLagFrames's sampleRate scaling
   * instead of redoing it). Computed once here by EnsureSetup(), not
   * re-derived by Process() from m_NDelayBufferRingFrames - both are
   * computed from this same value in the same place, so storing it
   * directly avoids two formulas that must otherwise be kept in sync by
   * hand (m_NDelayBufferRingFrames = m_NMaxLagFrames +
   * m_NFramesPerProcess).
   */
  unsigned m_NMaxLagFrames = 0;

  /**
   * m_NMaxLagFrames, as a float - precomputed here (alongside
   * m_NMaxLagFrames itself) so Process()'s per-frame loop does the clamp
   * bounds in pure float arithmetic, without an unsigned-to-double
   * promotion on every frame.
   */
  float m_NMaxLagFramesF = 0;

  /**
   * The ring's logical length in frames: m_NMaxLagFrames +
   * m_NFramesPerProcess. The extra +m_NFramesPerProcess is headroom, not
   * part of the clamped lag range: it is what guarantees the write head
   * can never lap the read head - see Process()'s "Write" step for why
   * this specific amount of headroom is exactly what's needed. The ring's
   * mirrored tail (N_DELAY_BUFFER_TAIL_FRAMES frames, always - see
   * m_InterpolationType) is allocated on top of this length, not counted
   * in it.
   */
  unsigned m_NDelayBufferRingFrames = 0;

  /**
   * m_NDelayBufferRingFrames * GOSoundResample::UPSAMPLE_FACTOR - the ring
   * size expressed in the same fixed-point "units" as
   * GOSoundResample::ResamplingPosition's combined index+fraction value.
   * Computed once here, alongside m_NDelayBufferRingFrames, rather than
   * every Process() call, since it depends on nothing else - see
   * Process()'s drift-free fixed-point conversion, which wraps the
   * per-frame units difference into this modulus.
   */
  unsigned m_NUnitsRing = 0;

  /**
   * k in the per-frame lag-feedback term k*(lag - m_NominalLagFrames)
   * added to every frame's read rate. Derived from
   * VIBRATO_LAG_FEEDBACK_TIME_CONSTANT_S and m_SampleRate so the term's
   * time constant is correct regardless of sample rate. float, not double
   * - set once by EnsureSetup() and only read afterward, never
   * accumulated.
   */
  float m_LagFeedbackCoeff = 0;

  /**
   * Whether EnsureSetup() has been called at least once; CreateTypedState()
   * asserts this, mirroring GOSoundShelfFilterProcessor's own flag.
   */
  bool m_IsSetupCalled = false;

  // --- Changed every round by the mapper wiring this processor ---

  /**
   * The current round's per-frame read-rate curve: p_PitchRateCurve[i] is
   * the Doppler multiplier 2^(cents[i] / 1200) for output frame i, already
   * computed by the caller from its own cents curve - exp2f() must never
   * run on the audio thread, so this processor never sees raw cents, only
   * the already-exponentiated rate. nullptr means bypass. Non-owning: the
   * caller retains ownership and must keep it valid only for the
   * Process() call(s) it was set for. When non-null, always has exactly
   * m_NFramesPerProcess entries - see m_NFramesPerProcess and
   * SetPitchRateCurve(). Reset to nullptr by EnsureSetup() - defensive
   * against a stale pointer of the wrong length surviving an nFrames
   * change (see EnsureSetup()).
   */
  const float *p_PitchRateCurve = nullptr;

  /**
   * Add two values and wrap the result into [0, nRing) - the shared
   * implementation behind the two-argument RingAdd(a, b) below (which
   * always wraps into m_NDelayBufferRingFrames) and Process()'s per-frame
   * fixed-point diff (which wraps into m_NUnitsRing instead). Templated so
   * it works for both the frame-granularity unsigned positions (e.g. the
   * write head) and the fractional double ones (e.g.
   * state.m_ReadHeadPosDesired).
   * @param a a value, already in [0, nRing)
   * @param b a value, already in [0, nRing)
   * @param nRing the modulus to wrap into
   * @return (a + b) mod nRing
   */
  template <class T> static inline T RingAdd(T a, T b, T nRing) {
    assert(a < nRing);
    assert(b < nRing);

    const T sum = a + b;

    return sum >= nRing ? sum - nRing : sum;
  }

  /**
   * Add ring positions and wrap the result into [0,
   * m_NDelayBufferRingFrames) - see the three-argument RingAdd() above.
   * @param a a ring position, already in [0, m_NDelayBufferRingFrames)
   * @param b an advance amount, already in [0, m_NDelayBufferRingFrames)
   * @return (a + b) mod m_NDelayBufferRingFrames
   */
  template <class T> inline T RingAdd(T a, T b) const {
    return RingAdd(a, b, (T)m_NDelayBufferRingFrames);
  }

  /**
   * Subtract two values and wrap the result into [0, nRing) - the shared
   * implementation behind the two-argument RingSub(a, b) below and
   * Process()'s per-frame fixed-point diff (which wraps into m_NUnitsRing
   * instead of m_NDelayBufferRingFrames). Templated so it works for both
   * the frame-granularity unsigned positions and the fractional double
   * ones.
   * @param a a value, already in [0, nRing)
   * @param b a value, already in [0, nRing)
   * @param nRing the modulus to wrap into
   * @return (a - b) mod nRing
   */
  template <class T> static inline T RingSub(T a, T b, T nRing) {
    assert(a < nRing);
    assert(b < nRing);

    return a >= b ? a - b : a + nRing - b;
  }

  /**
   * Subtract ring positions and wrap the result into [0,
   * m_NDelayBufferRingFrames) - see the three-argument RingSub() above.
   * @param a a ring position, already in [0, m_NDelayBufferRingFrames)
   * @param b a ring position, already in [0, m_NDelayBufferRingFrames)
   * @return (a - b) mod m_NDelayBufferRingFrames
   */
  template <class T> inline T RingSub(T a, T b) const {
    return RingSub(a, b, (T)m_NDelayBufferRingFrames);
  }

protected:
  /**
   * @return a new GOSoundVibratoProcessorState sized for the channel
   *   count/ring geometry from the most recent EnsureSetup() call; asserts
   *   EnsureSetup() was already called.
   */
  std::unique_ptr<GOSoundVibratoProcessorState> CreateTypedState()
    const override;

  /**
   * Bypass (p_PitchRateCurve == nullptr): leaves buffer untouched and
   * calls state.Reset(), so the next activation always starts at lag D0
   * and bypass itself costs no latency. Active: writes buffer's input
   * into state's ring, adds one round-constant lag-feedback term to
   * p_PitchRateCurve's precomputed per-frame Doppler rate, and reads the
   * modulated result back into buffer via r_resample's chosen resampler
   * and RingPlanarFrameVector. A pathological curve that would push lag
   * past its clamp instead flattens: the effective read rate sticks to
   * the clamp boundary (no ring corruption) until lag drifts back inside
   * range - audible only as the vibrato's extremum flattening, not as a
   * glitch.
   */
  void Process(
    GOSoundVibratoProcessorState &state,
    GOSoundBufferPlanarMutable &buffer) const override;

public:
  /**
   * Stores resample and interpolationType.
   * @param resample the shared kernel instance this processor reads from
   *   for the lifetime of this object - see r_resample
   * @param interpolationType which resampler Process() uses for the
   *   lifetime of this object - see m_InterpolationType. Defaulted to
   *   GO_POLYPHASE_INTERPOLATION (the higher-quality kernel, and
   *   GOSoundResample's own better-at-the-same-cost choice) so tests and
   *   callers that don't care about interpolation choice get the one
   *   actually worth defaulting to, not just the cheaper one.
   */
  explicit GOSoundVibratoProcessor(
    const GOSoundResample &resample,
    GOSoundResample::InterpolationType interpolationType
    = GOSoundResample::GO_POLYPHASE_INTERPOLATION);

  /**
   * Always overwrites m_NChannels and m_NFramesPerProcess (cheap,
   * unguarded) and resets p_PitchRateCurve to nullptr (defensive against a
   * stale, now-wrong-length pointer surviving an nFrames change - see
   * p_PitchRateCurve). m_NominalLagFrames and m_LagFeedbackCoeff are
   * recomputed only when sampleRate changes (mirroring
   * GOSoundShelfFilterProcessor::EnsureSetup()'s Coeffs guard);
   * m_NMaxLagFrames and m_NDelayBufferRingFrames are recomputed whenever
   * either sampleRate or nFrames changes, since the ring's size depends on
   * both (see m_NDelayBufferRingFrames).
   * @param nChannels see m_NChannels
   * @param nFrames the round's frame count - see m_NFramesPerProcess
   * @param sampleRate see m_SampleRate
   */
  void EnsureSetup(
    unsigned nChannels, unsigned nFrames, unsigned sampleRate) override;

  /**
   * Sets this round's precomputed per-frame read-rate curve. nullptr
   * bypasses entirely (see Process()'s doc comment) - the mapper that owns
   * the curve translates its "no curve" representation into this nullptr,
   * so an organ with no pitch tremulant pays no cost here. pRates must
   * already hold the Doppler multiplier 2^(cents[i] / 1200) per frame,
   * computed by the caller ahead of time (typically once per LFO cycle,
   * not once per round) - this processor never calls exp2f() itself,
   * since that cost must never land on the audio thread. Asserts nFrames
   * == m_NFramesPerProcess when pRates is non-null - see
   * m_NFramesPerProcess for why the length must match exactly.
   * @param pRates m_NFramesPerProcess precomputed rate multipliers, one
   *   per output frame, or nullptr for bypass. Must stay valid for the
   *   Process() call(s) it is set for.
   * @param nFrames number of entries in pRates; must equal
   *   m_NFramesPerProcess (the nFrames EnsureSetup() was most recently
   *   called with) whenever pRates is non-null, ignored when pRates is
   *   nullptr.
   */
  void SetPitchRateCurve(const float *pRates, unsigned nFrames);
};

#endif /* GOSOUNDVIBRATOPROCESSOR_H */
