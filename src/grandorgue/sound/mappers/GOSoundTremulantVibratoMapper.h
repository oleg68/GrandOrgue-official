/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDTREMULANTVIBRATOMAPPER_H
#define GOSOUNDTREMULANTVIBRATOMAPPER_H

#include "sound/processing/GOSoundProcessingPrmMapper.h"

class GOSoundTremulantTask;
class GOSoundVibratoProcessor;

/**
 * Drives one GOSoundVibratoProcessor from one GOSoundTremulantTask: once per
 * round it passes the task's pitch rate chunk to the processor (issue #709).
 * Lives in sound/mappers/ (not sound/processing/) because it depends on
 * tasks/; the model is not touched at all - the task already took its values
 * from the tremulant.
 *
 * Call order contract: EnsureParametersUpToDate() must be called after the
 * task's DoRun() of the same round and before the chain's Process() calls;
 * GOSoundWindchestTask::DoRun() guarantees this. The chunk's rates stay valid
 * until the task's next DoRun(), i.e. for all the Process() calls of the
 * round.
 *
 * After a wave form swap (GOSoundOrganEngine::CommitTremulantWaveForms()) the
 * chunk pointer left in the processor points into the replaced form and is
 * stale until the next EnsureParametersUpToDate(). It is never dereferenced:
 * the chain's Process() runs only after that call in every round.
 */
class GOSoundTremulantVibratoMapper : public GOSoundProcessingPrmMapper {
private:
  // The task whose chunk is passed on; must outlive this mapper (the engine
  // destroys the windchest tasks, with their chains, before the tremulant
  // tasks).
  const GOSoundTremulantTask &r_task;

public:
  /**
   * @param task the source of the pitch rate chunk
   * @param processor the target; must outlive this mapper
   */
  GOSoundTremulantVibratoMapper(
    const GOSoundTremulantTask &task, GOSoundVibratoProcessor &processor);

  /**
   * Passes r_task.GetPitchRateChunk() to the processor's SetPitchRateChunk().
   * There is no last-value cache: the pointer moves every round and the call
   * is a copy of three fields. Never touches GOSoundProcessorState. Asserts
   * that the task is done (through the getter).
   */
  void EnsureParametersUpToDate() override;
};

#endif /* GOSOUNDTREMULANTVIBRATOMAPPER_H */
