/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundTremulantWaveForm.h"

#include <algorithm>
#include <cassert>
#include <cmath>

GOSoundTremulantWaveForm::GOSoundTremulantWaveForm(
  const GOTremulant::Description &desc, unsigned nChunkFrames)
  : m_AmpUnits(desc.amp.size(), nChunkFrames),
    m_StartRamp(true, desc.nStartFrames),
    m_StopRamp(false, desc.nStopFrames) {
  const unsigned nLoopFrames = desc.amp.size();

  assert(nLoopFrames > 0);

  uint16_t *pAmpUnits = m_AmpUnits.GetLoopData();
  const float *pAmp = desc.amp.data();

  for (unsigned frameI = 0; frameI < nLoopFrames; ++frameI, ++pAmpUnits, ++pAmp)
    *pAmpUnits = (uint16_t)std::clamp(
      std::lround(*pAmp * UNIT_AMP_F), 0L, (long)UINT16_MAX);
  m_AmpUnits.FillTail();

  // The pitch part; an empty pitchCents leaves the curve not built
  if (!desc.pitchCents.empty()) {
    assert(desc.pitchCents.size() == nLoopFrames);
    m_PitchRateCurve.Build(desc.pitchCents.data(), nLoopFrames, nChunkFrames);
  }
}
