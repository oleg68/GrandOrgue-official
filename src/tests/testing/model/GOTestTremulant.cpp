/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestTremulant.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "config/GOConfigFileReader.h"
#include "config/GOConfigFileWriter.h"
#include "config/GOConfigWriter.h"
#include "model/GOTremulant.h"

#include "GOSaveableObject.h"

#include "GOTestScope.h"
#include "GOTestTremulantFixture.h"

const std::string GOTestTremulant::TEST_NAME = "GOTestTremulant";

static constexpr float TOLERANCE = 1e-4f;

static bool are_close(float a, float b) {
  return std::fabs(a - b) <= TOLERANCE;
}

static float max_of(const std::vector<float> &values) {
  return *std::max_element(values.begin(), values.end());
}

static float min_of(const std::vector<float> &values) {
  return *std::min_element(values.begin(), values.end());
}

/**
 * @return whether loading the ODF text throws
 */
static bool does_load_throw(const std::string &odfText) {
  GOTestTremulantModel model;
  bool isThrown = false;

  try {
    load_tremulant_from_text(model.organModel, odfText, "");
  } catch (const wxString &) {
    isThrown = true;
  }
  return isThrown;
}

void GOTestTremulant::TestLoadRateConversion() {
  GOTestTremulantModel model;
  GOTremulant::Description desc;

  load_tremulant(model.organModel, "Synth", 100, 4, 8, 20)->Render(48000, desc);
  GOAssert(desc.nStartFrames == 12000, "StartRate 4 must be 250 ms");
  GOAssert(desc.nStopFrames == 6000, "StopRate 8 must be 125 ms");

  load_tremulant(model.organModel, "Synth", 100, 3, 3, 20)->Render(48000, desc);
  GOAssert(desc.nStartFrames == 15984, "StartRate 3 must be 333 ms (rounded)");
}

void GOTestTremulant::TestLoadRequiredKeys() {
  const std::string head = "[Tremulant001]\nName=Test\n";

  GOAssert(
    !does_load_throw(
      head + "Period=100\nStartRate=4\nStopRate=8\nAmpModDepth=20\n"),
    "all the keys given must load");
  GOAssert(
    does_load_throw(head + "StartRate=4\nStopRate=8\nAmpModDepth=20\n"),
    "a missing Period must throw");
  GOAssert(
    does_load_throw(head + "Period=100\nStopRate=8\nAmpModDepth=20\n"),
    "a missing StartRate must throw");
  GOAssert(
    does_load_throw(head + "Period=100\nStartRate=4\nAmpModDepth=20\n"),
    "a missing StopRate must throw");
  GOAssert(
    does_load_throw(head + "Period=100\nStartRate=4\nStopRate=8\n"),
    "a missing AmpModDepth must throw");
}

void GOTestTremulant::TestLoadTypes() {
  GOTestTremulantModel model;

  GOAssert(
    load_tremulant_from_text(
      model.organModel,
      "[Tremulant001]\nName=Test\nPeriod=100\nStartRate=4\nStopRate=8\n"
      "AmpModDepth=20\n",
      "")
        ->GetTremulantType()
      == GOSynthTrem,
    "the default type must be Synth");
  GOAssert(
    load_tremulant(model.organModel, "Synth", 100, 4, 8, 20)->GetTremulantType()
      == GOSynthTrem,
    "Synth must be recognised");
  GOAssert(
    load_tremulant_from_text(
      model.organModel, "[Tremulant001]\nName=Test\nTremulantType=Wave\n", "")
        ->GetTremulantType()
      == GOWavTrem,
    "Wave must be recognised");
}

/**
 * Saves a tremulant to a temporary .cmb file and reads the file back.
 */
static void save_and_read(
  GOTremulant &tremulant, GOConfigFileReader &outReader) {
  GOConfigFileWriter file;
  GOConfigWriter writer(file, false);
  GOSaveableObject &saveable = tremulant;
  const std::filesystem::path path
    = std::filesystem::temp_directory_path() / "GOTestTremulant-save.cmb";

  saveable.Save(writer);
  file.Save(wxString(path.string()), false);
  outReader.Read(wxString(path.string()));
  std::filesystem::remove(path);
}

