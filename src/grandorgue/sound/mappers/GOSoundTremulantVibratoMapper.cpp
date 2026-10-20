/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundTremulantVibratoMapper.h"

#include "sound/effects/GOSoundVibratoProcessor.h"
#include "sound/tasks/GOSoundTremulantTask.h"

GOSoundTremulantVibratoMapper::GOSoundTremulantVibratoMapper(
  const GOSoundTremulantTask &task, GOSoundVibratoProcessor &processor)
  : GOSoundProcessingPrmMapper(processor), r_task(task) {}

void GOSoundTremulantVibratoMapper::EnsureParametersUpToDate() {
  static_cast<GOSoundVibratoProcessor &>(r_processor)
    .SetPitchRateChunk(r_task.GetPitchRateChunk());
}
