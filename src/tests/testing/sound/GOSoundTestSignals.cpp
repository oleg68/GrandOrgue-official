/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundTestSignals.h"

#include <cmath>

std::vector<float> GOSoundTestSignals::makeSineCents(
  unsigned nFrames, float amplitudeCents, float periodFrames) {
  std::vector<float> cents(nFrames);

  for (unsigned frameI = 0; frameI < nFrames; ++frameI)
    cents[frameI] = amplitudeCents
      * std::sin(2.0f * (float)M_PI * (float)frameI / periodFrames);
  return cents;
}