void GOTestTremulant::TestInitDefaults() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = init_tremulant_from_cmb(model.organModel, "");
  GOTremulant::Description desc;

  oTremulant->Render(48000, desc);
  GOAssert(!oTremulant->IsOdfDefined(), "Init() must make it internal");
  GOAssert(oTremulant->GetTremulantType() == GOSynthTrem, "the type is Synth");
  GOAssert(desc.amp.size() == 9600, "the default period is 200 ms");
  GOAssert(desc.nStartFrames == 14400, "the default start time is 300 ms");
  GOAssert(desc.nStopFrames == 14400, "the default stop time is 300 ms");
  GOAssert(
    std::abs(min_of(desc.amp) - 0.5f) < 1e-3f
      && std::abs(max_of(desc.amp) - 1.5f) < 1e-3f,
    "the default amplitude depth is 50%");
  GOAssert(desc.pitchCents.empty(), "the default has no pitch modulation");
}

void GOTestTremulant::TestLoadFromCmbFallback() {
  GOTestTremulantModel model;
  GOTremulant::Description desc;

  // Only two keys: the others get their defaults
  init_tremulant_from_cmb(model.organModel, "PeriodMs=50\nAmpModDepthPcnt=10\n")
    ->Render(48000, desc);
  GOAssert(desc.amp.size() == 2400, "the .cmb period must be used");
  GOAssert(are_close(max_of(desc.amp), 1.1f), "the .cmb depth must be used");
  GOAssert(desc.nStartFrames == 14400, "a missing start time is the default");
  GOAssert(desc.nStopFrames == 14400, "a missing stop time is the default");
  GOAssert(desc.pitchCents.empty(), "a missing pitch depth is 0");

  // All the keys
  init_tremulant_from_cmb(
    model.organModel,
    "PeriodMs=50\nStartTimeMs=100\nStopTimeMs=200\nAmpModDepthPcnt=10\n"
    "PitchModDepthCents=25\nPitchModPhaseDegrees=90\n")
    ->Render(48000, desc);
  GOAssert(desc.nStartFrames == 4800, "the .cmb start time must be used");
  GOAssert(desc.nStopFrames == 9600, "the .cmb stop time must be used");
  GOAssert(desc.pitchCents.size() == 2400, "the .cmb pitch must be used");
  GOAssert(are_close(desc.pitchCents[0], 25.0f), "the .cmb phase must be used");
}

void GOTestTremulant::TestInitDoesNotMarkModified() {
  GOTestTremulantModel model;

  init_tremulant_from_cmb(
    model.organModel, "PeriodMs=50\nPitchModDepthCents=25\n");
  GOAssert(
    !model.organModel.IsOrganModelModified(),
    "Init() must not mark the organ model as modified");
}

void GOTestTremulant::TestInitSaveRoundTrip() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = init_tremulant_from_cmb(model.organModel, "");

  oTremulant->SetPeriodMs(70);
  oTremulant->SetStartTimeMs(120);
  oTremulant->SetStopTimeMs(80);
  oTremulant->SetAmpModDepthPcnt(15);
  oTremulant->SetPitchModDepthCents(30.0f);
  oTremulant->SetPitchModPhaseDegrees(-45);

  GOConfigFileReader reader;

  save_and_read(*oTremulant, reader);
  GOAssert(
    reader.getEntry(TEST_TREMULANT_GROUP, wxT("PeriodMs")) == wxT("70"),
    "PeriodMs must be saved");
  GOAssert(
    reader.getEntry(TEST_TREMULANT_GROUP, wxT("AmpModDepthPcnt")) == wxT("15"),
    "AmpModDepthPcnt must be saved");
  GOAssert(
    reader.getEntry(TEST_TREMULANT_GROUP, wxT("PitchModPhaseDegrees"))
      == wxT("-45"),
    "PitchModPhaseDegrees must be saved");

  // Initialising from the saved values gives the same wave form
  GOTremulant::Description descOrig;
  GOTremulant::Description descLoaded;

  oTremulant->Render(48000, descOrig);
  init_tremulant_from_cmb(
    model.organModel,
    "PeriodMs=70\nStartTimeMs=120\nStopTimeMs=80\nAmpModDepthPcnt=15\n"
    "PitchModDepthCents=30\nPitchModPhaseDegrees=-45\n")
    ->Render(48000, descLoaded);
  GOAssert(descOrig.amp == descLoaded.amp, "the amplitude must round-trip");
  GOAssert(
    descOrig.pitchCents == descLoaded.pitchCents, "the pitch must round-trip");
  GOAssert(
    descOrig.nStartFrames == descLoaded.nStartFrames
      && descOrig.nStopFrames == descLoaded.nStopFrames,
    "the ramps must round-trip");
}

