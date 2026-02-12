/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundSystem.h"

#include <wx/app.h>
#include <wx/intl.h>
#include <wx/window.h>

#include "GOEvent.h"
#include "GOOrganController.h"
#include "GOSoundDefs.h"
#include "config/GOConfig.h"
#include "midi/GOMidiSystem.h"
#include "sound/buffer/GOSoundBufferMutable.h"
#include "sound/ports/GOSoundPort.h"
#include "threading/GOMultiMutexLocker.h"
#include "threading/GOMutexLocker.h"

GOSoundSystem::GOSoundSystem(GOConfig &settings)
  : m_open(false),
    m_IsRunning(false),
    m_NCallbacksEntered(0),
    m_CallbackCondition(m_CallbackMutex),
    logSoundErrors(true),
    m_AudioOutputs(),
    m_WaitCount(),
    m_CalcCount(),
    m_SamplesPerBuffer(0),
    meter_counter(0),
    m_DefaultAudioDevice(GOSoundDevInfo::getInvalideDeviceInfo()),
    m_OrganController(0),
    m_config(settings),
    m_midi(settings) {}

GOSoundSystem::~GOSoundSystem() {
  AssureSoundIsClosed();

  GOMidiPortFactory::terminate();
  GOSoundPortFactory::terminate();
}

void GOSoundSystem::OpenMidi() { m_midi.Open(); }

void GOSoundSystem::OpenSoundSystem() {
  m_LastErrorMessage = wxEmptyString;
  assert(!m_open);
  assert(m_AudioOutputs.size() == 0);

  std::vector<GOAudioDeviceConfig> &audio_config
    = m_config.GetAudioDeviceConfig();

  m_AudioOutputs.resize(audio_config.size());
  for (unsigned i = 0; i < m_AudioOutputs.size(); i++)
    m_AudioOutputs[i].port = NULL;
  m_SamplesPerBuffer = m_config.SamplesPerBuffer();
  m_AudioRecorder.SetBytesPerSample(m_config.WaveFormatBytesPerSample());
  m_SampleRate = m_config.SampleRate();
  m_AudioRecorder.SetSampleRate(m_SampleRate);

  const GOPortsConfig &portsConfig(m_config.GetSoundPortsConfig());

  for (unsigned l = m_AudioOutputs.size(), i = 0; i < l; i++) {
    GOAudioDeviceConfig &deviceConfig = audio_config[i];
    GODeviceNamePattern *pNamePattern = &deviceConfig;
    GODeviceNamePattern defaultDevicePattern;

    if (!pNamePattern->IsFilled()) {
      FillDeviceNamePattern(
        GetDefaultAudioDevice(portsConfig), defaultDevicePattern);
      pNamePattern = &defaultDevicePattern;
    }

    GOSoundPort *pPort
      = GOSoundPortFactory::create(portsConfig, this, *pNamePattern);

    if (!pPort)
      throw wxString::Format(
        _("Output device %s not found - no sound output will occur"),
        pNamePattern->GetRegEx());
    m_AudioOutputs[i].port = pPort;
    pPort->Init(
      deviceConfig.GetChannels(),
      m_SampleRate,
      m_SamplesPerBuffer,
      deviceConfig.GetDesiredLatency(),
      i);
  }

  StartStreams();
  m_open = true;
}

void GOSoundSystem::ConnectToEngine() {
  assert(m_open);

  if (!m_IsRunning) {
    m_NCallbacksEntered.store(0);
    m_IsRunning.store(true);
  }
}

void GOSoundSystem::StartStreams() {
  for (unsigned i = 0; i < m_AudioOutputs.size(); i++)
    m_AudioOutputs[i].port->Open();

  if (m_SamplesPerBuffer > MAX_FRAME_SIZE)
    throw wxString::Format(
      _("Cannot use buffer size above %d samples; "
        "unacceptable quantization would occur."),
      MAX_FRAME_SIZE);

  m_WaitCount.exchange(0);
  m_CalcCount.exchange(0);
  for (unsigned i = 0; i < m_AudioOutputs.size(); i++) {
    GOMutexLocker dev_lock(m_AudioOutputs[i].mutex);
    m_AudioOutputs[i].wait = false;
    m_AudioOutputs[i].waiting = true;
  }

  for (unsigned i = 0; i < m_AudioOutputs.size(); i++)
    m_AudioOutputs[i].port->StartStream();
}

