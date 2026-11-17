/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestTremulantFixture.h"

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

#include "config/GOConfigFileReader.h"
#include "config/GOConfigReader.h"
#include "config/GOConfigReaderDB.h"
#include "model/GOTremulant.h"
#include "testing/sound/GOTestWaveFileBuilder.h"

#include "GOWaveLoop.h"

const wxString TEST_TREMULANT_GROUP = wxT("Tremulant001");

GOTestTremulantModel::GOTestTremulantModel()
  : config("GOTestTremulant", ""), organModel(config) {}

const std::string GOTestTremulantModel::LfoFile::FILE_NAME = "lfo.wav";
const std::string GOTestTremulantModel::LfoFile::ODF_HEAD
  = "[Tremulant001]\nName=Test\nTremulantType=Lfo\nPeriod=100\n"
    "StartRate=10\nStopRate=10\nLfoWaveFile="
  + FILE_NAME + "\n";

GOTestTremulantModel::LfoFile::LfoFile(const GOConfig &config)
  : fileStore(config) {
  static std::atomic<unsigned> counter(0);

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
    wxString(m_Path.string()) + wxT("/") + wxString(FILE_NAME),
    true,
    32,
    2,
    items.data(),
    N_FILE_FRAMES,
    &loop);
  fileStore.SetDirectory(wxString(m_Path.string()));
}

GOTestTremulantModel::LfoFile::~LfoFile() {
  std::filesystem::remove_all(m_Path);
}

/**
 * Writes the text to a temporary file, reads it with the reader and removes
 * the file.
 */
static void read_text(
  const std::string &text, GOConfigFileReader &outFileReader) {
  static std::atomic<unsigned> counter(0);
  const std::filesystem::path path = std::filesystem::temp_directory_path()
    / std::format("GOTestTremulant-{}.ini", counter++);

  std::ofstream(path) << text;
  outFileReader.Read(wxString(path.string()));
  std::filesystem::remove(path);
}

std::unique_ptr<GOTremulant> load_tremulant_from_text(
  GOOrganModel &model, const std::string &odfText, const std::string &cmbText) {
  GOConfigFileReader odfFile;
  GOConfigReaderDB ini;

  // A drawstop requires DefaultToEngaged, which has nothing to do with the
  // tests, so it is appended to the (last) group
  read_text(odfText + "DefaultToEngaged=N\n", odfFile);
  ini.ReadData(odfFile, ODFSetting, false);
  if (!cmbText.empty()) {
    GOConfigFileReader cmbFile;

    read_text(cmbText, cmbFile);
    ini.ReadData(cmbFile, CMBSetting, false);
  }

  GOConfigReader cfg(ini, false, false);
  std::unique_ptr<GOTremulant> oTremulant
    = std::make_unique<GOTremulant>(model);

  oTremulant->Load(cfg, TEST_TREMULANT_GROUP);
  return oTremulant;
}

std::unique_ptr<GOTremulant> init_tremulant_from_cmb(
  GOOrganModel &model, const std::string &cmbText) {
  GOConfigFileReader cmbFile;
  GOConfigReaderDB ini;

  // A drawstop requires DefaultToEngaged, which has nothing to do with the
  // tests, so it is appended to the (last) group
  read_text("[Tremulant001]\n" + cmbText + "DefaultToEngaged=N\n", cmbFile);
  ini.ReadData(cmbFile, CMBSetting, false);

  GOConfigReader cfg(ini, false, false);
  std::unique_ptr<GOTremulant> oTremulant
    = std::make_unique<GOTremulant>(model);

  oTremulant->Init(cfg, TEST_TREMULANT_GROUP, wxT("Test"));
  return oTremulant;
}

std::unique_ptr<GOTremulant> load_tremulant(
  GOOrganModel &model,
  const std::string &type,
  int periodMs,
  int startRate,
  int stopRate,
  int ampDepthPcnt) {
  return load_tremulant_from_text(
    model,
    std::format(
      "[Tremulant001]\nName=Test\nTremulantType={}\nPeriod={}\n"
      "StartRate={}\nStopRate={}\nAmpModDepth={}\n",
      type,
      periodMs,
      startRate,
      stopRate,
      ampDepthPcnt),
    "");
}
