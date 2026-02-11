/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDSYSTEM_H
#define GOSOUNDSYSTEM_H

#include <map>
#include <vector>

#include <wx/string.h>

#include "config/GOPortsConfig.h"
#include "midi/GOMidiSystem.h"
#include "ports/GOSoundPortFactory.h"
#include "threading/GOCondition.h"
#include "threading/GOMutex.h"

#include "GOSoundDevInfo.h"
#include "GOSoundOrganEngine.h"
#include "GOSoundRecorder.h"

class GODeviceNamePattern;
class GOOrganController;
class GOMidiSystem;
class GOSoundPort;
class GOSoundRtPort;
class GOSoundPortaudioPort;
class GOConfig;

/**
 * This class represents a GrandOrgue-wide sound system. It may be used even
 * without a loaded organ
 */

class GOSoundSystem {
private:
  class GOSoundOutput {
  public:
    GOSoundPort *port;
    GOMutex mutex;
    GOCondition condition;
    bool wait;
    bool waiting;

    GOSoundOutput() : condition(mutex) {
      port = 0;
      wait = false;
      waiting = false;
    }

    GOSoundOutput(const GOSoundOutput &old) : condition(mutex) {
      port = old.port;
      wait = old.wait;
      waiting = old.waiting;
    }

    const GOSoundOutput &operator=(const GOSoundOutput &old) {
      port = old.port;
      wait = old.wait;
      waiting = old.waiting;
      return *this;
    }
  };

  // Have all output audio devices opened successfully
  bool m_open;
  // Is the sound engine ready yo accept audio callback calls
  std::atomic_bool m_IsRunning;

  // counter of audio callbacks that have been entered but have not yet been
  // exited
  std::atomic_uint m_NCallbacksEntered;

  // For waiting for and notifying when m_NCallbacksEntered becomes 0
  GOMutex m_CallbackMutex;
  GOCondition m_CallbackCondition;

  GOMutex m_lock;

  bool logSoundErrors;

  std::vector<GOSoundOutput> m_AudioOutputs;
  std::atomic_uint m_WaitCount;
  std::atomic_uint m_CalcCount;

  unsigned m_SamplesPerBuffer;

  unsigned meter_counter;

  GOSoundDevInfo m_DefaultAudioDevice;

  GOOrganController *m_OrganController;
  GOSoundRecorder m_AudioRecorder;

  GOSoundOrganEngine m_SoundEngine;

  GOConfig &m_config;

  GOMidiSystem m_midi;

  wxString m_LastErrorMessage;

  void ResetMeters();

  void OpenMidi();

  // Starting step 1.
  // Open output device ports and MIDI and do other initialising with using
  // neither m_SoundEngine nor m_OrganController
  // Sets m_open = true at the end
  void OpenSoundSystem();

  // Starting step 2.
  // Do specific initialising steps for m_SoundEngine using m_OrganController.
  // After this call the organ sound engine is ready to accept audio callbacks.
  // Can only be called when m_OrganController is set and m_open is true
  void PrepareEngine();

  // Starting step 3.
  // After this step output devece callbacks start to be propagated to the organ
  // sound engine. Can only be called when m_open is true
  void ConnectToEngine();

  // Starting step 4.
  // Call callbacks to m_OrganController
  // Can only be called when m_OrganController is set and m_open is true
  void NotifySoundStarted();

  // Finish step 4.
  // Call callbacks to m_OrganController
  // Can only be called when m_OrganController is set and m_open is true
  void NotifySoundStopped();

  // Finish step 3.
  // Wait for all audio callbacks in progress to finish and prevents propagation
  // of future callbacks. Can only be called when m_open is true
  void DisconnectFromEngine();

  // Finish step 2.
  // Cleanup steps with m_SoundEngine. After this call the sound engine is not
  // ready to accept audio callbacks.
  // Can only be called when m_OrganController is set and m_open is true
  void CleanupEngine();

  // Finish step 1.
  // neither m_SoundEngine nor m_OrganController
  // Can only be called when m_open is true. Sets m_open = false at the end
  void CloseSoundSystem();

  void StartStreams();
  void UpdateMeter();

public:
  GOSoundSystem(GOConfig &settings);
  ~GOSoundSystem();

  bool AssureSoundIsOpen();
  void AssureSoundIsClosed();

  wxString getLastErrorMessage() const { return m_LastErrorMessage; }
  wxString getState();

  GOConfig &GetSettings();

  void AssignOrganFile(GOOrganController *organController);
  GOOrganController *GetOrganFile();

  void SetLogSoundErrorMessages(bool settingsDialogVisible);

  std::vector<GOSoundDevInfo> GetAudioDevices(const GOPortsConfig &portsConfig);
  const GOSoundDevInfo &GetDefaultAudioDevice(const GOPortsConfig &portsConfig);

  static void FillDeviceNamePattern(
    const GOSoundDevInfo &deviceInfo, GODeviceNamePattern &pattern);

  GOMidiSystem &GetMidi();

  GOSoundOrganEngine &GetEngine();

  bool AudioCallback(unsigned devIndex, GOSoundBufferMutable &outOutputBuffer);
};

#endif
