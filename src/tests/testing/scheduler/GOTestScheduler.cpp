/*
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestScheduler.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "scheduler/GOScheduler.h"
#include "scheduler/GOSchedulerTask.h"
#include "scheduler/GOSchedulerThread.h"

#include "GOTestScope.h"

const std::string GOTestScheduler::TEST_NAME = "GOTestScheduler";

namespace {

// A minimal GOSchedulerTask double: IsEmpty() reflects hasContent directly,
// and DiscardContent() clears it and counts its own calls, which is all
// GOScheduler's deregistration contract (Clear()/Remove() call
// DiscardContent(); Add() asserts IsEmpty()) needs to exercise.
class FakeTask : public GOSchedulerTask {
public:
  bool hasContent = false;
  unsigned nDiscardContentCalls = 0;

  unsigned GetPriority() const override { return 0; }
  unsigned GetCost() const override { return 0; }
  bool IsRepeatable() const override { return false; }
  bool IsEmpty() const override { return !hasContent; }
  void Run(GOSchedulerThread * = nullptr) override {}
  void CompleteRound() override {}
  void NewRound() override {}
  void DiscardContent() override {
    hasContent = false;
    nDiscardContentCalls++;
  }
};

// Signals wasRun once actually Run() by a scheduler thread, so a test can
// prove a task was picked up on its own - not driven by anything else -
// within a bounded wait.
class SignalingTask : public GOSchedulerTask {
public:
  std::atomic_bool wasRun{false};

  unsigned GetPriority() const override { return 0; }
  unsigned GetCost() const override { return 0; }
  bool IsRepeatable() const override { return false; }
  bool IsEmpty() const override { return true; }
  void Run(GOSchedulerThread * = nullptr) override { wasRun.store(true); }
  void CompleteRound() override {}
  void NewRound() override {}
  void DiscardContent() override {}
};

} // namespace

void GOTestScheduler::TestClearDiscardsContentOnEveryTask() {
  FakeTask taskA;
  FakeTask taskB;
  GOScheduler scheduler;

  scheduler.Add(&taskA);
  scheduler.Add(&taskB);
  taskA.hasContent = true;
  taskB.hasContent = true;

  scheduler.Clear();

  GOAssert(
    taskA.nDiscardContentCalls == 1 && taskB.nDiscardContentCalls == 1,
    "Clear() must call DiscardContent() exactly once on every registered "
    "task");
  GOAssert(
    taskA.IsEmpty() && taskB.IsEmpty(),
    "after Clear(), every task must be IsEmpty()");
}

void GOTestScheduler::TestRemoveDiscardsContent() {
  FakeTask task;
  GOScheduler scheduler;

  scheduler.Add(&task);
  task.hasContent = true;

  scheduler.Remove(&task);

  GOAssert(
    task.nDiscardContentCalls == 1,
    "Remove() must call DiscardContent() on the removed task, "
    "symmetrically with Add()'s IsEmpty() assertion");
  GOAssert(
    task.IsEmpty(), "after Remove(), the task must be safe to Add() again");
}

void GOTestScheduler::TestRemoveNullptrIsNoop() {
  GOScheduler scheduler;

  scheduler.Remove(nullptr);
}

void GOTestScheduler::TestRemoveTwiceDiscardsContentOnlyOnce() {
  FakeTask task;
  GOScheduler scheduler;

  scheduler.Add(&task);
  task.hasContent = true;
  scheduler.Remove(&task);

  GOAssert(
    task.nDiscardContentCalls == 1,
    "sanity check: the first Remove() must discard the task's content");

  // Not a real scenario (a deregistered task should not accumulate content
  // on its own) - set directly to prove the second Remove() below leaves it
  // untouched, rather than merely happening to already be empty.
  task.hasContent = true;
  scheduler.Remove(&task);

  GOAssert(
    task.nDiscardContentCalls == 1,
    "removing a task a second time must not call DiscardContent() again - "
    "RemoveList() found nothing to remove the second time");
  GOAssert(
    task.hasContent,
    "a Remove() that found nothing must leave the task's content alone");
}

void GOTestScheduler::TestRemoveThenAddSameTaskSucceeds() {
  FakeTask task;
  GOScheduler scheduler;

  scheduler.Add(&task);
  task.hasContent = true;
  scheduler.Remove(&task);

  // Regression test: Remove() leaves a null placeholder in the work list
  // (compacted only by the next Clear()), and this Add() calls Update(),
  // which used to dereference that placeholder unconditionally while
  // grouping same-priority tasks - crashing the whole test process rather
  // than just failing an assertion.
  scheduler.Add(&task);

  GOAssert(
    scheduler.GetNextTask() == &task,
    "the re-added task must be scheduled normally");
}

void GOTestScheduler::TestResumeThenWakeupRunsIdleThreadsWork() {
  SignalingTask task;
  GOScheduler scheduler;
  GOSchedulerThread thread(&scheduler);

  thread.Run();
  thread.WaitForIdle(); // parked - nothing registered yet

  scheduler.Add(&task);

  /* Mirrors GOSoundOrganEngine::StopEngine() then the fixed StartEngine():
     pause, wait for the thread to park, then make a fresh round available
     and explicitly wake it. Regression coverage for the "wake aux workers on
     resume" fix: without the final Wakeup(), the thread would stay parked on
     its condition variable forever - GetNextTask() returning real work is
     not, by itself, enough to bring an idle thread back. */
  scheduler.PauseGivingWork();
  thread.WaitForIdle();

  scheduler.NewRound();
  scheduler.ResumeGivingWork();
  thread.Wakeup();

  const auto deadline
    = std::chrono::steady_clock::now() + std::chrono::seconds(1);

  while (!task.wasRun.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();

  thread.Delete();

  GOAssert(
    task.wasRun.load(),
    "an idle scheduler thread must be woken and run newly available work "
    "after PauseGivingWork()/WaitForIdle() followed by "
    "NewRound()/ResumeGivingWork()/Wakeup() - the same sequence "
    "StopEngine()/StartEngine() perform");
}

void GOTestScheduler::run() {
  GO_RUN_TEST(TestClearDiscardsContentOnEveryTask())
  GO_RUN_TEST(TestRemoveDiscardsContent())
  GO_RUN_TEST(TestRemoveNullptrIsNoop())
  GO_RUN_TEST(TestRemoveTwiceDiscardsContentOnlyOnce())
  GO_RUN_TEST(TestRemoveThenAddSameTaskSucceeds())
  GO_RUN_TEST(TestResumeThenWakeupRunsIdleThreadsWork())
}
