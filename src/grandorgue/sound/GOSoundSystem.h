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
public:
  class ClosingListener {
  public:
    // This callback is called from AssureSoundClosed. The callback adapter must
    // call DisconnectFromEngine, otherwise an exception occured
    virtual void OnSoundClosing(GOSoundSystem &soundSystem) = 0;
  };

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
  unsigned m_SampleRate;

  unsigned meter_counter;

  GOSoundDevInfo m_DefaultAudioDevice;

  GOOrganController *m_OrganController;
  GOSoundRecorder m_AudioRecorder;

  GOSoundOrganEngine m_SoundEngine;

  GOConfig &m_config;

  GOMidiSystem m_midi;

  wxString m_LastErrorMessage;

  ClosingListener *p_ClosingListener;

  bool IsEngineConnected() const { return m_IsRunning.load(); }

  void ResetMeters();

  void OpenMidi();

  // Starting step 1.
  // Open output device ports and MIDI and do other initialising with using
  // neither m_SoundEngine nor m_OrganController
  // Sets m_open = true at the end
  void OpenSoundSystem();

  // Finish step 1.
  // neither m_SoundEngine nor m_OrganController
  // Can only be called when m_open is true. Sets m_open = false at the end
  void CloseSoundSystem();

  void StartStreams();
  void UpdateMeter();

public:
  GOSoundSystem(GOConfig &settings);
  ~GOSoundSystem();

  wxString getLastErrorMessage() const { return m_LastErrorMessage; }
  wxString getState();

  GOConfig &GetSettings();

  unsigned GetSamplesPerBuffer() const { return m_SamplesPerBuffer; }
  unsigned GetSampleRate() const { return m_SampleRate; }
  GOSoundRecorder &GetAudioRecorder() { return m_AudioRecorder; }

  void SetLogSoundErrorMessages(bool settingsDialogVisible);

  GOMidiSystem &GetMidi();

  GOSoundOrganEngine &GetEngine();

  ClosingListener *GetClosingListener() const { return p_ClosingListener; }
  void SetClosingListener(ClosingListener *pListener) {
    p_ClosingListener = pListener;
  }

  std::vector<GOSoundDevInfo> GetAudioDevices(const GOPortsConfig &portsConfig);
  const GOSoundDevInfo &GetDefaultAudioDevice(const GOPortsConfig &portsConfig);

  static void FillDeviceNamePattern(
    const GOSoundDevInfo &deviceInfo, GODeviceNamePattern &pattern);

  bool AssureSoundIsOpen();
  void AssureSoundIsClosed();

  void AssignOrganFile(GOOrganController *organController);

  // Starting step 3.
  // After this step output device callbacks start to be propagated to the organ
  // sound engine. Can only be called when m_open is true
  void ConnectToEngine();

  // Finish step 3.
  // Wait for all audio callbacks in progress to finish and prevents propagation
  // of future callbacks. Can only be called when m_open is true
  void DisconnectFromEngine();

  bool AudioCallback(unsigned devIndex, GOSoundBufferMutable &outOutputBuffer);
};

#endif
