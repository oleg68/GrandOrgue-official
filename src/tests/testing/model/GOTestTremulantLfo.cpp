/*
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestTremulantLfo.h"

#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <vector>

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/wfstream.h>

#include "loader/GOFileStore.h"
#include "loader/cache/GOCache.h"
#include "loader/cache/GOCacheWriter.h"
#include "model/GOTremulant.h"
#include "testing/sound/GOTestWaveFileBuilder.h"

#include "GOHash.h"
#include "GOMemoryPool.h"
#include "GOTestScope.h"
#include "GOTestTremulantFixture.h"
#include "GOWaveLoop.h"

const std::string GOTestTremulantLfo::TEST_NAME = "GOTestTremulantLfo";

static constexpr unsigned N_FILE_FRAMES = 600;
static constexpr float PITCH_VALUE = 0.01f;
static constexpr float AMP_VALUE = 0.25f;
// 20 * log10(2): doubles the amplitude
static constexpr float DB_FACTOR_2 = 6.0205999f;
static constexpr unsigned SAMPLE_RATE = 48000;

// The common part of the ODF of an Lfo tremulant
static const std::string LFO_ODF_HEAD
  = "[Tremulant001]\nName=Test\nTremulantType=Lfo\nPeriod=100\n"
    "StartRate=10\nStopRate=10\nLfoWaveFile=lfo.wav\n";

namespace {

/**
 * A temporary directory with a stereo float file "lfo.wav" with a constant
 * pitch channel PITCH_VALUE and a constant amplitude channel AMP_VALUE, and a
 * file store that looks into it. The directory is removed at the end.
 */
class LfoFile {
private:
  std::filesystem::path m_Path;

public:
  GOFileStore fileStore;

  explicit LfoFile(const GOConfig &config) : fileStore(config) {
    static unsigned counter = 0;

    m_Path = std::filesystem::temp_directory_path()
      / std::format("goTremLfoTest_{}", counter++);
    std::filesystem::create_directories(m_Path);

    const GOWaveLoop loop = {100, N_FILE_FRAMES - 1};
    std::vector<float> items(2 * N_FILE_FRAMES);

    for (unsigned frameI = 0; frameI < N_FILE_FRAMES; frameI++) {
      items[frameI * 2] = PITCH_VALUE;
      items[frameI * 2 + 1] = AMP_VALUE;
    }
    GOTestWaveFileBuilder::writeWavFile(
      wxString(m_Path.string()) + wxT("/lfo.wav"),
      true,
      32,
      2,
      items.data(),
      N_FILE_FRAMES,
      &loop);
    fileStore.SetDirectory(wxString(m_Path.string()));
  }
  ~LfoFile() { std::filesystem::remove_all(m_Path); }
};

} // namespace

/**
 * The polyphase resampler of a stretched constant has a passband ripple of
 * about 0.2 percent, so the values are compared with a relative tolerance.
 * @return whether the value is within 1 percent of the expected one
 */
static bool is_near(float value, float expected) {
  return std::abs(value - expected) <= 0.01f * std::abs(expected);
}

/**
 * @param f a function that is expected to throw a wxString
 * @param text the text that the message must contain
 * @return whether f has thrown a message with the text
 */
static bool throws_message(
  const std::function<void()> &f, const wxString &text) {
  bool hasThrown = false;

  try {
    f();
  } catch (const wxString &message) {
    hasThrown = message.Contains(text);
  }
  return hasThrown;
}

void GOTestTremulantLfo::TestLoadDefaults() {
  GOTestTremulantModel model;
  const auto oTremulant
    = load_tremulant_from_text(model.organModel, LFO_ODF_HEAD, "");

  GOAssert(
    oTremulant->GetTremulantType() == GOTremulant::LFO,
    "Lfo must be recognised");
  GOAssert(
    oTremulant->GetPeriodMs() == 100 && oTremulant->GetStartTimeMs() == 100
      && oTremulant->GetStopTimeMs() == 100,
    "Lfo must read the shared keys");
  GOAssert(
    oTremulant->GetLfoFileName() == wxT("lfo.wav")
      && oTremulant->GetLfoPitchChannelN() == 1
      && oTremulant->GetLfoAmpChannelN() == 2
      && oTremulant->GetLfoAmpDepthAdjustDb() == 0.0f
      && oTremulant->GetLfoPitchDepthAdjustPcnt() == 100.0f,
    "the Lfo keys must have their defaults");

  const auto oMono = load_tremulant_from_text(
    model.organModel,
    LFO_ODF_HEAD + "LfoAmpChannel=0\nLfoPitchChannel=2\n",
    "");

  GOAssert(
    oMono->GetLfoPitchChannelN() == 2 && oMono->GetLfoAmpChannelN() == 0,
    "the channels must be read, 0 meaning none");
}