void GOTestTremulant::TestOdfIgnoresCmb() {
  GOTestTremulantModel model;
  GOTremulant::Description desc;
  std::unique_ptr<GOTremulant> oTremulant = load_tremulant_from_text(
    model.organModel,
    "[Tremulant001]\nName=Test\nPeriod=100\nStartRate=4\nStopRate=8\n"
    "AmpModDepth=20\n",
    "[Tremulant001]\nPeriodMs=50\nStartTimeMs=100\nStopTimeMs=200\n"
    "AmpModDepthPcnt=10\nPitchModDepthCents=25\nPitchModPhaseDegrees=90\n");

  GOAssert(oTremulant->IsOdfDefined(), "Load() must make it ODF-defined");
  oTremulant->Render(48000, desc);
  GOAssert(desc.amp.size() == 4800, "the ODF period must be used");
  GOAssert(are_close(max_of(desc.amp), 1.2f), "the ODF depth must be used");
  GOAssert(desc.nStartFrames == 12000, "the ODF start rate must be used");
  GOAssert(desc.nStopFrames == 6000, "the ODF stop rate must be used");
  GOAssert(desc.pitchCents.empty(), "the .cmb pitch must be ignored");

  // The parameters are not saved either
  GOConfigFileReader reader;

  save_and_read(*oTremulant, reader);
  GOAssert(
    reader.getEntry(TEST_TREMULANT_GROUP, wxT("PeriodMs")).IsEmpty()
      && reader.getEntry(TEST_TREMULANT_GROUP, wxT("StartTimeMs")).IsEmpty()
      && reader.getEntry(TEST_TREMULANT_GROUP, wxT("StopTimeMs")).IsEmpty()
      && reader.getEntry(TEST_TREMULANT_GROUP, wxT("AmpModDepthPcnt")).IsEmpty()
      && reader.getEntry(TEST_TREMULANT_GROUP, wxT("PitchModDepthCents"))
           .IsEmpty()
      && reader.getEntry(TEST_TREMULANT_GROUP, wxT("PitchModPhaseDegrees"))
           .IsEmpty(),
    "an ODF tremulant must not save its parameters");
}

void GOTestTremulant::TestLoadDoesNotMarkModified() {
  GOTestTremulantModel model;

  load_tremulant(model.organModel, "Synth", 100, 4, 8, 20);
  GOAssert(
    !model.organModel.IsOrganModelModified(),
    "loading must not mark the organ model as modified");
}

void GOTestTremulant::TestSettersMarkModified() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = load_tremulant(model.organModel, "Synth", 100, 4, 8, 20);

  // The same values as loaded
  oTremulant->SetPeriodMs(100);
  oTremulant->SetStartTimeMs(250);
  oTremulant->SetStopTimeMs(125);
  oTremulant->SetAmpModDepthPcnt(20);
  oTremulant->SetPitchModDepthCents(0.0f);
  oTremulant->SetPitchModPhaseDegrees(0);
  GOAssert(
    !model.organModel.IsOrganModelModified(),
    "setting the same values must not mark the organ model as modified");

  oTremulant->SetPeriodMs(101);
  GOAssert(model.organModel.IsOrganModelModified(), "SetPeriodMs");
  model.organModel.ResetOrganModelModified();
  oTremulant->SetStartTimeMs(251);
  GOAssert(model.organModel.IsOrganModelModified(), "SetStartTimeMs");
  model.organModel.ResetOrganModelModified();
  oTremulant->SetStopTimeMs(126);
  GOAssert(model.organModel.IsOrganModelModified(), "SetStopTimeMs");
  model.organModel.ResetOrganModelModified();
  oTremulant->SetAmpModDepthPcnt(21);
  GOAssert(model.organModel.IsOrganModelModified(), "SetAmpModDepthPcnt");
  model.organModel.ResetOrganModelModified();
  oTremulant->SetPitchModDepthCents(1.0f);
  GOAssert(model.organModel.IsOrganModelModified(), "SetPitchModDepthCents");
  model.organModel.ResetOrganModelModified();
  oTremulant->SetPitchModPhaseDegrees(1);
  GOAssert(model.organModel.IsOrganModelModified(), "SetPitchModPhaseDegrees");
}