void GOSoundSystem::DisconnectFromEngine() {
  assert(m_open);

  m_IsRunning.store(false);

  // wait for all started callbacks to finish
  {
    GOMutexLocker lock(m_CallbackMutex);

    while (m_NCallbacksEntered.load() > 0)
      m_CallbackCondition.WaitOrStop(
        "GOSoundSystem::CloseSound waits for all callbacks to finish", nullptr);
  }

  for (unsigned i = 0; i < m_AudioOutputs.size(); i++) {
    m_AudioOutputs[i].waiting = false;
    m_AudioOutputs[i].wait = false;
    m_AudioOutputs[i].condition.Broadcast();
  }

  for (unsigned i = 1; i < m_AudioOutputs.size(); i++) {
    GOMutexLocker dev_lock(m_AudioOutputs[i].mutex);
    m_AudioOutputs[i].condition.Broadcast();
  }
}

void GOSoundSystem::CloseSoundSystem() {
  assert(m_open);

  for (int i = m_AudioOutputs.size() - 1; i >= 0; i--) {
    if (m_AudioOutputs[i].port) {
      GOSoundPort *const port = m_AudioOutputs[i].port;

      m_AudioOutputs[i].port = NULL;
      port->Close();
      delete port;
    }
  }

  ResetMeters();
  m_AudioOutputs.clear();
  m_open = false;
}

bool GOSoundSystem::AssureSoundIsOpen() {
  if (!m_open) { // Try to open audio devices
    try {
      OpenSoundSystem();
    } catch (wxString &msg) {
      if (logSoundErrors)
        GOMessageBox(msg, _("Error"), wxOK | wxICON_ERROR, NULL);
      else
        m_LastErrorMessage = msg;
    }
  }
  if (m_open) { // Everything is OK. Perform other starting steps
    OpenMidi();
    if (m_OrganController && !m_IsRunning.load())
      m_OrganController->StartSound(*this);
  } else // Sometimes is wrong. Close all audio devices that are partially open
    CloseSoundSystem();
  return m_open;
}

void GOSoundSystem::AssureSoundIsClosed() {
  if (m_IsRunning.load()) {
    assert(m_open);
    assert(m_OrganController);
    m_OrganController->StopSound(*this);
  }
  if (m_open)
    CloseSoundSystem();
}

void GOSoundSystem::AssignOrganFile(GOOrganController *organController) {
  if (organController != m_OrganController) {
    GOMutexLocker locker(m_lock);
    GOMultiMutexLocker multi;

    for (unsigned i = 0; i < m_AudioOutputs.size(); i++)
      multi.Add(m_AudioOutputs[i].mutex);

    if (m_OrganController && m_open)
      m_OrganController->StopSound(*this);

    m_OrganController = organController;

    if (m_OrganController && m_open)
      m_OrganController->StartSound(*this);
  }
}

GOConfig &GOSoundSystem::GetSettings() { return m_config; }

void GOSoundSystem::SetLogSoundErrorMessages(bool settingsDialogVisible) {
  logSoundErrors = settingsDialogVisible;
}

std::vector<GOSoundDevInfo> GOSoundSystem::GetAudioDevices(
  const GOPortsConfig &portsConfig) {
  // Getting a device list tries to open and close each device
  // Because some devices (ex. ASIO) cann't be open more than once
  // then close the current audio device
  AssureSoundIsClosed();
  m_DefaultAudioDevice = GOSoundDevInfo::getInvalideDeviceInfo();

  std::vector<GOSoundDevInfo> list
    = GOSoundPortFactory::getDeviceList(portsConfig);

  for (const auto &devInfo : list)
    if (devInfo.IsDefault()) {
      m_DefaultAudioDevice = devInfo;
      break;
    }
  return list;
}

const GOSoundDevInfo &GOSoundSystem::GetDefaultAudioDevice(
  const GOPortsConfig &portsConfig) {
  if (!m_DefaultAudioDevice.IsValid())
    GetAudioDevices(portsConfig);
  return m_DefaultAudioDevice;
}

