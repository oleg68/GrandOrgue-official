/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundWindchestGroupTask.h"

#include <cassert>

#include "scheduler/GOSchedulerThread.h"
#include "sound/GOSoundDefs.h"
#include "sound/playing/GOSoundSamplerPlayer.h"
#include "sound/processing/GOSoundProcessingChain.h"
#include "sound/processing/GOSoundProcessingChainState.h"
#include "threading/GOMutexLocker.h"

#include "GOSoundWindchestTask.h"

GOSoundWindchestGroupTask::GOSoundWindchestGroupTask(
  GOSoundSamplerPlayer &samplerPlayer,
  GOSoundWindchestTask &windchestTask,
  unsigned nFramesPerBuffer)
  : GOSoundBufferTaskBase(
    PRIORITY_WINDCHESTMIX, true, MAX_OUTPUT_CHANNELS, nFramesPerBuffer),
    r_SamplerPlayer(samplerPlayer),
    r_WindchestTask(windchestTask),
    m_Condition(m_mutex),
    mp_ChainState(windchestTask.GetChain().CreateState()),
    m_ActiveCount(0) {}

// The destructor body is empty (= default), but it must be defined here (not
// in the header) so that std::unique_ptr can call the complete destructor of
// its managed type (GOSoundProcessingChainState), which is only
// forward-declared in the header file.
GOSoundWindchestGroupTask::~GOSoundWindchestGroupTask() = default;

void GOSoundWindchestGroupTask::OnMixed() {
  // Lazy prerequisite: a round with zero samplers never otherwise triggers
  // r_WindchestTask to run (nothing calls its GetAmplitude()), but a
  // stateful processor in the chain (e.g. a reverb tail) still needs
  // current parameters and must still run. GetAmplitude() is already the
  // established way to force that windchest task to completion, and is
  // already called concurrently from here (via ProcessList()) and from
  // other audio groups of the same windchest, so this adds no new
  // concurrency concern.
  r_WindchestTask.GetAmplitude();
  mp_ChainState->Process(*this);
}

void GOSoundWindchestGroupTask::PublishDone() {
  // The caller must already hold m_mutex - GetLockerInfo() is non-null only
  // between a successful Lock and its matching Unlock, so this catches a
  // caller that forgot to acquire it (it cannot catch a caller holding some
  // *other* thread's lock instead, but every call site in this file locks
  // m_mutex itself immediately beforehand).
  assert(m_mutex.GetLockerInfo());
  m_RunState.store(RUN_STATE_DONE);
  m_Condition.Broadcast();
}

void GOSoundWindchestGroupTask::DiscardContent() {
  m_Active.Clear();
  m_Release.Clear();
  mp_ChainState->Reset();
}

void GOSoundWindchestGroupTask::Add(GOSoundSampler *sampler) {
  if (sampler->is_release)
    m_Release.Put(sampler);
  else
    m_Active.Put(sampler);
}

void GOSoundWindchestGroupTask::ProcessList(
  GOSoundSamplerList &list, bool isToDropOld, GOSoundBufferMutable &outBuffer) {
  GOSoundSampler *sampler;

  while ((sampler = list.Get())) {
    if (
      isToDropOld && m_IsToComplete.load()
      && sampler->time + 2000 < r_SamplerPlayer.GetTime()) {
      if (sampler->drop_counter++ > 3) {
        r_SamplerPlayer.ReturnSampler(sampler);
        continue;
      }
    }
    sampler->drop_counter = 0;

    GOSoundWindchestTask *const windchest = sampler->p_WindchestTask;

    if (
      windchest
      && r_SamplerPlayer.ProcessSampler(
        *sampler, windchest->GetAmplitude(), outBuffer))
      Add(sampler);
  }
}

unsigned GOSoundWindchestGroupTask::GetCost() const {
  return m_Active.GetCount() + m_Release.GetCount();
}

