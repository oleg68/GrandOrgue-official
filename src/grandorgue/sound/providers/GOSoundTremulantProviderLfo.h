/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDTREMULANTPROVIDERLFO_H
#define GOSOUNDTREMULANTPROVIDERLFO_H

#include <memory>

#include <wx/string.h>

#include "sound/providers/GOSoundProviderWave.h"
#include "sound/providers/GOSoundTremulantProvider.h"

class GOSoundAudioSection;

/**
 * A tremulant wave form source that takes the shapes of one modulation period
 * from an LFO wav file. A Hauptwerk-compatible file is stereo: one channel
 * holds the pitch deviation, the other the amplitude deviation; a mono file
 * holds one of them. The wav is loaded through GOSoundProviderWave into a
 * GOSoundAudioSection, and the longest loop of the file is stretched to the
 * requested period length.
 */
class GOSoundTremulantProviderLfo : public GOSoundTremulantProvider {
private:
  // Access for unit tests, as in GOSoundVibratoProcessor
  friend class GOTestSoundTremulantProviderLfo;

  /**
   * The number of target (output) frames rendered at once.
   * GOSoundStream::ReadBlock() writes interleaved stereo, while the output
   * shapes are separate mono vectors, so a scratch buffer is needed anyway.
   * Rendering in chunks bounds its size on the stack (about 8 KiB) whatever
   * the period length is: a period may be as long as 441 s.
   */
  static constexpr unsigned N_RENDER_CHUNK_TARGET_FRAMES = 1024;
  /**
   * The start segment of the loop in the section. Setup() creates the start
   * segments in this order: segment 0 starts at the beginning of the file (the
   * part before the loop, played once), then one segment per loop starts at
   * the loop start, and the end of the loop returns to it. Only the longest
   * loop is loaded, so there is exactly one such segment, 1.
   */
  static constexpr unsigned LOOP_START_SEGMENT_INDEX = 1;
  /** The version of the data written by SaveCache() */
  static constexpr unsigned CACHE_VERSION = 1;

  // Read from the ODF by the constructor, whatever the loading mode: they are
  // available even when the sample data is not loaded (the GUI-only mode)

  /** The LFO wav file, relative to the ODF root */
  const wxString m_FileName;
  /** The pitch channel of the file, 1-based; 0 if the file has no pitch shape
   */
  const unsigned m_PitchChannelN;
  /** The amplitude channel of the file, 1-based; 0 if it has no such shape */
  const unsigned m_AmpChannelN;

  // Loaded from the wave file or the cache by LoadData() and LoadCache(). They
  // are not set in the GUI-only mode, where no sample data is loaded

  /** Owns the loaded GOSoundAudioSection and its pool memory */
  std::unique_ptr<GOSoundProviderWave> o_WaveProvider;
  /** The loaded section; points into o_WaveProvider, non-owning */
  const GOSoundAudioSection *p_section = nullptr;
  /** The loop length in frames; the loop is one modulation period */
  unsigned m_NLoopFrames = 0;

  /**
   * Takes the loaded section from o_WaveProvider, checks that it has a loop and
   * the chosen channels and derives the loop geometry from it. Called once
   * after the data has been loaded from the file or from the cache.
   * @return an error message, or an empty string on success; on an error
   *   p_section is nullptr
   */
  wxString AcquireSection();

public:
  /**
   * Creates a provider that is not loaded yet. It is called by
   * GOTremulant::Load() with the values read from the ODF, so it reads no
   * data: the wave file is loaded later by LoadData() or LoadCache().
   * @param fileName the LFO wav file, relative to the ODF root
   * @param pitchChannelN the pitch channel of the file, 1-based, 1 or 2; 0 if
   *   the file has no pitch shape
   * @param ampChannelN the amplitude channel of the file, 1-based, 1 or 2; 0 if
   *   the file has no amplitude shape
   */
  GOSoundTremulantProviderLfo(
    const wxString &fileName, unsigned pitchChannelN, unsigned ampChannelN);

  /** @return the LFO wav file, relative to the ODF root */
  const wxString &GetFileName() const { return m_FileName; }
  /** @return the pitch channel of the file, 1-based; 0 if it has none */
  unsigned GetPitchChannelN() const { return m_PitchChannelN; }
  /** @return the amplitude channel of the file, 1-based; 0 if it has none */
  unsigned GetAmpChannelN() const { return m_AmpChannelN; }

  /**
   * Adds everything the loaded data depends on to the hash: the cached
   * section is built from the wave file. The content of the file is not
   * hashed, the same as for pipe samples.
   * @param hash the hash of the organ cache
   */
  void UpdateHash(GOHash &hash) const override;

  /**
   * Loads the LFO wave file and builds the audio section.
   * @param fileStore the source of the wave file
   * @param pool the memory pool the section data is allocated in
   * @param isCompressed whether to compress the section data, like a pipe
   * @throws wxString if the file cannot be read, has no usable loop or does
   *   not have the chosen channels
   */
  void LoadData(
    const GOFileStore &fileStore,
    GOMemoryPool &pool,
    bool isCompressed) override;

  /**
   * Restores the section from the organ cache instead of LoadData().
   * @param pool the memory pool the section data is allocated in
   * @param cache the organ cache to read from
   * @return whether the section was restored; if false, the provider stays
   *   unloaded and the framework falls back to LoadData()
   */
  bool LoadCache(GOMemoryPool &pool, GOCache &cache) override;

  /**
   * Writes the loaded section to the organ cache.
   * @param cache the cache writer
   * @return whether the data was written successfully
   */
  bool SaveCache(GOCacheWriter &cache) const override;

  /**
   * Renders one modulation period of the requested channels, stretching the
   * file's loop to nPeriodFrames. If the data has not been loaded (the file
   * failed to load, or it is the GUI-only mode), the shapes are empty. A period
   * that the loop cannot be stretched to (see
   * GOSoundResample::ChunkedIncrements::IsSupported()) also gives empty
   * shapes, with a warning. The period ends within 1/8 of a source frame of
   * the end of the loop, so the repeated period has no audible seam.
   * @param nPeriodFrames the length of the period in frames of the output
   * @param isAmpNeeded whether to render the amplitude deviation
   * @param isPitchNeeded whether to render the pitch deviation
   * @return the deviations; a channel that is not needed stays empty
   */
  Shapes RenderOnePeriod(
    unsigned nPeriodFrames,
    bool isAmpNeeded,
    bool isPitchNeeded) const override;
};

#endif /* GOSOUNDTREMULANTPROVIDERLFO_H */
