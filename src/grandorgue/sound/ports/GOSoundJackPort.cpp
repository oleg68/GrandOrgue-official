/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

// wx should be included before windows.h (from jack.h), otherwise it cannot be
// compiled with mingw
#include "GOSoundJackPort.h"

#include <cassert>

#include <wx/log.h>

#include "config/GODeviceNamePattern.h"
#include "sound/buffer/GOSoundBufferMutableMono.h"

const wxString GOSoundJackPort::PORT_NAME = wxT("Jack");

GOSoundJackPort::GOSoundJackPort(GOSoundSystem *sound, wxString name)
  : GOSoundPort(sound, name) {}

GOSoundJackPort::~GOSoundJackPort() { Close(); }

#if defined(GO_USE_JACK)

#define MAX_CHANNELS_COUNT 64

static const jack_options_t JACK_OPTIONS = JackNullOption;
static const char *CLIENT_NAME = "GrandOrgueAudio";
static const wxString DEVICE_NAME = "Native Output";

void GOSoundJackPort::jackLatencyCallback(
  jack_latency_callback_mode_t mode, void *pData) {
  if (mode == JackPlaybackLatency) {
    GOSoundJackPort &port = *(GOSoundJackPort *)pData;

    if (port.m_Channels) {
      jack_latency_range_t range;

      jack_port_get_latency_range(
        port.mp_JackOutPorts[0], JackPlaybackLatency, &range);
      port.SetActualLatency(range.min / (double)port.m_SampleRate);
      wxLogDebug("JACK actual latency set to %d ms", port.m_ActualLatency);
    }
  }
}

int GOSoundJackPort::jackProcessCallback(jack_nframes_t nSamples, void *pData) {
  int result = 1;
  GOSoundJackPort &port = *(GOSoundJackPort *)pData;

  assert(nSamples == port.m_InterleavedBuffer.GetNSamples());
  if (port.AudioCallback(port.m_InterleavedBuffer)) {
    unsigned int channelI = 0;

    // Process each channel
    for (auto pJackOutPort : port.mp_JackOutPorts) {
      // Get a JACK output buffer for this channel
      jack_default_audio_sample_t *pOutData
        = (jack_default_audio_sample_t *)jack_port_get_buffer(
          pJackOutPort, nSamples);
      // Create a GOSoundBufferMutableMono wrapper around the JACK buffer
      GOSoundBufferMutableMono jackBuffer(pOutData, nSamples);

      if (port.m_IsStarted) {
        // Copy one channel from interleaved buffer to the JACK buffer
        jackBuffer.CopyChannelFrom(port.m_InterleavedBuffer, channelI);
      } else {
        // Fill the JACK buffer with silence
        jackBuffer.FillWithSilence();
      }
      channelI++;
    }
    result = 0;
  }
  return result;
}

void GOSoundJackPort::jackShutdownCallback(void *pData) {
  // GOSoundJackPort * const jp = (GOSoundJackPort *) data;
}

void GOSoundJackPort::Open() {
  Close();

  wxLogDebug("Connecting to a jack server");

  jack_status_t jack_status;

  m_JackClient
    = jack_client_open(CLIENT_NAME, JACK_OPTIONS, &jack_status, NULL);

  if (!m_JackClient) {
    if (jack_status & JackServerFailed)
      throw wxString::Format("Unable to connect to a JACK server");
    throw wxString::Format(
      "jack_client_open() failed, status = 0x%2.0x", jack_status);
  }
  if (jack_status & JackServerStarted)
    wxLogDebug("JACK server started");
  if (jack_status & JackNameNotUnique)
    wxLogDebug("Unique name `%s' assigned", jack_get_client_name(m_JackClient));

  const jack_nframes_t sample_rate = jack_get_sample_rate(m_JackClient);
  const jack_nframes_t samples_per_buffer = jack_get_buffer_size(m_JackClient);

  if (sample_rate != m_SampleRate)
    throw wxString::Format(
      "Device %s wants a different sample rate: %d.\nPlease adjust the "
      "GrandOrgue audio settings.",
      m_Name,
      sample_rate);
  if (samples_per_buffer != m_SamplesPerBuffer)
    throw wxString::Format(
      "Device %s wants a different samples per buffer settings: %d.\nPlease "
      "adjust the GrandOrgue audio settings.",
      m_Name,
      samples_per_buffer);

  char port_name[32];

  if (m_Channels) {
    mp_JackOutPorts.clear();
    for (unsigned int i = 0; i < m_Channels; i++) {
      snprintf(port_name, sizeof(port_name), "out_%d", i);

      jack_port_t *pJackOutPort = jack_port_register(
        m_JackClient, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

      if (!pJackOutPort)
        throw wxString::Format("No more JACK ports available");
      mp_JackOutPorts.push_back(pJackOutPort);
    }
  }
  wxLogDebug("Created %d output ports", m_Channels);

  jack_set_latency_callback(m_JackClient, &jackLatencyCallback, this);
  jack_set_process_callback(m_JackClient, &jackProcessCallback, this);
  jack_on_shutdown(m_JackClient, &jackShutdownCallback, this);

  m_InterleavedBuffer.Resize(m_Channels, samples_per_buffer);

  m_IsOpen = true;
}

void GOSoundJackPort::StartStream() {
  if (!m_JackClient || !m_IsOpen)
    throw wxString::Format("Audio device %s not open", m_Name);
  m_IsStarted = true;
  if (jack_activate(m_JackClient))
    wxString::Format("Cannot activate the jack client");
}

wxString GOSoundJackPort::getName() {
  return GOSoundPortFactory::getInstance().ComposeDeviceName(
    PORT_NAME, wxEmptyString, "Native Output");
}
#endif /* GO_USE_JACK */

void GOSoundJackPort::Close() {
#if defined(GO_USE_JACK)
  m_IsStarted = false;
  m_IsOpen = false;
  if (m_JackClient) {
    jack_deactivate(m_JackClient);
    wxLogDebug("Disconnecting from the jack server");
    jack_client_close(m_JackClient);
    m_JackClient = NULL;
  }
  mp_JackOutPorts.clear();
#endif
}

static const wxString OLD_STYLE_NAME = wxT("Jack Output");

GOSoundPort *GOSoundJackPort::create(
  const GOPortsConfig &portsConfig,
  GOSoundSystem *sound,
  GODeviceNamePattern &pattern) {
  GOSoundPort *pPort = nullptr;
#if defined(GO_USE_JACK)
  const wxString devName = getName();

  if (
    portsConfig.IsEnabled(PORT_NAME)
    && (
      pattern.DoesMatch(devName)
      || pattern.DoesMatch(devName + GOPortFactory::c_NameDelim)
      || pattern.DoesMatch(OLD_STYLE_NAME))) {
    pattern.SetPhysicalName(devName);
    pPort = new GOSoundJackPort(sound, devName);
  }
#endif
  return pPort;
}

void GOSoundJackPort::addDevices(
  const GOPortsConfig &portsConfig, std::vector<GOSoundDevInfo> &result) {
#if defined(GO_USE_JACK)
  if (portsConfig.IsEnabled(PORT_NAME))
    result.emplace_back(
      PORT_NAME, wxEmptyString, DEVICE_NAME, MAX_CHANNELS_COUNT, false);
#endif
}
