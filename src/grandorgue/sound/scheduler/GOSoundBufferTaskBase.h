/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDBUFFERTASKBASE_H
#define GOSOUNDBUFFERTASKBASE_H

#include "sound/buffer/GOSoundBufferManaged.h"

class GOSoundThread;

class GOSoundBufferTaskBase : public GOSoundBufferManaged {
public:
  GOSoundBufferTaskBase(unsigned nChannels, unsigned nSamples)
    : GOSoundBufferManaged(nChannels, nSamples) {}

  virtual void Finish(bool stop, GOSoundThread *pThread = nullptr) = 0;
};

#endif /* GOSOUNDBUFFERTASKBASE_H */
