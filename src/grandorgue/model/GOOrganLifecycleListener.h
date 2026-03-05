/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOORGANLIFECYCLELISTENER_H
#define GOORGANLIFECYCLELISTENER_H

/**
 * Interface for objects that need to respond to organ lifecycle events.
 * PreparePlayback() is called when the sound engine becomes ready.
 * AbortPlayback() is called when the sound engine is about to close.
 */
class GOOrganLifecycleListener {
public:
  virtual ~GOOrganLifecycleListener() = default;

  virtual void PreparePlayback() = 0;
  virtual void StartPlayback() {}
  virtual void AbortPlayback() {}
  virtual void PrepareRecording() {}
};

#endif /* GOORGANLIFECYCLELISTENER_H */
