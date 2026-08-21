/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDREVERB_H
#define GOSOUNDREVERB_H

#include <wx/string.h>

#include "ptrvector.h"

class Convproc;
class GOConfig;
class GOSoundBufferPlanarMutable;

class GOSoundReverb {
public:
  /**
   * @brief Reverb configuration, independent of engine runtime parameters.
   *
   * sampleRate and nSamplesPerBuffer are NOT part of this struct —
   * they are engine-level parameters passed directly to Setup().
   */
  struct ReverbConfig {
    bool isEnabled;
    bool isDirect;
    unsigned channel;
    unsigned startOffset;
    unsigned len;
    unsigned delay;
    float gain;
    wxString file;
  };

  /** Named constant meaning "reverb disabled". Used as default. */
  static const ReverbConfig CONFIG_REVERB_DISABLED;

  /** Creates a ReverbConfig from GOConfig. */
  static ReverbConfig createReverbConfig(const GOConfig &config);

private:
  unsigned m_channels;
  // the frame count Process() must be called with, matching what was passed
  // to Convproc::configure() in Setup() - Process() asserts against this
  unsigned m_FramesPerBuffer = 0;
  ptr_vector<Convproc> m_engine;

  void Cleanup();

public:
  GOSoundReverb(unsigned channels);
  virtual ~GOSoundReverb();

  void Reset();
  void Setup(
    const ReverbConfig &config,
    unsigned nSamplesPerBuffer,
    unsigned sampleRate);

  /**
   * Runs the convolution reverb in place over a planar output buffer, one
   * channel of the Convproc engine per channel of the buffer.
   * @param buffer The output buffer to convolve in place
   */
  void Process(GOSoundBufferPlanarMutable &buffer);
};

#endif
