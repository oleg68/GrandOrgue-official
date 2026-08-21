/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundProcessingChain.h"

#include <cassert>

#include "GOSoundProcessingChainState.h"
#include "GOSoundProcessingPrmMapper.h"
#include "GOSoundProcessor.h"

// Defined here, not inline in the header: even the default constructor
// needs GOSoundProcessor's and GOSoundProcessingPrmMapper's complete
// definitions, to generate the member-destruction code for the case where
// a later member's construction throws; the header intentionally only
// forward-declares them.
GOSoundProcessingChain::GOSoundProcessingChain() = default;
GOSoundProcessingChain::~GOSoundProcessingChain() = default;

void GOSoundProcessingChain::AddProcessor(
  std::unique_ptr<GOSoundProcessor> pProcessor) {
  m_processors.push_back(std::move(pProcessor));
}

void GOSoundProcessingChain::AddMapper(
  std::unique_ptr<GOSoundProcessingPrmMapper> pMapper) {
  m_mappers.push_back(std::move(pMapper));
}

const GOSoundProcessor &GOSoundProcessingChain::GetProcessor(
  unsigned processorI) const {
  assert(processorI < m_processors.size());
  return *m_processors[processorI];
}

void GOSoundProcessingChain::EnsureSetup(
  unsigned nChannels, unsigned nFrames, unsigned sampleRate) {
  for (const std::unique_ptr<GOSoundProcessor> &pProcessor : m_processors)
    pProcessor->EnsureSetup(nChannels, nFrames, sampleRate);
}

void GOSoundProcessingChain::EnsureParametersUpToDate() {
  for (const std::unique_ptr<GOSoundProcessingPrmMapper> &pMapper : m_mappers)
    pMapper->EnsureParametersUpToDate();
}

std::unique_ptr<GOSoundProcessingChainState> GOSoundProcessingChain::
  CreateState() const {
  return std::make_unique<GOSoundProcessingChainState>(*this);
}
