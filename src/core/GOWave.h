/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOWAVE_H
#define GOWAVE_H

#include <cstdint>
#include <vector>

#include <wx/string.h>

#include "GOBuffer.h"
#include "GOWaveLoop.h"

class GOOpenedFile;

class GOWave {
private:
  GOBuffer<uint8_t> m_SampleData;
  unsigned m_BytesPerSample;
  unsigned m_SampleRate;
  unsigned m_CuePoint;
  unsigned m_Channels;
  unsigned m_MidiNote;
  float m_PitchFract;
  bool m_isPacked;
  /** The stored items are either IEEE floats (format code 3) or PCM */
  bool m_isFloat;
  bool m_hasRelease;
  std::vector<GOWaveLoop> m_Loops;

  void SetInvalid();
  void LoadFormatChunk(const uint8_t *ptr, unsigned long length);
  void LoadCueChunk(const uint8_t *ptr, unsigned long length);
  void LoadSamplerChunk(const uint8_t *ptr, unsigned long length);
  template <class T> static void writeNext(uint8_t *&output, const T &value);
  template <class T> static T readNext(const uint8_t *&input);

public:
  typedef enum {
    SF_SIGNEDBYTE_8 = 8,
    SF_SIGNEDSHORT_9 = 9,
    SF_SIGNEDSHORT_10 = 10,
    SF_SIGNEDSHORT_11 = 11,
    SF_SIGNEDSHORT_12 = 12,
    SF_SIGNEDSHORT_13 = 13,
    SF_SIGNEDSHORT_14 = 14,
    SF_SIGNEDSHORT_15 = 15,
    SF_SIGNEDSHORT_16 = 16,
    SF_SIGNEDINT24_17 = 17,
    SF_SIGNEDINT24_18 = 18,
    SF_SIGNEDINT24_19 = 19,
    SF_SIGNEDINT24_20 = 20,
    SF_SIGNEDINT24_21 = 21,
    SF_SIGNEDINT24_22 = 22,
    SF_SIGNEDINT24_23 = 23,
    SF_SIGNEDINT24_24 = 24,
    SF_IEEE_FLOAT
  } SAMPLE_FORMAT;

  /** Raw units per full scale: the internal form has 24 fractional bits */
  static constexpr float SCALE_24_BITS = (float)(1 << 23);

  /**
   * @return whether the stored samples are already in the requested format,
   * so that they can be copied verbatim instead of being converted sample by
   * sample
   *
   * Alternatives considered:
   * - compare only the item size: wrong for the reduced-depth formats
   *   (SF_SIGNEDSHORT_12, SF_SIGNEDINT24_20, ...), which need a shift, and
   *   for packed (WavPack) integer data, which is decoded to int32;
   * - include 8-bit PCM: its file data is offset binary while the output is
   *   two's complement, so it always needs the bias conversion.
   *
   * @param readFormat the format requested by ReadSamples()
   */
  bool IsStoredAs(SAMPLE_FORMAT readFormat) const;

  GOWave();
  ~GOWave();

  void Open(GOOpenedFile *file);
  void Open(const GOBuffer<uint8_t> &content, const wxString fileName);
  bool Save(GOBuffer<uint8_t> &buf);
  void Close();

  /* GetChannels()
   * Returns the number of channels in the wave stream.
   */
  unsigned GetChannels() const;

  /* GetReleaseMarkerPosition()
   * Returns the block where the release marker has been placed at.
   */
  bool HasReleaseMarker() const;
  unsigned GetReleaseMarkerPosition() const;

  unsigned GetNbLoops() const;
  const GOWaveLoop &GetLoop(unsigned idx) const;
  const GOWaveLoop &GetLongestLoop() const;

  /* GetLength()
   * Returns the number of blocks in the wave file (there are *channel*
   * samples in a block so for a mono wave, this will simply be the number
   * of samples)
   */
  unsigned GetLength() const;

  /* ReadSamples()
   * Reads all of the samples in the wave file into destBuffer at the
   * specified read format and sample rate.
   */
  void ReadSamples(
    void *dest_buffer,
    GOWave::SAMPLE_FORMAT read_format,
    unsigned sample_rate,
    int channels) const;

  unsigned GetSampleRate() const;
  unsigned GetBitsPerSample() const;

  /**
   * @return whether the stored samples are IEEE floats rather than integers.
   * Alternatives considered: GetBitsPerSample() == 32 and a 4-byte item size
   * both identify a float only while 32-bit PCM is unsupported.
   */
  bool IsFloat() const { return m_isFloat; }

  /**
   * @return the format that holds the samples of the file without loss: the
   *   integer format of the file's depth, or SF_IEEE_FLOAT for a float file
   */
  SAMPLE_FORMAT GetOwnFormat() const;

  /**
   * @param bitsPerSample the requested depth, 8..24
   * @return the integer format of the smaller depth of the file and the
   *   requested one; a float file is always converted to the requested depth
   */
  SAMPLE_FORMAT GetMinIntFormat(unsigned bitsPerSample) const;

  unsigned GetMidiNote() const;
  float GetPitchFract() const;

  static bool IsWave(const GOBuffer<uint8_t> &data);
  static bool IsWaveFile(const GOBuffer<uint8_t> &data);
};

#endif /* GOWAVE_H */
