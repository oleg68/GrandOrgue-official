/*
 * GrandOrgue - free pipe organ simulator based on MyOrgan
 *
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef GOSOUNDRESAMPLE_H_
#define GOSOUNDRESAMPLE_H_

#include <cassert>
#include <cmath>
#include <cstdint>

/**
 * A stateless DSP kernel: this class provides algorithms for resampling
 * audio buffers
 * Now two algorithms are supported: Linear and Polyphase.
 * They calculate a next output sample based on a vector of a few continous
 * input samples.
 */

class GOSoundResample {
public:
  static constexpr unsigned POLYPHASE_POINTS = 8;
  static constexpr unsigned LINEAR_POINTS = 2;
  static constexpr unsigned UPSAMPLE_BITS = 13;
  static constexpr unsigned UPSAMPLE_FACTOR = 1 << UPSAMPLE_BITS;
  static constexpr unsigned UPSAMPLE_MASK = UPSAMPLE_FACTOR - 1;

  enum InterpolationType {
    GO_LINEAR_INTERPOLATION = 0,
    GO_POLYPHASE_INTERPOLATION = 1,
  };

  /**
   * Converts a resampling-rate multiplier (source frames per target frame,
   * e.g. 1.0 = no change, 2^(cents/1200) for a pitch shift) into the
   * 1/UPSAMPLE_FACTOR-unit increment ResamplingPosition::Inc(unsigned) and
   * Init() use.
   */
  static inline unsigned rateToFractionIncrement(float rate) {
    return (unsigned)roundf(rate * UPSAMPLE_FACTOR);
  }

  /**
   * A position in the source sample stream for the current position in the
   * target stream. Because the numbers of source and target samples differ, the
   * source position may be not integer and has a fractional part
   */
  class ResamplingPosition {
  private:
    // an integer part
    unsigned m_index;
    // a fractional part in 1/UPSAMPLE_FACTOR units
    unsigned m_fraction;
    // Increment of the source position for one target sample in
    //   1/UPSAMPLE_FACTOR units
    unsigned m_FractionIncrement;

  public:
    inline unsigned GetIndex() const { return m_index; }
    inline void SetIndex(unsigned newIndex) { m_index = newIndex; }
    inline unsigned GetFraction() const { return m_fraction; }
    inline unsigned GetFractionIncrement() const { return m_FractionIncrement; }

    /**
     * A resampling factor equals to (source length / target length) or to
     * (source sample rate / target sample rate)
     * @return the resampling factor
     */
    inline float GetResamplingFactor() const {
      return (float)m_FractionIncrement / UPSAMPLE_FACTOR;
    }

    void Init(
      float factor,
      unsigned startIndex = 0,
      const ResamplingPosition *pOld = nullptr);

    /**
     * Advance the position for the next target frame by an explicit
     * increment, for a time-varying resampling rate (e.g. vibrato). Unlike
     * Inc(), this does not use m_FractionIncrement, so it does not disturb
     * AvailableTargetSamples() or GetResamplingFactor() for streams that
     * also use the constant-rate path.
     * @param fractionIncrement the source-position advance for this one
     *   target frame, in 1/UPSAMPLE_FACTOR units.
     */
    inline void Inc(unsigned fractionIncrement) {
      m_fraction += fractionIncrement;
      m_index += m_fraction >> UPSAMPLE_BITS;
      m_fraction &= UPSAMPLE_MASK;
    }

    /**
     * Advance the position for the next target frame using the stream's
     * constant resampling rate. Equivalent to Inc(m_FractionIncrement).
     */
    inline void Inc() { Inc(m_FractionIncrement); }

    /**
     * Calculates the target samples length from given source position to the
     * end index
     * @param endIndex the first index position after the last source sample
     * @result - the number of tatget samples can be received before the current
     *   position exceeds endIndex specified
     */
    inline unsigned AvailableTargetSamples(unsigned endIndex) {
      return m_index < endIndex ? unsigned(
               (
                 // for preventing owerflowing if endIndex >= 2**19 ~ 550000
                 uint64_t(endIndex - m_index) * UPSAMPLE_FACTOR
                 + m_FractionIncrement - 1 // for rounding up
                 - m_fraction)
               / m_FractionIncrement)
                                : 0;
    }
  };

