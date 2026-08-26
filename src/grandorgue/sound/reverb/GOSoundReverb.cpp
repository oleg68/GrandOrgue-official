/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundReverb.h"

#include <algorithm>
#include <cassert>
#include <memory>

#include <wx/intl.h>
#include <wx/log.h>

#include "config/GOConfig.h"
#include "files/GOStandardFile.h"
#include "sound/buffer/GOSoundBufferMutableMono.h"
#include "sound/buffer/GOSoundBufferPlanarMutable.h"
#include "sound/playing/GOSoundResample.h"

#include "GOWave.h"
#include "zita-convolver.h"

const GOSoundReverb::ReverbConfig GOSoundReverb::CONFIG_REVERB_DISABLED
  = {false, false, 0, 0, 0, 0, 0.0f, std::string()};

GOSoundReverb::ReverbConfig GOSoundReverb::createReverbConfig(
  const GOConfig &config) {
  return {
    .isEnabled = config.ReverbEnabled(),
    .isDirect = config.ReverbDirect(),
    .channel = config.ReverbChannel(),
    .startOffset = config.ReverbStartOffset(),
    .len = config.ReverbLen(),
    .delay = config.ReverbDelay(),
    .gain = config.ReverbGain(),
    .file = config.ReverbFile().ToStdString(),
  };
}

GOSoundReverb::GOSoundReverb(unsigned channels)
  : m_channels(channels), m_engine() {}

GOSoundReverb::~GOSoundReverb() { Cleanup(); }

void GOSoundReverb::Cleanup() {
  for (unsigned i = 0; i < m_engine.size(); i++) {
    m_engine[i]->stop_process();
    m_engine[i]->cleanup();
  }
}

GOSoundReverb::IRData GOSoundReverb::loadIRData(
  const ReverbConfig &config, unsigned sampleRate) {
  // Owns the malloc'd sample buffer for the whole function, including every
  // throwing path below (a missing/invalid file, a failed resample, or
  // std::vector::assign() running out of memory) - no manual free() needed.
  std::unique_ptr<float, decltype(&free)> data(nullptr, &free);
  GOWave wav;
  unsigned offset = config.startOffset;
  const float gain = config.gain;
  unsigned len;

  GOStandardFile reverb_file(config.file);
  wav.Open(&reverb_file);
  if (offset > wav.GetLength())
    throw(wxString) _("Invalid reverb start offset");
  len = wav.GetLength();
  data.reset((float *)malloc(sizeof(float) * len));
  if (!data)
    throw(wxString) _("Out of memory");
  wav.ReadSamples(
    data.get(), GOWave::SF_IEEE_FLOAT, wav.GetSampleRate(), -config.channel);

  float *pData = data.get();

  for (unsigned i = len; i > 0; i--)
    *pData++ *= gain;
  if (len >= offset + config.len && config.len)
    len = offset + config.len;
  if (wav.GetSampleRate() != sampleRate) {
    GOSoundResample resample;
    float *new_data = resample.NewResampledMono(
      data.get(), len, wav.GetSampleRate(), sampleRate);

    if (!new_data)
      throw(wxString) _("Resampling failed");
    data.reset(new_data);
    offset = (offset * sampleRate) / (float)wav.GetSampleRate();
  }
  wav.Close();

  IRData irData;

  irData.delay = (sampleRate * config.delay) / 1000;
  irData.isDirect = config.isDirect;
  irData.data.assign(data.get() + offset, data.get() + len);

  return irData;
}

void GOSoundReverb::Setup(
  const ReverbConfig &config, unsigned nSamplesPerBuffer, unsigned sampleRate) {
  Cleanup();
  m_FramesPerBuffer = nSamplesPerBuffer;

  if (!config.isEnabled)
    return;

  m_engine.clear();
  for (unsigned i = 0; i < m_channels; i++) {
    Convproc *pConvProc = new Convproc();

    // Disable stopping the reverb engine when the system is overloaded
    pConvProc->set_options(Convproc::OPT_LATE_CONTIN);
    m_engine.push_back(pConvProc);
  }
  unsigned val = nSamplesPerBuffer;
  if (val < Convproc::MINPART)
    val = Convproc::MINPART;
  if (val > Convproc::MAXPART)
    val = Convproc::MAXPART;
  try {
    for (unsigned i = 0; i < m_engine.size(); i++)
      if (m_engine[i]->configure(
            1, 1, 1000000, nSamplesPerBuffer, val, Convproc::MAXPART, 1))
        throw(wxString) _("Invalid reverb configuration (samples per buffer)");

    const IRData irData = loadIRData(config, sampleRate);
    const unsigned block = 0x4000;
    float *const d = const_cast<float *>(irData.data.data());
    const unsigned l = (unsigned)irData.data.size();

    for (unsigned i = 0; i < m_channels; i++) {
      float g = 1;
      if (irData.isDirect)
        m_engine[i]->impdata_create(0, 0, 0, &g, 0, 1);
      for (unsigned j = 0; j < l; j += block) {
        m_engine[i]->impdata_create(
          0,
          0,
          1,
          d + j,
          irData.delay + j,
          irData.delay + j + std::min(l - j, block));
      }
    }
    for (unsigned i = 0; i < m_engine.size(); i++)
      m_engine[i]->start_process(0, 0);
  } catch (wxString error) {
    wxLogError(_("Reverb load error: %s"), error.c_str());
    m_engine.clear();
  }
}

void GOSoundReverb::Reset() {
  for (unsigned i = 0; i < m_engine.size(); i++)
    m_engine[i]->reset();
}

void GOSoundReverb::Process(GOSoundBufferPlanarMutable &buffer) {
  if (!m_engine.size())
    return;

  assert(buffer.GetNFrames() == m_FramesPerBuffer);
  assert(buffer.GetNChannels() == m_channels);

  const unsigned nFrames = buffer.GetNFrames();

  for (unsigned i = 0; i < m_channels; i++) {
    Convproc *const pConvProc = m_engine[i];

    if (pConvProc->state() != Convproc::ST_WAIT)
      pConvProc->check_stop();

    if (pConvProc->state() == Convproc::ST_PROC) {
      GOSoundBufferMutableMono channelBuffer = buffer.GetChannelBuffer(i);
      GOSoundBufferMutableMono convInBuffer(pConvProc->inpdata(0), nFrames);
      GOSoundBufferMutableMono convOutBuffer(pConvProc->outdata(0), nFrames);

      convInBuffer.CopyFrom(channelBuffer);
      pConvProc->process(false);
      channelBuffer.CopyFrom(convOutBuffer);
    }
  }
}
