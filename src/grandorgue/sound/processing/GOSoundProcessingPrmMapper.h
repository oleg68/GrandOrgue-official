/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDPROCESSINGPRMMAPPER_H
#define GOSOUNDPROCESSINGPRMMAPPER_H

/**
 * Copies values owned by the GUI/MIDI thread (organ model, config) into the
 * parameters of one GOSoundProcessor, once per round, outside Process().
 * This directory (sound/processing/) must not depend on model/: concrete,
 * model-aware mappers live in a future sound/mappers/ directory.
 */
class GOSoundProcessingPrmMapper {
public:
  virtual ~GOSoundProcessingPrmMapper() = default;

  /**
   * Re-reads the source values and writes them into the target processor's
   * parameters. Must not touch any GOSoundProcessorState, and must not be
   * called concurrently with Process() on the same processor.
   */
  virtual void EnsureParametersUpToDate() = 0;
};

#endif /* GOSOUNDPROCESSINGPRMMAPPER_H */