  /**
   * Represents an abstract vector of continous input samples somethere in the
   * input stream. It has nChannels input channels (1 - mono, 2 - stereo)
   * It has two main methods:
   * - void Seek(unsigned index, uint8_t channel) - sets the vector to the index
   *   position in the input stream for the specified channel. The
   *   implementation may restrict moving the position only forward
   * - void NextItem() - returns the next sample of the same channel. The
   *   implementation must provide sufficient number of samples required by
   *   the certain resampling algorithm.
   *
   * These methods are not virtual for a better performance. Their
   * implementation is substituted inline from the subclasses by a resampler.
   */
  template <uint8_t nChannels> struct FloatingFrameVector {
    static constexpr uint8_t m_NChannels = nChannels;
  };

  /**
   * A vector of continous samples in a memory region referenced by a pointer.
   * SrcItemT - a type of one sample. Usually int8_t, int16_t, GOInt24, or
   * float
   * ResItemT - a type of one sample returned by NextItem(). Usually int or
   * float
   * nChannels - number of channels in the source stream
   *
   * If nChannels>1 it asumes that samples are interleaving for several channels
   *     - 0 left
   *     - 0 right
   *     - 1 left
   *     - 1 right
   *     - ...
   * For the performance reason this NextItem() does not check for the bounds.
   * The calling program must ensure that there are sufficient number of samples
   */
  template <class SrcItemT, class ResItemT, uint8_t nChannels>
  class PtrFrameVector : public FloatingFrameVector<nChannels> {
  private:
    // points to the first sample of the 0-channel in the input stream
    const SrcItemT *p_StartPtr;
    // points to the current sample in the vector
    const SrcItemT *p_CurrPtr;

  protected:
    /**
     * Checks that the current sample pointer is before the specified end
     * pointer
     * @param endPtr the end pointer
     * @return are there more samples in the vector
     */
    inline bool IsBefore(const SrcItemT *endPtr) const {
      return p_CurrPtr < endPtr;
    }

  public:
    /**
     * Construct the vector with some start pointer.
     * @param ptr a pointer to the first (0 left) sample
     */
    inline PtrFrameVector(const SrcItemT *ptr) : p_StartPtr(ptr) {}

    /**
     * Moves the current sample pointer to the specified position in the input
     * stream for the channel specified
     * @param ptr a pointer to the first (0 left) sample
     */
    inline void Seek(unsigned index, uint8_t channel) {
      p_CurrPtr = p_StartPtr + nChannels * index + channel;
    }

    /**
     * Returns the next sample from the vector and moves the current sample
     * pointer to the next sample of the same channel
     * @return the sample
     */
    inline ResItemT NextItem() {
      ResItemT res = (ResItemT)*p_CurrPtr;
      p_CurrPtr += nChannels;
      return res;
    }

    /**
     * No-op: this vector addresses a flat, non-recycled memory region, so
     * ResamplingPosition's index never needs correcting. Exists only so
     * ResampleBlockImpl() can call fV.NormalizePosition(resamplingPos)
     * unconditionally, resolved statically per FrameVectorT with no
     * runtime branch - see RingPlanarFrameVector's own override below for
     * the vector type that actually needs this.
     */
    inline void NormalizePosition(ResamplingPosition &) const {}
  };

  /**
   * A vector of continous samples in memory with checking for bounds on
   * NextItem().
   * This checking reduces the performance dramatically so it is intended to use
   * only in not realtime cases (for example, on loading, but not when playing)
   */
  template <class SrcItemT, class ResItemT, uint8_t nChannels>
  class BoundedPtrFrameVector
    : public PtrFrameVector<SrcItemT, ResItemT, nChannels> {
  private:
    /**
     * Points to the end of the memory region
     */
    const SrcItemT *p_EndPtr;

  public:
    /**
     * Constructs the vector with the start pointer and the length of the
     * memory region
     * @param ptr - a pointer to the first sample in the region
     * @param len - a number of samples of each channels
     */
    inline BoundedPtrFrameVector(const SrcItemT *ptr, unsigned len)
      : PtrFrameVector<SrcItemT, ResItemT, nChannels>(ptr),
        p_EndPtr(ptr + nChannels * len) {}

    inline ResItemT NextItem() {
      return PtrFrameVector<SrcItemT, ResItemT, nChannels>::IsBefore(p_EndPtr)
        ? PtrFrameVector<SrcItemT, ResItemT, nChannels>::NextItem()
        : (ResItemT)0;
    }
  };

