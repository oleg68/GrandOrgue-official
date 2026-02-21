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

#include "config/GOConfig.h"
#include "ports/GOSoundPortFactory.h"
#include "sound/buffer/GOSoundBufferMutable.h"
#include "sound/ports/GOSoundPort.h"
#include "threading/GOMutexLocker.h"

#include "GOEvent.h"
#include "GOSoundDefs.h"
#include "GOSoundOrganEngine.h"

GOSoundSystem::GOSoundSystem(GOConfig &settings)
  : m_open(false),
    p_OrganEngine(nullptr),
    m_NCallbacksEntered(0),
    m_CallbackCondition(m_CallbackMutex),
    logSoundErrors(true),
    m_SamplesPerBuffer(0),
    m_SampleRate(0),
    meter_counter(0),
    m_DefaultAudioDevice(GOSoundDevInfo::getInvalideDeviceInfo()),
    m_config(settings),
    m_midi(settings),
    p_ClosingListener(nullptr) {}

GOSoundSystem::~GOSoundSystem() {
  AssureSoundIsClosed();

  GOMidiPortFactory::terminate();
  GOSoundPortFactory::terminate();
}

void GOSoundSystem::OpenMidi() { m_midi.Open(); }

void GOSoundSystem::OpenSoundSystem() {
  m_LastErrorMessage = wxEmptyString;
  assert(!m_open);
  assert(mp_SoundPorts.empty());

  std::vector<GOAudioDeviceConfig> &audio_config
    = m_config.GetAudioDeviceConfig();

  mp_SoundPorts.resize(audio_config.size());
  m_SamplesPerBuffer = m_config.SamplesPerBuffer();
  m_AudioRecorder.SetBytesPerSample(m_config.WaveFormatBytesPerSample());
  m_SampleRate = m_config.SampleRate();
  m_AudioRecorder.SetSampleRate(m_SampleRate);

  const GOPortsConfig &portsConfig(m_config.GetSoundPortsConfig());

  for (unsigned nPorts = mp_SoundPorts.size(), portI = 0; portI < nPorts;
       portI++) {
    GOAudioDeviceConfig &deviceConfig = audio_config[portI];
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
    pPort->Init(
      deviceConfig.GetChannels(),
      m_SampleRate,
      m_SamplesPerBuffer,
      deviceConfig.GetDesiredLatency(),
      portI);
    mp_SoundPorts[portI].reset(pPort);
  }

  StartStreams();
  m_open = true;
}

void GOSoundSystem::ConnectToEngine(GOSoundOrganEngine &engine) {
  assert(m_open);
  assert(engine.IsWorking());

  if (p_OrganEngine.load() != &engine) {
    m_NCallbacksEntered.store(0);
    engine.SetUsed(true);

    GOSoundOrganEngine *const pOldEngine = p_OrganEngine.exchange(&engine);

    if (pOldEngine)
      pOldEngine->SetUsed(false);
  }
}

void GOSoundSystem::StartStreams() {
  for (const std::unique_ptr<GOSoundPort> &pPort : mp_SoundPorts)
    pPort->Open();

  if (m_SamplesPerBuffer > MAX_FRAME_SIZE)
    throw wxString::Format(
      _("Cannot use buffer size above %d samples; "
        "unacceptable quantization would occur."),
      MAX_FRAME_SIZE);

  for (const std::unique_ptr<GOSoundPort> &pPort : mp_SoundPorts)
    pPort->StartStream();
}

void GOSoundSystem::DisconnectFromEngine() {
  assert(m_open);

  GOSoundOrganEngine *const pOldEngine = p_OrganEngine.exchange(nullptr);

  // wait for all started callbacks to finish
  {
    GOMutexLocker lock(m_CallbackMutex);

    while (m_NCallbacksEntered.load() > 0)
      m_CallbackCondition.WaitOrStop(
        "GOSoundSystem::CloseSound waits for all callbacks to finish", nullptr);
  }

  if (pOldEngine)
    pOldEngine->SetUsed(false);
}

void GOSoundSystem::CloseSoundSystem() {
  for (int nPorts = mp_SoundPorts.size(), portI = nPorts - 1; portI >= 0;
       portI--)
    mp_SoundPorts[portI]->Close();
  mp_SoundPorts.clear();

  ResetMeters();
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
  } else // Sometimes is wrong. Close all audio devices that are partially open
    CloseSoundSystem();
  return m_open;
}

void GOSoundSystem::AssureSoundIsClosed() {
  if (p_OrganEngine.load()) {
    assert(m_open);
    if (p_ClosingListener) {
      // try to stop gracefully
      p_ClosingListener->OnSoundClosing(*this);
    };
    if (p_OrganEngine.load()) {
      // Normally OnSoundStopping() should call DisconnectFromEngine(), but if
      // it hasn't done it
      DisconnectFromEngine();
    }
  }
  if (m_open)
    CloseSoundSystem();
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
  const unsigned nFrames = outOutputBuffer.GetNFrames();

  if (p_OrganEngine.load()) {
    if (nFrames == m_SamplesPerBuffer) {
      m_NCallbacksEntered.fetch_add(1);
      wasEntered = true;
    } else
      wxLogError(
        _("No sound output will happen. Samples per buffer has been "
          "changed by the sound driver to %d"),
        nFrames);
  }
  // Reload p_OrganEngine after fetch_add to assure the control thread does
  // not miss m_NCallbacksEntered > 0 and proceeds to wait
  GOSoundOrganEngine *pOrganEngine
    = wasEntered ? p_OrganEngine.load() : nullptr;

  if (pOrganEngine) {
    assert(pOrganEngine->IsUsed());

    const bool isNewPeriod
      = pOrganEngine->ProcessOutputCallback(devIndex, outOutputBuffer);

    if (isNewPeriod)
      UpdateMeter();
  } else
    outOutputBuffer.FillWithSilence();
  if (
    wasEntered && m_NCallbacksEntered.fetch_sub(1) <= 1
    && !p_OrganEngine.load()) {
    // ensure that the control thread enters into m_NCallbackCondition.Wait()
    GOMutexLocker lk(m_CallbackMutex);

    // notify the control thread
    m_CallbackCondition.Broadcast();
  }
  return true;
}

wxString GOSoundSystem::getState() {
  if (mp_SoundPorts.empty())
    return _("No sound output occurring");
  wxString result = wxString::Format(
    _("%d samples per buffer, %d Hz\n"), m_SamplesPerBuffer, m_SampleRate);
  for (const std::unique_ptr<GOSoundPort> &pPort : mp_SoundPorts)
    result = result + _("\n") + pPort->getPortState();
  return result;
}