void GOSoundWindchestGroupTask::Run(GOSchedulerThread *pThread) {
  if (m_RunState.load() < RUN_STATE_DONE) {
    bool isParticipating = false;

    {
      GOMutexLocker locker(
        m_mutex,
        false,
        "GOSoundWindchestGroupTask::Run.beforeProcess",
        pThread);

      if (locker.IsLocked()) {
        if (m_RunState.load() == RUN_STATE_NOT_STARTED) {
          // the first thread entered Run() claims the round
          m_Active.Move();
          m_Release.Move();
          m_RunState.store(RUN_STATE_IN_PROGRESS);
          isParticipating = true;
        } else if (m_Active.Peek() || m_Release.Peek())
          isParticipating = true;

        if (isParticipating)
          m_ActiveCount.fetch_add(1);
      }
    }

    if (isParticipating) {
      // several threads may process the same list in parallel helping each
      // other; at first, they fill their's own buffer instances
      GO_DECLARE_LOCAL_SOUND_BUFFER(
        localBuffer, MAX_OUTPUT_CHANNELS, GetNFrames())

      localBuffer.FillWithSilence();
      ProcessList(m_Active, false, localBuffer);
      ProcessList(m_Release, true, localBuffer);

      GOMutexLocker locker(
        m_mutex, false, "GOSoundWindchestGroupTask::Run.afterProcess", pThread);

      if (locker.IsLocked()) {
        if (m_RunState.load() == RUN_STATE_IN_PROGRESS) {
          // The first thread is finished. Assign the result to the common
          // buffer
          DeinterleaveFrom(localBuffer);
          m_RunState.store(RUN_STATE_PARTLY_DONE);
        } else
          // not the first thread. Add the result to the common buffer
          AddDeinterleavedFrom(localBuffer);
      }
      if (m_ActiveCount.fetch_sub(1) <= 1) {
        // Exactly one thread ever observes <= 1 here (fetch_sub is atomic
        // and started from a known participant count), so running the chain
        // itself is not a race regardless of whether locker above actually
        // holds m_mutex.
        OnMixed();

        // PublishDone() must run under m_mutex: a waiter in
        // WaitAndDiscardContent() uses WaitOrStop(..., NULL) - no timeout -
        // so a Broadcast() it misses while re-checking m_RunState under the
        // same mutex is a lost wakeup it never recovers from, and (unlike
        // GOSoundTaskBase::Run(), which just skips its unlocked store and
        // relies on a later retry) nothing will ever retry this one:
        // m_ActiveCount has already reached 0. locker above is unlocked
        // here only on the documented pThread->ShouldStop() case, so
        // re-acquiring blocking (no pThread, guaranteed to lock) cannot
        // self-deadlock.
        if (locker.IsLocked())
          PublishDone();
        else {
          GOMutexLocker doneLocker(
            m_mutex, false, "GOSoundWindchestGroupTask::Run.publishDone");

          PublishDone();
        }
      }
    }
  }
}

void GOSoundWindchestGroupTask::EnsureBufferReady(
  bool isToComplete, GOSchedulerThread *pThread) {
  if (isToComplete)
    m_IsToComplete.store(true);
  Run(pThread);
  if (m_RunState.load() < RUN_STATE_DONE) {
    GOMutexLocker locker(
      m_mutex, false, "GOSoundWindchestGroupTask::EnsureBufferReady", pThread);

    while (locker.IsLocked() && m_RunState.load() < RUN_STATE_DONE
           && (pThread == nullptr || !pThread->ShouldStop()))
      m_Condition.WaitOrStop(
        "GOSoundWindchestGroupTask::EnsureBufferReady", pThread);
  }
}

void GOSoundWindchestGroupTask::CompleteRound() {
  // precisely the round deadline: pass it down, finish the round, and do not
  // return until it is finished
  EnsureBufferReady(true);
}

// Called under m_mutex from GOSoundTaskBase::NewRound(), before the round
// state is reset, so the assertions still see the round that is ending. This
// task mixes in ProcessList() outside m_mutex, so the mutex alone cannot keep
// a worker out of a round being reset: the protocol must already have brought
// the task to rest. Either it never ran this period (RUN_STATE_NOT_STARTED),
// or CompleteRound() ran it to completion (RUN_STATE_DONE).
void GOSoundWindchestGroupTask::DoNewRound() {
  assert(m_ActiveCount.load() == 0);
  assert(
    m_RunState.load() == RUN_STATE_NOT_STARTED
    || m_RunState.load() == RUN_STATE_DONE);
  // not redundant with the assertion above: assert() is compiled out under
  // NDEBUG, and resetting the counter is what this hook exists to do
  m_ActiveCount.store(0);
}

// Read without m_mutex, like every other IsEmpty(): called only while the
// task is quiescent (deregistered from the scheduler), never concurrently
// with Run()
bool GOSoundWindchestGroupTask::IsEmpty() const {
  return m_Active.IsEmpty() && m_Release.IsEmpty();
}

void GOSoundWindchestGroupTask::WaitAndDiscardContent() {
  GOMutexLocker locker(m_mutex, false, "WaitAndDiscardContent");

  // wait for no threads are inside Run()
  while (m_RunState.load() > RUN_STATE_NOT_STARTED
         && m_RunState.load() < RUN_STATE_DONE)
    m_Condition.WaitOrStop("WaitAndDiscardContent", NULL);

  // Now it is safe to clear because m_mutex is locked and no other threads
  // can enter in Run()
  DiscardContent();
}