  /**
   * A read-only FloatingFrameVector view over a planar (channel-major) ring
   * buffer, for a source whose read position can advance at a varying,
   * non-integer rate and wrap around a bounded window of history — e.g. a
   * modulated delay line such as pitch vibrato, the motivating use case but
   * not a dependency of this class. Channel-major like GOSoundBufferPlanar,
   * but strided by this class's own nPhysicalFrames, not
   * GOSoundBufferPlanar's nFrames: channel c starts at pRingStart +
   * c * nPhysicalFrames.
   *
   * Read only via ResampleBlock()/ResampleBlockVariableRate(), which call
   * NormalizePosition() every frame — Seek() only asserts the index is
   * already below nLogicalFrames, it does not wrap it.
   */
  template <class SrcItemT, class ResItemT, uint8_t nChannels>
  class RingPlanarFrameVector : public FloatingFrameVector<nChannels> {
  private:
    const SrcItemT *p_RingStart;
    const SrcItemT *p_CurrPtr;
#ifndef NDEBUG
    const SrcItemT *p_EndPtr;
#endif
    unsigned m_NLogicalFrames;
    unsigned m_NPhysicalFrames;

  public:
    /**
     * @param pRingStart pointer to channel 0's physical sub-buffer.
     * @param nLogicalFrames the ring's logical frame count — the index
     *   passed to Seek() must already be below this; must be > 0.
     * @param nPhysicalFrames each channel's actual allocated length in
     *   frames; must exceed nLogicalFrames by at least the reading
     *   resampler's VECTOR_LENGTH, to hold the mirrored tail Seek()/
     *   NextItem() rely on.
     */
    inline RingPlanarFrameVector(
      const SrcItemT *pRingStart,
      unsigned nLogicalFrames,
      unsigned nPhysicalFrames)
      : p_RingStart(pRingStart),
        m_NLogicalFrames(nLogicalFrames),
        m_NPhysicalFrames(nPhysicalFrames) {
      assert(nLogicalFrames > 0);
      // Only the minimum possible margin is checked here - this class does
      // not know the reading resampler's VECTOR_LENGTH (nPoints), so it
      // cannot verify the caller actually left enough room for the mirrored
      // tail the class comment requires. A too-small margin is instead
      // caught where it actually matters, by NextItem()'s p_EndPtr assert.
      assert(nPhysicalFrames > nLogicalFrames);
    }

    /**
     * Pins the channel and points to the given index directly - index must
     * already be below nLogicalFrames (see the class comment).
     */
    inline void Seek(unsigned index, uint8_t channel) {
      assert(channel < nChannels);
      assert(index < m_NLogicalFrames);

      const SrcItemT *pChannelBase = p_RingStart + channel * m_NPhysicalFrames;

      p_CurrPtr = pChannelBase + index;
#ifndef NDEBUG
      p_EndPtr = pChannelBase + m_NPhysicalFrames;
#endif
    }

    /**
     * Returns the seeked channel's next item and advances by one frame.
     * Relies on the ring owner having mirrored each channel's first
     * (nPhysicalFrames - nLogicalFrames) frames into that channel's own
     * tail, so a run of calls that crosses nLogicalFrames still reads valid
     * data instead of needing a second wrap; asserts against p_EndPtr so an
     * over-long run (more calls than the mirror covers) fails loudly in
     * Debug instead of silently reading adjacent heap memory.
     */
    inline ResItemT NextItem() {
      assert(p_CurrPtr < p_EndPtr);

      return (ResItemT) * (p_CurrPtr++);
    }