void GOTestTremulant::TestGettersReturnSetValues() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = init_tremulant_from_cmb(model.organModel, "");

  oTremulant->SetPeriodMs(123);
  oTremulant->SetStartTimeMs(11);
  oTremulant->SetStopTimeMs(22);
  oTremulant->SetAmpModDepthPcnt(33);
  oTremulant->SetPitchModDepthCents(44.0f);
  oTremulant->SetPitchModPhaseDegrees(-55);
  GOAssert(oTremulant->GetPeriodMs() == 123, "GetPeriodMs");
  GOAssert(oTremulant->GetStartTimeMs() == 11, "GetStartTimeMs");
  GOAssert(oTremulant->GetStopTimeMs() == 22, "GetStopTimeMs");
  GOAssert(oTremulant->GetAmpModDepthPcnt() == 33, "GetAmpModDepthPcnt");
  GOAssert(oTremulant->GetPitchModDepthCents() == 44.0f, "GetPitchModDepth");
  GOAssert(oTremulant->GetPitchModPhaseDegrees() == -55, "GetPitchModPhase");
}

void GOTestTremulant::TestParamRanges() {
  const GOParamRange<int> range{-2, 5};

  GOAssert(range.Contains(-2) && range.Contains(5), "both ends are included");
  GOAssert(range.Contains(0), "an inner value is contained");
  GOAssert(!range.Contains(-3) && !range.Contains(6), "outside is not");

  // A value of another type is compared as is; a NaN is never contained
  const double nan = std::nan("");
  const GOParamRange<float> floatRange{0.0f, 10.0f};

  GOAssert(
    range.Contains(2.5) && floatRange.Contains(10.0)
      && !floatRange.Contains(10.5),
    "a double value");
  GOAssert(
    !range.Contains(nan) && !floatRange.Contains(nan), "NaN is not contained");

  // The .cmb accepts the borders of the ranges
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oMin = init_tremulant_from_cmb(
    model.organModel,
    "PeriodMs=32\nStartTimeMs=0\nStopTimeMs=0\nAmpModDepthPcnt=0\n"
    "PitchModDepthCents=0\nPitchModPhaseDegrees=-180\n");
  std::unique_ptr<GOTremulant> oMax = init_tremulant_from_cmb(
    model.organModel,
    "PeriodMs=441000\nStartTimeMs=10000\nStopTimeMs=10000\n"
    "AmpModDepthPcnt=100\nPitchModDepthCents=2400\nPitchModPhaseDegrees=359\n");

  GOAssert(
    oMin->GetPeriodMs() == GOTremulant::PERIOD_MS_RANGE.m_Min
      && oMax->GetPeriodMs() == GOTremulant::PERIOD_MS_RANGE.m_Max,
    "period");
  GOAssert(
    oMin->GetStartTimeMs() == GOTremulant::START_TIME_MS_RANGE.m_Min
      && oMax->GetStartTimeMs() == GOTremulant::START_TIME_MS_RANGE.m_Max,
    "start time");
  GOAssert(
    oMin->GetStopTimeMs() == GOTremulant::STOP_TIME_MS_RANGE.m_Min
      && oMax->GetStopTimeMs() == GOTremulant::STOP_TIME_MS_RANGE.m_Max,
    "stop time");
  GOAssert(
    oMin->GetAmpModDepthPcnt() == GOTremulant::AMP_MOD_DEPTH_PCNT_RANGE.m_Min
      && oMax->GetAmpModDepthPcnt()
        == GOTremulant::AMP_MOD_DEPTH_PCNT_RANGE.m_Max,
    "amplitude depth");
  GOAssert(
    oMin->GetPitchModDepthCents()
        == GOTremulant::PITCH_MOD_DEPTH_CENTS_RANGE.m_Min
      && oMax->GetPitchModDepthCents()
        == GOTremulant::PITCH_MOD_DEPTH_CENTS_RANGE.m_Max,
    "pitch depth");
  GOAssert(
    oMin->GetPitchModPhaseDegrees()
        == GOTremulant::PITCH_MOD_PHASE_DEGREES_RANGE.m_Min
      && oMax->GetPitchModPhaseDegrees()
        == GOTremulant::PITCH_MOD_PHASE_DEGREES_RANGE.m_Max,
    "pitch phase");
}

