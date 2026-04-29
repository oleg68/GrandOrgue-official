/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDSYSTEM_H
#define GOSOUNDSYSTEM_H

#include <vector>

#include <wx/string.h>

#include "midi/GOMidiSystem.h"
#include "threading/GOCondition.h"
#include "threading/GOMutex.h"

#include "ptrvector.h"

#include "GOSoundDevInfo.h"
#include "GOSoundOrganEngine.h"
#include "GOSoundRecorder.h"

class GOConfig;
class GODeviceNamePattern;
class GOOrganController;
class GOPortsConfig;
class GOSoundBufferMutable;
class GOSoundPort;
class GOSoundThread;

/**
 * This class represents a GrandOrgue-wide sound system. It may be used even
 * without a loaded organ
 */

class GOSoundSystem {
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

private:
  GOConfig &m_config;

  GOMidiSystem m_midi;
  GOSoundRecorder m_AudioRecorder;
  GOSoundOrganEngine m_SoundEngine;
  ptr_vector<GOSoundThread> m_Threads;

  bool m_open;
  bool logSoundErrors;
  unsigned m_SamplesPerBuffer;
  std::vector<GOSoundOutput> m_AudioOutputs;

  wxString m_LastErrorMessage;

  GOOrganController *m_OrganController;

  GOSoundDevInfo m_DefaultAudioDevice;

  std::atomic_bool m_IsRunning;

  // counter of audio callbacks that have been entered but have not yet been
  // exited
  std::atomic_uint m_NCallbacksEntered;

  // For waiting for and notifying when m_NCallbacksEntered bacomes 0
  GOMutex m_CallbackMutex;
  GOCondition m_CallbackCondition;

  GOMutex m_lock;
  GOMutex m_thread_lock;

  unsigned meter_counter;

  std::atomic_uint m_WaitCount;
  std::atomic_uint m_CalcCount;

  void StopThreads();
  void StartThreads();

  void ResetMeters();
  void UpdateMeter();

  void OpenMidi();

  void OpenSound();
  void CloseSound();

  void StartStreams();
  void StartSoundSystem();

public:
  static void FillDeviceNamePattern(
    const GOSoundDevInfo &deviceInfo, GODeviceNamePattern &pattern);

  GOSoundSystem(GOConfig &settings);
  ~GOSoundSystem();

  GOConfig &GetSettings() { return m_config; }
  GOMidiSystem &GetMidi() { return m_midi; }
  GOSoundOrganEngine &GetEngine() { return m_SoundEngine; }

  std::vector<GOSoundDevInfo> GetAudioDevices(const GOPortsConfig &portsConfig);
  const GOSoundDevInfo &GetDefaultAudioDevice(const GOPortsConfig &portsConfig);
  wxString getLastErrorMessage() const { return m_LastErrorMessage; }
  GOOrganController *GetOrganFile() { return m_OrganController; }
  wxString getState();

  void SetLogSoundErrorMessages(bool isVisible) { logSoundErrors = isVisible; }

  bool AssureSoundIsOpen();
  void AssureSoundIsClosed();
  void AssignOrganFile(GOOrganController *organController);

  bool AudioCallback(unsigned devIndex, GOSoundBufferMutable &outBuffer);
};

#endif