void GOTestTremulantLfo::TestOnlyOwnKeysAreRead() {
  GOTestTremulantModel model;
  const auto oSynth = load_tremulant_from_text(
    model.organModel,
    "[Tremulant001]\nName=Test\nTremulantType=Synth\nPeriod=200\n"
    "StartRate=10\nStopRate=10\nLfoAmpDepthAdjustDb=6\n",
    "");
  const auto oLfo = load_tremulant_from_text(
    model.organModel,
    LFO_ODF_HEAD + "AmpModDepth=50\nPitchModDepthCents=10\n",
    "");
  const auto oWave = load_tremulant_from_text(
    model.organModel,
    "[Tremulant001]\nName=Test\nTremulantType=Wave\nPeriod=200\n"
    "AmpModDepth=50\nLfoAmpDepthAdjustDb=6\n",
    "");

  GOAssert(
    oSynth->GetLfoAmpDepthAdjustDb() == 0.0f,
    "an Lfo key under Synth is not read");
  GOAssert(
    oLfo->GetAmpModDepthPcnt() == 0 && oLfo->GetPitchModDepthCents() == 0.0f,
    "a Synth key under Lfo is not read");
  GOAssert(
    oWave->GetPeriodMs() == 0 && oWave->GetAmpModDepthPcnt() == 0
      && oWave->GetLfoAmpDepthAdjustDb() == 0.0f,
    "a Wave tremulant reads none of the Synth and Lfo keys");
}

void GOTestTremulantLfo::TestRejectedKeys() {
  GOTestTremulantModel model;
  // A key line added to the ODF of an Lfo tremulant
  const auto isRejected = [&](const std::string &line, const wxString &text) {
    return throws_message(
      [&] {
        load_tremulant_from_text(model.organModel, LFO_ODF_HEAD + line, "");
      },
      text);
  };

  GOAssert(
    isRejected("LfoPitchChannel=0\nLfoAmpChannel=0\n", wxT("LfoPitchChannel")),
    "both channels 0 must be rejected");
  GOAssert(
    isRejected("LfoPitchChannel=3\n", wxT("LfoPitchChannel")),
    "channel 3 must be rejected");
  GOAssert(
    isRejected("LfoAmpDepthAdjustDb=21\n", wxT("LfoAmpDepthAdjustDb")),
    "an amplitude adjust above 20 dB must be rejected");
  GOAssert(
    isRejected("LfoPitchDepthAdjustPcnt=401\n", wxT("LfoPitchDepthAdjustPcnt")),
    "a pitch adjust above 400 percent must be rejected");
  GOAssert(
    throws_message(
      [&] {
        load_tremulant_from_text(
          model.organModel,
          "[Tremulant001]\nName=Test\nTremulantType=Lfo\nPeriod=100\n"
          "StartRate=10\nStopRate=10\nLfoWaveFile= \n",
          "");
      },
      wxT("LfoWaveFile")),
    "an empty LfoWaveFile must be rejected");
}

