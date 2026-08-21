/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundProcessingChain.h"

#include <cstring>
#include <format>
#include <vector>

#include "GOSoundProcessingTestImpls.h"
#include "sound/buffer/GOSoundBufferPlanarManaged.h"
#include "sound/buffer/GOSoundBufferPlanarMutable.h"
#include "sound/processing/GOSoundProcessingChain.h"
#include "sound/processing/GOSoundProcessingChainState.h"

const std::string GOTestSoundProcessingChain::TEST_NAME
  = "GOTestSoundProcessingChain";

void GOTestSoundProcessingChain::TestEmptyChainLeavesBufferUntouched() {
  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 2, 8);

  fillWithSequential(buffer, 1.0f);

  GOSoundBufferPlanarManaged snapshot(buffer);
  GOSoundProcessingChain chain;

  GOAssert(chain.IsEmpty(), "a chain with no processors should be empty");

  std::unique_ptr<GOSoundProcessingChainState> pState = chain.CreateState();

  GOAssert(
    pState->GetNStates() == 0,
    "an empty chain's state should have no processor states");

  pState->Reset(); // must not crash on an empty state
  pState->Process(buffer);

  GOAssert(
    std::memcmp(buffer.GetData(), snapshot.GetData(), buffer.GetNBytes()) == 0,
    "an empty chain must leave the buffer untouched");
}

void GOTestSoundProcessingChain::TestProcessorsRunInOrder() {
  std::vector<int> log;
  GOSoundProcessingChain loggingChain;

  loggingChain.AddProcessor(
    std::make_unique<GOAddConstProcessor>(0.0f, &log, 1));
  loggingChain.AddProcessor(
    std::make_unique<GOAddConstProcessor>(0.0f, &log, 2));
  loggingChain.AddProcessor(
    std::make_unique<GOAddConstProcessor>(0.0f, &log, 3));

  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(logBuffer, 1, 1);

  logBuffer.FillWithSilence();

  std::unique_ptr<GOSoundProcessingChainState> pLoggingState
    = loggingChain.CreateState();

  pLoggingState->Process(logBuffer);

  GOAssert(
    log.size() == 3 && log[0] == 1 && log[1] == 2 && log[2] == 3,
    "processors should run in the order they were added");

  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(arithBuffer, 1, 1);

  arithBuffer.FillWithSilence();

  GOSoundProcessingChain addThenScale;

  addThenScale.AddProcessor(std::make_unique<GOAddConstProcessor>(1.0f));
  addThenScale.AddProcessor(std::make_unique<GOScaleProcessor>(2.0f));
  addThenScale.CreateState()->Process(arithBuffer);

  GOAssert(
    arithBuffer.GetData()[0] == 2.0f,
    std::format(
      "add(+1)-then-scale(x2) on 0 should give 2.0 (got: {})",
      arithBuffer.GetData()[0]));

  arithBuffer.FillWithSilence();

  GOSoundProcessingChain scaleThenAdd;

  scaleThenAdd.AddProcessor(std::make_unique<GOScaleProcessor>(2.0f));
  scaleThenAdd.AddProcessor(std::make_unique<GOAddConstProcessor>(1.0f));
  scaleThenAdd.CreateState()->Process(arithBuffer);

  GOAssert(
    arithBuffer.GetData()[0] == 1.0f,
    std::format(
      "scale(x2)-then-add(+1) on 0 should give 1.0 (got: {})",
      arithBuffer.GetData()[0]));
}

void GOTestSoundProcessingChain::TestEnsureSetupReachesEveryProcessor() {
  GOSoundProcessingChain chain;
  GOAddConstProcessor *pProcessors[3];

  for (int processorI = 0; processorI < 3; processorI++) {
    std::unique_ptr<GOAddConstProcessor> pProcessor
      = std::make_unique<GOAddConstProcessor>(0.0f);

    pProcessors[processorI] = pProcessor.get();
    chain.AddProcessor(std::move(pProcessor));
  }

  chain.EnsureSetup(2, 64, 44100);

  for (int processorI = 0; processorI < 3; processorI++) {
    GOAddConstProcessor *pProcessor = pProcessors[processorI];

    GOAssert(
      pProcessor->GetNSetups() == 1,
      std::format(
        "processor {} should have received exactly one EnsureSetup() call",
        processorI));
    GOAssert(
      pProcessor->GetLastNChannels() == 2 && pProcessor->GetLastNFrames() == 64
        && pProcessor->GetLastSampleRate() == 44100,
      std::format(
        "processor {} should have received the exact EnsureSetup() arguments",
        processorI));
  }
}

void GOTestSoundProcessingChain::TestResetReachesEveryState() {
  GOSoundProcessingChain chain;

  for (int processorI = 0; processorI < 3; processorI++)
    chain.AddProcessor(std::make_unique<GOAddConstProcessor>(0.0f));

  std::unique_ptr<GOSoundProcessingChainState> pState = chain.CreateState();

  pState->Reset();

  // Being a friend of GOSoundProcessingChainState, inspect each state
  // directly: the public interface alone (GetNStates()) cannot confirm
  // Reset() individually reached every one of them.
  for (const std::unique_ptr<GOSoundProcessorState> &pRawState :
       pState->m_states) {
    GOCountingProcessorState *pCountingState
      = dynamic_cast<GOCountingProcessorState *>(pRawState.get());

    GOAssert(
      pCountingState && pCountingState->GetNResets() == 1,
      "each processor state should have received exactly one Reset() call");
  }
}

void GOTestSoundProcessingChain::
  TestEnsureParametersUpToDateReachesEveryMapper() {
  GOSoundProcessingChain chain;
  std::unique_ptr<GOAddConstProcessor> pTargetProcessorOwner
    = std::make_unique<GOAddConstProcessor>(1.0f);
  GOAddConstProcessor *pTargetProcessor = pTargetProcessorOwner.get();

  chain.AddProcessor(std::move(pTargetProcessorOwner));
  chain.AddMapper(std::make_unique<GOCountingMapper>());
  chain.AddMapper(std::make_unique<GOCountingMapper>(pTargetProcessor, 5.0f));

  GOAssert(
    chain.m_mappers.size() == 2,
    "chain.m_mappers should have exactly the 2 added mappers");

  chain.EnsureParametersUpToDate();

  for (const std::unique_ptr<GOSoundProcessingPrmMapper> &pRawMapper :
       chain.m_mappers) {
    GOCountingMapper *pCountingMapper
      = dynamic_cast<GOCountingMapper *>(pRawMapper.get());

    GOAssert(
      pCountingMapper && pCountingMapper->GetNCalls() == 1,
      "each mapper should have received exactly one EnsureParametersUpToDate() "
      "call");
  }

  GO_DECLARE_LOCAL_SOUND_BUFFER_PLANAR(buffer, 1, 1);

  buffer.FillWithSilence();
  chain.CreateState()->Process(buffer);

  GOAssert(
    buffer.GetData()[0] == 5.0f,
    std::format(
      "the mapper's new constant should have taken effect (got: {})",
      buffer.GetData()[0]));
}

void GOTestSoundProcessingChain::run() {
  TestEmptyChainLeavesBufferUntouched();
  TestProcessorsRunInOrder();
  TestEnsureSetupReachesEveryProcessor();
  TestResetReachesEveryState();
  TestEnsureParametersUpToDateReachesEveryMapper();
}
