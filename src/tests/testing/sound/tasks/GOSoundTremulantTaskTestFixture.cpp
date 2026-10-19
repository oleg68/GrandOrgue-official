/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundTremulantTaskTestFixture.h"

#include <cmath>
#include <numbers>

GOTremulant::Description make_description(
  unsigned nLoopFrames,
  float ampDepth,
  float pitchCents,
  unsigned nStartFrames,
  unsigned nStopFrames) {
  GOTremulant::Description desc;
  const double radsPerFrame = 2.0 * std::numbers::pi / nLoopFrames;

  desc.amp.resize(nLoopFrames);
  for (unsigned frameI = 0; frameI < nLoopFrames; ++frameI)
    desc.amp[frameI] = 1.0f + ampDepth * (float)std::sin(radsPerFrame * frameI);
  if (pitchCents != 0.0f) {
    desc.pitchCents.resize(nLoopFrames);
    for (unsigned frameI = 0; frameI < nLoopFrames; ++frameI)
      desc.pitchCents[frameI]
        = pitchCents * (float)std::sin(radsPerFrame * frameI);
  }
  desc.nStartFrames = nStartFrames;
  desc.nStopFrames = nStopFrames;
  return desc;
}

std::unique_ptr<GOSoundTremulantWaveForm> make_wave_form(
  unsigned nLoopFrames,
  float ampDepth,
  float pitchCents,
  unsigned nStartFrames,
  unsigned nStopFrames) {
  return std::make_unique<GOSoundTremulantWaveForm>(
    make_description(
      nLoopFrames, ampDepth, pitchCents, nStartFrames, nStopFrames),
    TASK_TEST_N_FRAMES);
}

void run_round(GOSoundTremulantTask &task) {
  task.NewRound();
  task.EnsureDone();
}

GOSoundTremulantTaskTestFixture::GOSoundTremulantTaskTestFixture(
  float pitchCents,
  unsigned nStartFrames,
  unsigned nStopFrames,
  unsigned nLoopFrames)
  : tremulant(model.organModel),
    task(tremulant, TASK_TEST_N_FRAMES, TASK_TEST_SAMPLE_RATE) {
  InstallWaveForm(make_wave_form(
    nLoopFrames, TASK_TEST_AMP_DEPTH, pitchCents, nStartFrames, nStopFrames));
}

void GOSoundTremulantTaskTestFixture::InstallWaveForm(
  std::unique_ptr<GOSoundTremulantWaveForm> oWaveForm) {
  task.SwapWaveForm(oWaveForm);
}