void GOTestTremulant::TestApplySynthParamChanges() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = init_tremulant_from_cmb(model.organModel, "");
  GOTremulant::SynthParamChanges changes;

  GOAssert(
    !oTremulant->ApplySynthParamChanges(changes)
      && !model.organModel.IsOrganModelModified(),
    "no values present: nothing changes");

  changes.periodMs = oTremulant->GetPeriodMs();
  changes.pitchModDepthCents = oTremulant->GetPitchModDepthCents();
  GOAssert(
    !oTremulant->ApplySynthParamChanges(changes)
      && !model.organModel.IsOrganModelModified(),
    "the same values: nothing changes");

  changes.periodMs = 300;
  changes.stopTimeMs = 77;
  GOAssert(
    oTremulant->ApplySynthParamChanges(changes)
      && model.organModel.IsOrganModelModified(),
    "different values: changed and modified");
  GOAssert(
    oTremulant->GetPeriodMs() == 300 && oTremulant->GetStopTimeMs() == 77,
    "the present values are applied");
  GOAssert(
    oTremulant->GetStartTimeMs() == 300
      && oTremulant->GetAmpModDepthPcnt() == 50
      && oTremulant->GetPitchModDepthCents() == 0.0f
      && oTremulant->GetPitchModPhaseDegrees() == 0,
    "the absent values are kept");
}

void GOTestTremulant::TestResetSynthParamsToDefault() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = init_tremulant_from_cmb(model.organModel, "");
  GOTremulant::Description defaultDesc;
  GOTremulant::Description desc;

  oTremulant->Render(48000, defaultDesc);
  GOAssert(
    !oTremulant->ResetSynthParamsToDefault()
      && !model.organModel.IsOrganModelModified(),
    "the defaults: nothing changes");

  oTremulant->SetPeriodMs(300);
  oTremulant->SetStartTimeMs(1);
  oTremulant->SetStopTimeMs(2);
  oTremulant->SetAmpModDepthPcnt(3);
  oTremulant->SetPitchModDepthCents(4.0f);
  oTremulant->SetPitchModPhaseDegrees(5);
  model.organModel.ResetOrganModelModified();
  GOAssert(oTremulant->ResetSynthParamsToDefault(), "changed parameters");
  GOAssert(model.organModel.IsOrganModelModified(), "the organ is modified");
  GOAssert(
    !oTremulant->ResetSynthParamsToDefault(),
    "the second call changes nothing");

  oTremulant->Render(48000, desc);
  GOAssert(
    desc.amp == defaultDesc.amp && desc.pitchCents == defaultDesc.pitchCents
      && desc.nStartFrames == defaultDesc.nStartFrames
      && desc.nStopFrames == defaultDesc.nStopFrames,
    "the same wave form as after Init()");
}

void GOTestTremulant::TestRenderLoopLength() {
  GOTestTremulantModel model;
  GOTremulant::Description desc;

  load_tremulant(model.organModel, "Synth", 100, 4, 8, 20)->Render(48000, desc);
  GOAssert(desc.amp.size() == 4800, "100 ms at 48 kHz must be 4800 frames");
  load_tremulant(model.organModel, "Synth", 100, 4, 8, 20)->Render(44100, desc);
  GOAssert(desc.amp.size() == 4410, "100 ms at 44.1 kHz must be 4410 frames");
  load_tremulant(model.organModel, "Synth", 33, 4, 8, 20)->Render(44100, desc);
  GOAssert(
    desc.amp.size() == 1455, "33 ms at 44.1 kHz must be rounded to 1455");
}

void GOTestTremulant::TestRenderAmplitude() {
  GOTestTremulantModel model;
  GOTremulant::Description desc;

  load_tremulant(model.organModel, "Synth", 100, 4, 8, 20)->Render(48000, desc);
  GOAssert(are_close(max_of(desc.amp), 1.2f), "depth 20 must peak at 1.2");
  GOAssert(are_close(min_of(desc.amp), 0.8f), "depth 20 must dip to 0.8");

  load_tremulant(model.organModel, "Synth", 100, 4, 8, 50)->Render(48000, desc);
  GOAssert(are_close(max_of(desc.amp), 1.5f), "depth 50 must peak at 1.5");
  GOAssert(are_close(min_of(desc.amp), 0.5f), "depth 50 must dip to 0.5");
}

