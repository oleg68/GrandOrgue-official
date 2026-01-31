/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDSTATEHANDLER_H
#define GOSOUNDSTATEHANDLER_H

/**
 * This is a basic class for all objects that need to interact with
 * GOSoundOrganEngine.
 * When the sound engine becomes ready, PreparePlayback() is called. When the
 * sound engine goes to be closed, AbortPlayback() is called.
 * The GOSoundOrganEngine instance should be accessed with GOSoundInterfaceProxy
 * implementation in GOOrganModel
 */
class GOSoundStateHandler {
public:
  virtual ~GOSoundStateHandler() = default;

  // Derived classes should override
  virtual void PreparePlayback() = 0;
  virtual void StartPlayback() {}
  virtual void AbortPlayback() {}
  virtual void PrepareRecording() {}
};

#endif
