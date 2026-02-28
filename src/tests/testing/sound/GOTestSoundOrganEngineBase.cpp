/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundOrganEngineBase.h"

#include <format>

#include "sound/GOSoundOrganEngine.h"

GOSoundOrganEngine &GOTestSoundOrganEngineBase::BuildAndStartEngine(
  unsigned nAudioGroups, unsigned nAuxThreads, unsigned nOutputs) {
  GOSoundOrganEngine &engine = controller->GetSoundEngine();

  engine.SetNAudioGroups(nAudioGroups);
  engine.SetNAuxThreads(nAuxThreads);

  const auto defaultConfigs
    = GOSoundOrganEngine::createDefaultOutputConfigs(nAudioGroups);
  const std::vector<GOSoundOrganEngine::AudioOutputConfig> configs(
    nOutputs, defaultConfigs[0]);

  GOAssert(
    defaultConfigs[0].channels == N_OUTPUT_CHANNELS,
    std::format(
      "Default output config should have {} channels but has {}",
      N_OUTPUT_CHANNELS,
      defaultConfigs[0].channels));

  engine.BuildAndStart(configs, N_SAMPLES_PER_BUFFER, SAMPLE_RATE, m_recorder);

  GOAssert(engine.IsWorking(), "Engine should be WORKING after BuildAndStart");
  GOAssert(!engine.IsUsed(), "Engine should not be USED after BuildAndStart");
  GOAssert(
    !engine.IsStreaming(),
    "Engine should not be STREAMING after BuildAndStart");

  return engine;
}

void GOTestSoundOrganEngineBase::StopAndDestroyEngine() {
  GOSoundOrganEngine &engine = controller->GetSoundEngine();

  engine.StopAndDestroy();

  GOAssert(engine.IsIdle(), "Engine should be IDLE after StopAndDestroy");
}
