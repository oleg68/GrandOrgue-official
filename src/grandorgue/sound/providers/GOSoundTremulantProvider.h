/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDTREMULANTPROVIDER_H
#define GOSOUNDTREMULANTPROVIDER_H

#include <vector>

/**
 * A source of one period of the normalised shape of a tremulant: the amplitude
 * shape and the pitch shape at the same frame indexes. The full scale of a
 * shape is 1.0, so the caller turns a shape into a relative deviation around
 * zero by multiplying it by a depth; the nominal level and frequency are zero
 * deviation.
 *
 * Owned by GOTremulant and called only from the control thread while a wave
 * form is being built; the audio thread never sees a provider, it reads the
 * rendered loop. Implementations hold no mutable state, so RenderOnePeriod()
 * may run concurrently.
 */
class GOSoundTremulantProvider {
public:
  virtual ~GOSoundTremulantProvider() = default;

  /**
   * Renders exactly one period of the normalised shapes, in their natural
   * phase relation (the user's phase shift is applied by the caller).
   *
   * @param nPeriodFrames the period length in frames, > 0; the caller rounds
   *   the period to whole frames
   * @param outAmpShape the amplitude shape per frame, with the full scale 1.0;
   *   resized to nPeriodFrames, or emptied if the shape has no amplitude
   *   channel
   * @param outPitchShape the pitch shape per frame, with the full scale 1.0;
   *   resized to nPeriodFrames, or emptied if the shape has no pitch channel
   */
  virtual void RenderOnePeriod(
    unsigned nPeriodFrames,
    std::vector<float> &outAmpShape,
    std::vector<float> &outPitchShape) const = 0;
};

#endif /* GOSOUNDTREMULANTPROVIDER_H */