void GOTestTremulantLfo::TestRenderFactors() {
  GOTestTremulantModel model;
  const LfoFile lfoFile(model.config);
  GOMemoryPool pool;

  struct Case {
    std::string extraKeys;
    float expectedAmp;
    float expectedPitch; // 0 means the shape must be empty
  };

  for (const Case &c :
       {Case{"", AMP_VALUE, PITCH_VALUE},
        Case{"LfoAmpDepthAdjustDb=6.0205999\n", AMP_VALUE * 2, PITCH_VALUE},
        Case{"LfoPitchDepthAdjustPcnt=50\n", AMP_VALUE, PITCH_VALUE / 2},
        Case{"LfoPitchDepthAdjustPcnt=0\n", AMP_VALUE, 0.0f}}) {
    const auto oTremulant = load_tremulant_from_text(
      model.organModel, LFO_ODF_HEAD + c.extraKeys, "");

    GOAssert(
      oTremulant->LoadFromFileWithoutExc(lfoFile.fileStore, pool),
      std::string("the file must be loaded: ")
        + oTremulant->GetLoadError().ToStdString());

    const auto description = oTremulant->Render(SAMPLE_RATE);

    GOAssert(
      description.nPeriodFrames == SAMPLE_RATE / 10
        && description.ampDeviation.size() == description.nPeriodFrames,
      "the period must be rendered");
    GOAssert(
      is_near(description.ampDeviation[100], c.expectedAmp),
      std::format(
        "amplitude must be {}, not {} ({})",
        c.expectedAmp,
        description.ampDeviation[100],
        c.extraKeys));
    if (c.expectedPitch == 0.0f)
      GOAssert(
        description.pitchDeviation.empty(), "a zero pitch adjust: no pitch");
    else
      GOAssert(
        is_near(description.pitchDeviation[100], c.expectedPitch),
        std::format("pitch must be {} ({})", c.expectedPitch, c.extraKeys));
  }
}

void GOTestTremulantLfo::TestCacheRoundTrip() {
  GOTestTremulantModel model;
  const LfoFile lfoFile(model.config);
  GOMemoryPool pool;
  const auto oSaved
    = load_tremulant_from_text(model.organModel, LFO_ODF_HEAD, "");

  GOAssert(
    oSaved->LoadFromFileWithoutExc(lfoFile.fileStore, pool),
    "the file must be loaded");

  const wxString path = wxFileName::CreateTempFileName(wxT("goTremLfoCache"));

  {
    wxFileOutputStream stream(path);
    GOCacheWriter writer(stream, false);

    GOAssert(
      writer.WriteHeader() && oSaved->SaveCache(writer),
      "the tremulant must be saved");
    writer.Close();
  }

  // The GOCache constructor maps the file into the pool, so the cache is
  // loaded into a pool of its own. The restored tremulant must be destroyed
  // before it
  GOMemoryPool cachePool;
  wxFile file(path);
  GOCache cache(file, cachePool);
  const auto oRestored
    = load_tremulant_from_text(model.organModel, LFO_ODF_HEAD, "");

  GOAssert(
    cache.ReadHeader() && oRestored->LoadFromCacheWithoutExc(cachePool, cache),
    "the tremulant must be restored from the cache");
  cache.Close();

  const auto saved = oSaved->Render(SAMPLE_RATE);
  const auto restored = oRestored->Render(SAMPLE_RATE);

  GOAssert(
    saved.ampDeviation == restored.ampDeviation
      && saved.pitchDeviation == restored.pitchDeviation,
    "the restored tremulant must render the same shapes");
  wxRemoveFile(path);
}

wxString GOTestTremulantLfo::hashOf(const GOTremulant &tremulant) {
  GOHash hash;

  tremulant.UpdateHash(hash);
  return hash.getStringHash();
}

void GOTestTremulantLfo::TestHashFollowsCompression() {
  GOTestTremulantModel model;
  const auto oLfo
    = load_tremulant_from_text(model.organModel, LFO_ODF_HEAD, "");
  const auto oSynth
    = load_tremulant(model.organModel, "Synth", 100, 10, 10, 10);

  model.config.LosslessCompression(false);

  const wxString lfoPlain = hashOf(*oLfo);
  const wxString synthPlain = hashOf(*oSynth);

  model.config.LosslessCompression(true);
  GOAssert(
    hashOf(*oLfo) != lfoPlain,
    "the compression setting must change the hash of an Lfo tremulant");
  GOAssert(
    hashOf(*oSynth) == synthPlain,
    "the compression setting must not change the hash of a Synth tremulant");
}

void GOTestTremulantLfo::run() {
  GO_RUN_TEST(TestLoadDefaults())
  GO_RUN_TEST(TestOnlyOwnKeysAreRead())
  GO_RUN_TEST(TestRejectedKeys())
  GO_RUN_TEST(TestRenderFactors())
  GO_RUN_TEST(TestCacheRoundTrip())
  GO_RUN_TEST(TestHashFollowsCompression())
}
