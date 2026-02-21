/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDSYSTEM_H
#define GOSOUNDSYSTEM_H

#include <atomic>
#include <memory>
#include <vector>

#include <wx/string.h>

#include "config/GOPortsConfig.h"
#include "midi/GOMidiSystem.h"
#include "threading/GOCondition.h"
#include "threading/GOMutex.h"

#include "GOSoundDevInfo.h"
#include "GOSoundRecorder.h"

class GOConfig;
class GODeviceNamePattern;
class GOSoundBufferMutable;
class GOSoundOrganEngine;
class GOSoundPort;

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
  // Have all output audio devices opened successfully
  bool m_open;
  // Non-null when the sound engine is connected and ready to accept callbacks
  std::atomic<GOSoundOrganEngine *> p_OrganEngine;

  // counter of audio callbacks that have been entered but have not yet been
  // exited
  std::atomic_uint m_NCallbacksEntered;

  // For waiting for and notifying when m_NCallbacksEntered becomes 0
  GOMutex m_CallbackMutex;
  GOCondition m_CallbackCondition;

  GOMutex m_lock;

  bool logSoundErrors;

  std::vector<std::unique_ptr<GOSoundPort>> mp_SoundPorts;

  unsigned m_SamplesPerBuffer;
  unsigned m_SampleRate;

  unsigned meter_counter;

  GOSoundDevInfo m_DefaultAudioDevice;

  GOSoundRecorder m_AudioRecorder;

  GOConfig &m_config;

  GOMidiSystem m_midi;

  wxString m_LastErrorMessage;

  ClosingListener *p_ClosingListener;

  bool IsEngineConnected() const { return p_OrganEngine.load(); }

  void ResetMeters();

  void OpenMidi();

  // Starting step 1.
  // Open output device ports and MIDI and do other initialising without using
  // the organ controller or sound engine
  // Sets m_open = true at the end
  void OpenSoundSystem();

  // Finish step 1.
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

  bool IsOpen() const { return m_open; }
  unsigned GetSamplesPerBuffer() const { return m_SamplesPerBuffer; }
  unsigned GetSampleRate() const { return m_SampleRate; }
  GOSoundRecorder &GetAudioRecorder() { return m_AudioRecorder; }

  void SetLogSoundErrorMessages(bool settingsDialogVisible);

  GOMidiSystem &GetMidi();

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

  // Starting step 3.
  // After this step output device callbacks start to be propagated to the
  // given organ sound engine. Can only be called when m_open is true
  void ConnectToEngine(GOSoundOrganEngine &engine);

  // Finish step 3.
  // Wait for all audio callbacks in progress to finish and prevents propagation
  // of future callbacks. Can only be called when m_open is true
  void DisconnectFromEngine();

  bool AudioCallback(unsigned devIndex, GOSoundBufferMutable &outOutputBuffer);
};

#endif