void GOTestTremulant::TestRenderPitch() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = load_tremulant(model.organModel, "Synth", 100, 4, 8, 20);
  GOTremulant::Description desc;
  const unsigned nLoopFrames = 4800;

  oTremulant->Render(48000, desc);
  GOAssert(desc.pitchCents.empty(), "the pitch must be empty by default");

  oTremulant->SetPitchModDepthCents(25.0f);
  oTremulant->Render(48000, desc);
  GOAssert(desc.pitchCents.size() == nLoopFrames, "the pitch length");
  GOAssert(are_close(max_of(desc.pitchCents), 25.0f), "the pitch peak");
  GOAssert(are_close(desc.pitchCents[0], 0.0f), "phase 0 starts at 0");

  oTremulant->SetPitchModPhaseDegrees(90);
  oTremulant->Render(48000, desc);

  const float lead90 = desc.pitchCents[0];

  GOAssert(are_close(lead90, 25.0f), "phase 90 must lead to the peak");

  oTremulant->SetPitchModPhaseDegrees(-90);
  oTremulant->Render(48000, desc);

  const std::vector<float> minus90 = desc.pitchCents;

  oTremulant->SetPitchModPhaseDegrees(270);
  oTremulant->Render(48000, desc);
  GOAssert(minus90 == desc.pitchCents, "phase -90 must equal 270");

  oTremulant->SetPitchModPhaseDegrees(360);
  oTremulant->Render(48000, desc);

  const std::vector<float> phase360 = desc.pitchCents;

  oTremulant->SetPitchModPhaseDegrees(0);
  oTremulant->Render(48000, desc);
  GOAssert(phase360 == desc.pitchCents, "phase 360 must equal 0");
}

void GOTestTremulant::TestRenderUsesSetters() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = load_tremulant(model.organModel, "Synth", 100, 4, 8, 20);
  GOTremulant::Description desc;

  oTremulant->SetPeriodMs(50);
  oTremulant->SetStartTimeMs(10);
  oTremulant->SetStopTimeMs(0);
  oTremulant->SetAmpModDepthPcnt(0);
  oTremulant->Render(48000, desc);
  GOAssert(desc.amp.size() == 2400, "SetPeriodMs");
  GOAssert(desc.nStartFrames == 480, "SetStartTimeMs");
  GOAssert(desc.nStopFrames == 0, "a zero stop time must give no ramp");
  GOAssert(
    min_of(desc.amp) == 1.0f && max_of(desc.amp) == 1.0f,
    "a zero depth must give a flat 1.0");
}

void GOTestTremulant::TestRenderOverwritesDescription() {
  GOTestTremulantModel model;
  std::unique_ptr<GOTremulant> oTremulant
    = load_tremulant(model.organModel, "Synth", 100, 4, 8, 20);
  GOTremulant::Description desc;

  desc.amp.assign(100000, 7.0f);
  desc.pitchCents.assign(100000, 7.0f);
  desc.nStartFrames = 1;
  desc.nStopFrames = 1;
  oTremulant->Render(48000, desc);
  GOAssert(desc.amp.size() == 4800, "the old amplitude must be replaced");
  GOAssert(desc.pitchCents.empty(), "a stale pitch must be emptied");
  GOAssert(desc.nStartFrames == 12000, "the old start must be replaced");
  GOAssert(desc.nStopFrames == 6000, "the old stop must be replaced");
}

void GOTestTremulant::run() {
  GO_RUN_TEST(TestLoadRateConversion())
  GO_RUN_TEST(TestLoadRequiredKeys())
  GO_RUN_TEST(TestLoadTypes())
  GO_RUN_TEST(TestInitDefaults())
  GO_RUN_TEST(TestLoadFromCmbFallback())
  GO_RUN_TEST(TestInitDoesNotMarkModified())
  GO_RUN_TEST(TestInitSaveRoundTrip())
  GO_RUN_TEST(TestOdfIgnoresCmb())
  GO_RUN_TEST(TestLoadDoesNotMarkModified())
  GO_RUN_TEST(TestSettersMarkModified())
  GO_RUN_TEST(TestGettersReturnSetValues())
  GO_RUN_TEST(TestParamRanges())
  GO_RUN_TEST(TestApplySynthParamChanges())
  GO_RUN_TEST(TestResetSynthParamsToDefault())
  GO_RUN_TEST(TestRenderLoopLength())
  GO_RUN_TEST(TestRenderAmplitude())
  GO_RUN_TEST(TestRenderPitch())
  GO_RUN_TEST(TestRenderUsesSetters())
  GO_RUN_TEST(TestRenderOverwritesDescription())
}
