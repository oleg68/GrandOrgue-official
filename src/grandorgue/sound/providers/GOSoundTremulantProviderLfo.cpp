/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundTremulantProviderLfo.h"

#include <algorithm>
#include <cassert>

#include <wx/intl.h>
#include <wx/log.h>

#include "sound/buffer/GOSoundBufferMutable.h"
#include "sound/buffer/GOSoundBufferMutableMono.h"
#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/playing/GOSoundAudioSection.h"
#include "sound/playing/GOSoundStream.h"

#include "loader/GOFileStore.h"
#include "loader/GOLoaderFilename.h"
#include "loader/cache/GOCache.h"
#include "loader/cache/GOCacheWriter.h"

#include "GOHash.h"
#include "GOMemoryPool.h"

/**
 * The resampler shared by every LFO provider and period. Its coefficient
 * tables are large (320 KiB), so it is created once. It is built at the start
 * of the program, even if no organ has an Lfo tremulant; it is only read
 * afterwards, so the loading threads share it safely.
 */
static const GOSoundResample resampler;

/**
 * Adds one channel of a rendered chunk to the corresponding part of an output
 * shape.
 * @param chunk the interleaved chunk
 * @param channelN the channel of the chunk (1-based)
 * @param normGain the gain of the samples
 * @param firstTargetFrameI the index of the first target frame of the chunk in
 *   the output buffer
 * @param outBuffer the whole shape; if it is not valid, the channel is not
 *   needed and nothing is added
 */
static void append_chunk_channel(
  const GOSoundBuffer &chunk,
  unsigned channelN,
  float normGain,
  unsigned firstTargetFrameI,
  GOSoundBufferMutableMono &outBuffer) {
  if (outBuffer.isValid()) {
    // A shape is created only if the file has its channel (the number is not 0)
    assert(channelN > 0);

    GOSoundBufferMutableMono outChunk
      = outBuffer.GetSubBuffer(firstTargetFrameI, chunk.GetNFrames());

    // The coefficient overload is hidden by the mono one
    outChunk.GOSoundBufferMutable::AddChannelFrom(
      chunk, channelN - 1, 0, normGain);
  }
}

GOSoundTremulantProviderLfo::GOSoundTremulantProviderLfo(
  const wxString &fileName, unsigned pitchChannelN, unsigned ampChannelN)
  : m_FileName(fileName),
    m_PitchChannelN(pitchChannelN),
    m_AmpChannelN(ampChannelN) {
  assert(pitchChannelN <= 2 && ampChannelN <= 2);
}

wxString GOSoundTremulantProviderLfo::AcquireSection() {
  assert(o_WaveProvider);
  p_section = o_WaveProvider->GetAttack(0, 0);

  wxString errMsg;

  if (p_section) {
    const unsigned nChannels = p_section->GetChannels();
    const unsigned maxChannelN = std::max(m_PitchChannelN, m_AmpChannelN);

    if (p_section->IsOneshot())
      errMsg = _("The file has no loop");
    else if (maxChannelN > nChannels)
      errMsg = wxString::Format(
        _("The file has %u channel(s), but channel %u is chosen"),
        nChannels,
        maxChannelN);
    else {
      const unsigned startOffset
        = p_section->GetStartSegment(LOOP_START_SEGMENT_INDEX).start_offset;
      const unsigned endPos
        = p_section
            ->GetEndSegment(p_section->PickEndSegment(LOOP_START_SEGMENT_INDEX))
            .end_pos;

      if (endPos > startOffset)
        m_NLoopFrames = endPos - startOffset;
      else
        errMsg = _("The loop of the file is empty");
    }
  } else
    errMsg = _("No audio section was loaded");
  if (!errMsg.IsEmpty())
    p_section = nullptr;
  return errMsg;
}

void GOSoundTremulantProviderLfo::UpdateHash(GOHash &hash) const {
  hash.Update(m_FileName);
}

void GOSoundTremulantProviderLfo::LoadData(
  const GOFileStore &fileStore, GOMemoryPool &pool, bool isCompressed) {
  GOSoundProviderWave::AttackFileInfo attack;

  // The ODF root, as GOSoundingPipe does for its sample files
  attack.filename.Assign(m_FileName);
  attack.min_attack_velocity = 0;
  attack.max_released_time = 0;
  attack.max_playback_time = 0;
  attack.attack_start = 0;
  attack.cue_point = 0;
  attack.release_end = 0;
  attack.m_LoopCrossfadeLength = 0;
  attack.m_ReleaseCrossfadeLength = 0;
  attack.m_WaveTremulantStateFor = BOOL3_DEFAULT;
  attack.load_release = false;
  attack.percussive = false;

  o_WaveProvider = std::make_unique<GOSoundProviderWave>(nullptr);
  /* FILE_OWN_DEPTH loads the file in its own format. An empty loop list means
   * "use the loops of the wave", and LOOP_LOAD_LONGEST picks the longest one.
   * A file without any loop loads as a one-shot section and is rejected by
   * AcquireSection(). isCompressed is the global setting, so the section
   * compresses exactly as a pipe section would. */
  o_WaveProvider->LoadFromMultipleFiles(
    fileStore,
    pool,
    {attack},
    {},
    GOSoundProviderWave::FILE_OWN_DEPTH,
    2,
    isCompressed,
    GOSoundProviderWave::LOOP_LOAD_LONGEST,
    true,
    false);

  const wxString errMsg = AcquireSection();

  if (!errMsg.IsEmpty())
    throw attack.filename.GenerateMessage(errMsg);
}

