/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDTREMULANTWAVEFORM_H
#define GOSOUNDTREMULANTWAVEFORM_H

#include <cstdint>

#include "model/GOTremulant.h"
#include "sound/buffer/GOSoundTypedBufferLoopMonoManaged.h"
#include "sound/dsp-kernels/GOSoundLinearFaderCore.h"
#include "sound/effects/GOSoundVibratoPitchRateCurve.h"

/**
 * The wave form of a tremulant: everything that is built from a
 * GOTremulant::Description and replaced as a whole when the parameters
 * change. Built on the control thread (by the constructor), then installed by
 * GOSoundTremulantTask::SwapWaveForm(); the audio thread only reads it. It
 * holds no cursor and no round state, those belong to the task.
 *
 * It is a class of its own and not nested in GOSoundTremulantTask because a
 * nested class cannot be forward-declared, and GOSoundOrganEngine.h has to
 * name it (in TremulantWaveFormChange) without including the task.
 */
class GOSoundTremulantWaveForm {
private:
  // the amplitude multiplier per frame, in units: UNIT_AMP is the gain 1.0
  GOSoundTypedBufferLoopMonoManaged<uint16_t> m_AmpUnits;
  // the pitch rates per frame, in resampler units; not built without PM
  GOSoundVibratoPitchRateCurve m_PitchRateCurve;
  // the depth ramps; they have no frames if the time is 0 (instant)
  const GOSoundLinearFaderCore::UnitRamp m_StartRamp;
  const GOSoundLinearFaderCore::UnitRamp m_StopRamp;

public:
  /** The gain 1.0 in the units of the amplitude loop */
  static constexpr unsigned UNIT_AMP = 16384;
  /** UNIT_AMP as a float, for the conversions */
  static constexpr float UNIT_AMP_F = UNIT_AMP;

  /**
   * Converts a description to the integer wave form: the float amplitude to
   * uint16_t (clamped to 0 .. UINT16_MAX), the pitch cents to the normalised
   * rates of the curve, the ramp lengths as they are (0 means an instant
   * ramp). Allocates and does heavy math: call it on the control thread only.
   *
   * @param desc the rendered loop, non-empty; desc.pitchCents is either empty
   *   (no pitch modulation) or of the same length as desc.amp
   * @param nChunkFrames the longest chunk a reader takes from the loop at
   *   once: the frames per audio period; both loops get the tail for it
   */
  GOSoundTremulantWaveForm(
    const GOTremulant::Description &desc, unsigned nChunkFrames);

  /**
   * The amplitude multiplier of the loop at a position, scaled to 1.0 by the
   * depth factor: 1 + depthFactor * (loop value - 1).
   *
   * @param pos a position in this loop (the same loop length; GetChunk()
   *   asserts it)
   * @param depthFactor 0 .. 1; 0 gives 1.0, 1 gives the loop value
   */
  inline float GetAmplitudeAt(
    const GOSoundTypedBufferLoopBase::LoopPosition &pos,
    float depthFactor) const {
    return 1.0f
      + depthFactor * ((float)*m_AmpUnits.GetChunk(pos) / UNIT_AMP_F - 1.0f);
  }

  /** @return the amplitude loop in units */
  const GOSoundTypedBufferLoopMonoManaged<uint16_t> &GetAmpUnits() const {
    return m_AmpUnits;
  }

  /** @return the number of frames of the loop */
  unsigned GetNLoopFrames() const { return m_AmpUnits.GetNLoopFrames(); }

  /** @return true if the wave form modulates the pitch */
  bool HasPitch() const { return m_PitchRateCurve.IsValid(); }

  /** @return the pitch rates; IsValid() is false without PM */
  const GOSoundVibratoPitchRateCurve &GetPitchRateCurve() const {
    return m_PitchRateCurve;
  }

  /**
   * @param isRising true: the ramp of engaging (0 -> 1 over the start time),
   *   false: the ramp of disengaging (1 -> 0 over the stop time)
   * @return the depth ramp; it has no frames if the time is 0 (instant)
   */
  const GOSoundLinearFaderCore::UnitRamp &GetRamp(bool isRising) const {
    return isRising ? m_StartRamp : m_StopRamp;
  }
};

#endif /* GOSOUNDTREMULANTWAVEFORM_H */