    /**
     * Keeps resamplingPos's index below this ring's own nLogicalFrames,
     * called once per output frame by ResampleBlockImpl() (see
     * PtrFrameVector::NormalizePosition() for why this call site is
     * unconditional and zero-cost for non-ring vectors). A single
     * conditional subtract suffices as long as one frame's growth stays
     * below nLogicalFrames, which holds for any physically sane
     * rate/ring-size pairing; the assert catches a violation instead of
     * silently under-correcting.
     */
    inline void NormalizePosition(ResamplingPosition &pos) const {
      const unsigned index = pos.GetIndex();

      if (index >= m_NLogicalFrames)
        pos.SetIndex(index - m_NLogicalFrames);
      assert(pos.GetIndex() < m_NLogicalFrames);
    }
  };

  // These cofficients are calculated in the constructor and are not more
  // changed

  // The coefficients for linear interpolation
  float m_LinearCoefs[UPSAMPLE_FACTOR][LINEAR_POINTS];
  // The coefficients for polyphase interpolation
  float m_PolyphaseCoefs[UPSAMPLE_FACTOR][POLYPHASE_POINTS];

  GOSoundResample();

  /**
   * A resampler that calculates a next output sample as a scalar production of
   * the vector of continous input samples and the vector of coefficients
   */
  template <unsigned nPoints> class ScalarProductionResampler {
  private:
    // precalculated coefficients for each resampling position fraction
    const float (&r_coefs)[UPSAMPLE_FACTOR][nPoints];

  protected:
    inline ScalarProductionResampler(
      const float (&coefs)[UPSAMPLE_FACTOR][nPoints])
      : r_coefs(coefs) {}

  public:
    /**
     * @return Necessary number of continous input samples for calculating one
     * output sample
     */
    static constexpr unsigned VECTOR_LENGTH = nPoints;

  private:
    /**
     * Shared implementation of ResampleBlock() and
     * ResampleBlockVariableRate(). PosIncrementSourceT must provide
     * `unsigned NextIncrement()`, self-advancing and returning the Inc()
     * argument for the next output frame on each call (exactly one call
     * per output frame, in order) — the same calling convention as
     * FloatingFrameVector::NextItem(). Being a template parameter (not a
     * std::function/virtual call), it is fully inlined, so a
     * constant-returning source compiles to the same code as today's
     * unconditional resamplingPos.Inc(). fV.NormalizePosition() is called
     * the same way, unconditionally - it is a no-op for a flat FrameVectorT
     * (PtrFrameVector) and the real ring-wrap correction for
     * RingPlanarFrameVector, resolved statically per FrameVectorT with no
     * runtime branch here.
     * @param resamplingPos A resampling position in the input stream. It is
     *   advanced during this call
     * @param fV a floating sample vector linked to the input stream
     * @param posIncrementSource supplies the per-frame Inc() argument
     * @param pOut a pointer to the output sample buffer in interleaving format.
     *   Must have at least nOutChannels*nOutFrames length
     * @param nOutFrames a number of output frames
     */
    template <
      class FrameVectorT,
      uint8_t nOutChannels,
      class PosIncrementSourceT>
    inline void ResampleBlockImpl(
      ResamplingPosition &resamplingPos,
      FrameVectorT &fV,
      PosIncrementSourceT posIncrementSource,
      float *pOut,
      unsigned nOutFrames) const {
      for (unsigned nFramesLeft = nOutFrames; nFramesLeft > 0; nFramesLeft--) {
        const float(&coefs)[nPoints] = r_coefs[resamplingPos.GetFraction()];
        float outItem = 0.0f;

        for (uint8_t ch = 0; ch < nOutChannels; ch++) {
          if (ch < FrameVectorT::m_NChannels) {
            const float *pCoef = coefs;

            fV.Seek(resamplingPos.GetIndex(), ch);
            // calculate the next output item as a scalar production of the
            // input sample vector and the vector of coefficients
            outItem = 0.0f;
            for (unsigned j = 0; j < nPoints; j++)
              outItem += fV.NextItem() * *(pCoef++);
          }
          /* else copy the calculated item from the previous channel. It is
           * useful only for resampling a mono stream to a stereo one */
          *(pOut++) = outItem;
        }

        resamplingPos.Inc(posIncrementSource.NextIncrement());
        fV.NormalizePosition(resamplingPos);
      }
    }

  public:
    /**
     * A position-increment source returning the same increment every call,
     * for ResampleBlock()'s constant-rate path.
     */
    struct ConstantPosIncrementSource {
      const unsigned m_Increment;

      inline unsigned NextIncrement() { return m_Increment; }
    };

    /**
     * A position-increment source that walks a caller-owned array one entry
     * per call, for ResampleBlockVariableRate()'s time-varying path. Mirrors
     * PtrFrameVector: a bare pointer bump, no bounds check, no modulo - the
     * caller guarantees at least nOutFrames entries.
     */
    struct ArrayPosIncrementSource {
      const unsigned *p_Next;

      inline unsigned NextIncrement() { return *(p_Next++); }
    };

    /**
     * Do actual resampling of an input sample block to the output block of the
     *   given number of frames, at the stream's constant resampling rate
     * @param resamplingPos A resampling position in the input stream. It is
     *   advanced during this call
     * @param fV a floating sample vector linked to the input stream
     * @param pOut a pointer to the output sample buffer in interleaving format.
     *   Must have at least nOutChannels*nOutFrames length
     * @param nOutFrames a number of output frames
     */
    template <class FrameVectorT, uint8_t nOutChannels>
    inline void ResampleBlock(
      ResamplingPosition &resamplingPos,
      FrameVectorT &fV,
      float *pOut,
      unsigned nOutFrames) const {
      ResampleBlockImpl<FrameVectorT, nOutChannels>(
        resamplingPos,
        fV,
        ConstantPosIncrementSource{resamplingPos.GetFractionIncrement()},
        pOut,
        nOutFrames);
    }

    /**
     * Like ResampleBlock(), but the source-position advance is supplied per
     * output frame instead of being constant, for a time-varying resampling
     * rate (vibrato). resamplingPos's own m_FractionIncrement is never read.
     * @param resamplingPos A resampling position in the input stream. It is
     *   advanced during this call
     * @param fV a floating sample vector linked to the input stream
     * @param pFractionIncrements the first of at least nOutFrames increments,
     *   in 1/UPSAMPLE_FACTOR units (source frames per target frame, scaled
     *   by UPSAMPLE_FACTOR and rounded — see
     *   GOSoundResample::rateToFractionIncrement()).
     * @param pOut a pointer to the output sample buffer in interleaving format.
     *   Must have at least nOutChannels*nOutFrames length
     * @param nOutFrames a number of output frames
     */
    template <class FrameVectorT, uint8_t nOutChannels>
    inline void ResampleBlockVariableRate(
      ResamplingPosition &resamplingPos,
      FrameVectorT &fV,
      const unsigned *pFractionIncrements,
      float *pOut,
      unsigned nOutFrames) const {
      ResampleBlockImpl<FrameVectorT, nOutChannels>(
        resamplingPos,
        fV,
        ArrayPosIncrementSource{pFractionIncrements},
        pOut,
        nOutFrames);
    }
  };

