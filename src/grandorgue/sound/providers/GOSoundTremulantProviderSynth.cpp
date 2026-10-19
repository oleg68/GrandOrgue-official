/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundTremulantProviderSynth.h"

#include <cassert>
#include <cmath>
#include <numbers>

void GOSoundTremulantProviderSynth::RenderOnePeriod(
  unsigned nPeriodFrames,
  std::vector<float> &outAmpShape,
  std::vector<float> &outPitchShape) const {
  assert(nPeriodFrames > 0);

  // The phase change per frame, in radians: one period per call
  const double radsPerFrame = 2.0 * std::numbers::pi / nPeriodFrames;

  outAmpShape.resize(nPeriodFrames);
  outPitchShape.resize(nPeriodFrames);

  float *pAmp = outAmpShape.data();
  float *pPitch = outPitchShape.data();

  for (unsigned frameI = 0; frameI < nPeriodFrames;
       ++frameI, ++pAmp, ++pPitch) {
    *pAmp = (float)std::sin(radsPerFrame * frameI);
    *pPitch = *pAmp;
  }
}
