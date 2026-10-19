/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDTREMULANTPROVIDERSYNTH_H
#define GOSOUNDTREMULANTPROVIDERSYNTH_H

#include "GOSoundTremulantProvider.h"

/**
 * The synthesized tremulant: a unit sine computed directly at the requested
 * length. The period holds exactly one sine period, so the loop joins itself
 * without resampling. With a = 2 * pi * frameI / nPeriodFrames:
 * amp shape = pitch shape = sin(a).
 * Period of the ODF is always required: a sine has no recorded period.
 */
class GOSoundTremulantProviderSynth : public GOSoundTremulantProvider {
public:
  void RenderOnePeriod(
    unsigned nPeriodFrames,
    std::vector<float> &outAmpShape,
    std::vector<float> &outPitchShape) const override;
};

#endif /* GOSOUNDTREMULANTPROVIDERSYNTH_H */