  /**
   * This resampler use a scalar production of vector of two samples and a
   * vector of two coefficients
   */
  struct LinearResampler : public ScalarProductionResampler<LINEAR_POINTS> {
    inline LinearResampler(const GOSoundResample &r)
      : ScalarProductionResampler<LINEAR_POINTS>(r.m_LinearCoefs) {}
  };

  /**
   * This resampler use a scalar production of vector of eight samples and a
   * vector of eight coefficients
   */
  struct PolyphaseResampler
    : public ScalarProductionResampler<POLYPHASE_POINTS> {
    inline PolyphaseResampler(const GOSoundResample &r)
      : ScalarProductionResampler<POLYPHASE_POINTS>(r.m_PolyphaseCoefs) {}
  };

  /**
   * Returns the necessary sample vector length for the given interpolation type
   * @param interpolation - the interpolation type
   * @return the number of samples
   */
  static unsigned getVectorLength(InterpolationType interpolation);

  /**
   * Allocate a new sample block and fill it with resampled data. Assume that
   *   the samples are mono
   * @param data a pointer to the source samples
   * @param before call - number of source samples. After call it is filled
   *   with number of target samples
   * @param from_samplerate source samplerate
   * @param to_samplerate target samplerate
   * @return a pointer to the new allocated data block. The caller is
   *   responsible to free this block
   */
  float *NewResampledMono(
    const float *data,
    unsigned &len,
    unsigned from_samplerate,
    unsigned to_samplerate);
};

#endif /* GOSOUNDRESAMPLE_H_ */