bool GOSoundTremulantProviderLfo::LoadCache(
  GOMemoryPool &pool, GOCache &cache) {
  unsigned version = 0;
  bool isOk = cache.Read(&version, sizeof(version)) && version == CACHE_VERSION;

  if (isOk) {
    o_WaveProvider = std::make_unique<GOSoundProviderWave>(nullptr);
    isOk = o_WaveProvider->LoadCache(pool, cache);
  }
  if (isOk)
    isOk = AcquireSection().IsEmpty();
  if (!isOk)
    // Let the framework fall back to LoadData()
    o_WaveProvider.reset();
  return isOk;
}

bool GOSoundTremulantProviderLfo::SaveCache(GOCacheWriter &cache) const {
  return o_WaveProvider && cache.Write(&CACHE_VERSION, sizeof(CACHE_VERSION))
    && o_WaveProvider->SaveCache(cache);
}

GOSoundTremulantProvider::Shapes GOSoundTremulantProviderLfo::RenderOnePeriod(
  unsigned nPeriodFrames, bool isAmpNeeded, bool isPitchNeeded) const {
  assert(nPeriodFrames > 0);

  Shapes shapes;

  // Without the loaded section (its file failed to load, or the sample data is
  // not loaded at all) there is no modulation. The load error is already logged
  if (p_section) {
    if (GOSoundResample::ChunkedIncrements::IsSupported(
          m_NLoopFrames, nPeriodFrames)) {
      // A shape the file does not have stays empty: no such modulation
      if (isAmpNeeded && m_AmpChannelN)
        shapes.ampDeviation.resize(nPeriodFrames);
      if (isPitchNeeded && m_PitchChannelN)
        shapes.pitchDeviation.resize(nPeriodFrames);

      // An empty shape has no frames, so its buffer is not valid;
      // append_chunk_channel() skips it
      GOSoundBufferMutableMono ampOutBuffer(
        shapes.ampDeviation.data(), shapes.ampDeviation.size());
      GOSoundBufferMutableMono pitchOutBuffer(
        shapes.pitchDeviation.data(), shapes.pitchDeviation.size());
      // The loop is always read with the polyphase interpolation, whatever the
      // pipe interpolation setting is: the shape must not depend on it. The
      // first output frame is source[loop start + 3]
      GOSoundStream stream;

      stream.InitStream(
        &resampler,
        p_section,
        GOSoundResample::GO_POLYPHASE_INTERPOLATION,
        LOOP_START_SEGMENT_INDEX);

      // ReadBlock() always writes interleaved stereo; the chunks bound the size
      // of this buffer whatever the period length is
      GO_DECLARE_LOCAL_SOUND_BUFFER(
        interleaved, 2, N_RENDER_CHUNK_TARGET_FRAMES)

      const float normGain = p_section->GetNormGain();
      // Gives each chunk its own increment so that the period reads exactly
      // the loop
      GOSoundResample::ChunkedIncrements increments(
        m_NLoopFrames, nPeriodFrames);
      // The first target frame of the chunk in the whole shape
      unsigned firstTargetFrameI;

      while ((firstTargetFrameI = increments.GetDoneTargetFrames())
             < nPeriodFrames) {
        // The last chunk may be shorter
        const unsigned nChunkTargetFrames = std::min(
          N_RENDER_CHUNK_TARGET_FRAMES, nPeriodFrames - firstTargetFrameI);
        GOSoundBufferMutable chunk
          = interleaved.GetSubBuffer(0, nChunkTargetFrames);
        // A looped section always has more data
        const bool hasMoreData = stream.ReadBlock(
          increments.NextChunkIncrement(nChunkTargetFrames), chunk);

        assert(hasMoreData);
        // Avoids an unused variable warning when assert is compiled out
        (void)hasMoreData;
        append_chunk_channel(
          chunk, m_AmpChannelN, normGain, firstTargetFrameI, ampOutBuffer);
        append_chunk_channel(
          chunk, m_PitchChannelN, normGain, firstTargetFrameI, pitchOutBuffer);
      }
    } else
      // The shapes stay empty: no modulation
      wxLogWarning(
        _("The loop of %s (%u frames) cannot be stretched to the period of %u "
          "frames: the tremulant is not modulated"),
        m_FileName,
        m_NLoopFrames,
        nPeriodFrames);
  }
  return shapes;
}