void GOSoundSystem::FillDeviceNamePattern(
  const GOSoundDevInfo &deviceInfo, GODeviceNamePattern &pattern) {
  pattern.SetLogicalName(deviceInfo.GetDefaultLogicalName());
  pattern.SetRegEx(deviceInfo.GetDefaultNameRegex());
  pattern.SetPortName(deviceInfo.GetPortName());
  pattern.SetApiName(deviceInfo.GetApiName());
  pattern.SetPhysicalName(deviceInfo.GetFullName());
}

GOMidiSystem &GOSoundSystem::GetMidi() { return m_midi; }

void GOSoundSystem::ResetMeters() {
  wxWindow *const topWindow = wxTheApp ? wxTheApp->GetTopWindow() : nullptr;

  if (topWindow) {
    wxCommandEvent event(wxEVT_METERS, 0);

    event.SetInt(0x1);
    topWindow->GetEventHandler()->AddPendingEvent(event);
  }
}

void GOSoundSystem::UpdateMeter() {
  /* Update meters */
  meter_counter += m_SamplesPerBuffer;
  if (meter_counter >= 6144) // update 44100 / (N / 2) = ~14 times per second
  {
    wxCommandEvent event(wxEVT_METERS, 0);
    event.SetInt(0x0);
    if (wxTheApp->GetTopWindow())
      wxTheApp->GetTopWindow()->GetEventHandler()->AddPendingEvent(event);
    meter_counter = 0;
  }
}

bool GOSoundSystem::AudioCallback(
  unsigned devIndex, GOSoundBufferMutable &outOutputBuffer) {
  bool wasEntered = false;
  const unsigned nSamples = outOutputBuffer.GetNSamples();

  if (m_IsRunning.load()) {
    if (nSamples == m_SamplesPerBuffer) {
      m_NCallbacksEntered.fetch_add(1);
      wasEntered = true;
    } else
      wxLogError(
        _("No sound output will happen. Samples per buffer has been "
          "changed by the sound driver to %d"),
        nSamples);
  }
  // assure that m_IsRunning has not yet been changed after
  // m_NCallbacksEntered.fetch_add, otherwise the control thread may not wait
  if (wasEntered && m_IsRunning.load()) {
    GOSoundOutput &device = m_AudioOutputs[devIndex];
    GOMutexLocker locker(device.mutex);

    while (device.wait && device.waiting)
      device.condition.Wait();

    unsigned cnt = m_CalcCount.fetch_add(1);
    m_SoundEngine.GetAudioOutput(
      devIndex, cnt + 1 >= m_AudioOutputs.size(), outOutputBuffer);
    device.wait = true;
    unsigned count = m_WaitCount.fetch_add(1);

    if (count + 1 == m_AudioOutputs.size()) {
      m_SoundEngine.NextPeriod();
      UpdateMeter();

      m_SoundEngine.WakeupThreads();
      m_CalcCount.exchange(0);
      m_WaitCount.exchange(0);

      for (unsigned i = 0; i < m_AudioOutputs.size(); i++) {
        GOMutexLocker lock(m_AudioOutputs[i].mutex, i == devIndex);
        m_AudioOutputs[i].wait = false;
        m_AudioOutputs[i].condition.Signal();
      }
    }
  } else
    outOutputBuffer.FillWithSilence();
  if (
    wasEntered && m_NCallbacksEntered.fetch_sub(1) <= 1
    && !m_IsRunning.load()) {
    // ensure that the control thread enters into m_NCallbackCondition.Wait()
    GOMutexLocker lk(m_CallbackMutex);

    // notify the control thread
    m_CallbackCondition.Broadcast();
  }
  return true;
}

GOSoundOrganEngine &GOSoundSystem::GetEngine() { return m_SoundEngine; }

wxString GOSoundSystem::getState() {
  if (!m_AudioOutputs.size())
    return _("No sound output occurring");
  wxString result = wxString::Format(
    _("%d samples per buffer, %d Hz\n"), m_SamplesPerBuffer, m_SampleRate);
  for (unsigned i = 0; i < m_AudioOutputs.size(); i++)
    result = result + _("\n") + m_AudioOutputs[i].port->getPortState();
  return result;
}
