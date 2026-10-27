/*
 * Copyright 2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestOrganSettingsTremulantsTab.h"

#include <cmath>
#include <memory>
#include <set>
#include <vector>

#include "config/GOConfig.h"
#include "model/GOOrganModel.h"
#include "model/GOTremulant.h"

#include "GOTestScope.h"
#include "testing/model/GOTestTremulantFixture.h"

const std::string GOTestOrganSettingsTremulantsTab::TEST_NAME
  = "GOTestOrganSettingsTremulantsTab";

namespace {
/** An organ model that records the calls of RebuildTremulantWaveForms() */
class RecordingOrganModel : public GOOrganModel {
public:
  std::vector<std::set<unsigned>> m_RebuildCalls;

  RecordingOrganModel(GOConfig &config) : GOOrganModel(config) {}

  void RebuildTremulantWaveForms(
    const std::set<unsigned> &tremulantIndices) override {
    m_RebuildCalls.push_back(tremulantIndices);
  }
};
} // namespace

void GOTestOrganSettingsTremulantsTab::TestParseValid() {
  Tab::EditTexts texts;
  GOTremulant::SynthParamChanges changes;
  Tab::ParseError error{};

  GOAssert(
    Tab::parseEdits(texts, changes, error),
    "no modified edits: nothing to parse");
  GOAssert(
    !changes.periodMs && !changes.ampModDepthPcnt && !changes.pitchModDepthCents
      && !changes.pitchModPhaseDegrees && !changes.startTimeMs
      && !changes.stopTimeMs,
    "no modified edits: no values");

  texts.periodMs = wxT("250");
  texts.pitchModDepthCents = wxT("12.5");
  texts.pitchModPhaseDegrees = wxT("-90");
  texts.stopTimeMs = wxT("0");
  GOAssert(Tab::parseEdits(texts, changes, error), "valid texts");
  GOAssert(changes.periodMs && *changes.periodMs == 250, "period is parsed");
  GOAssert(
    changes.pitchModDepthCents && *changes.pitchModDepthCents == 12.5f,
    "pitch depth is parsed");
  GOAssert(
    changes.pitchModPhaseDegrees && *changes.pitchModPhaseDegrees == -90,
    "pitch phase is parsed");
  GOAssert(
    changes.stopTimeMs && *changes.stopTimeMs == 0.0f, "stop time is parsed");
  GOAssert(
    !changes.ampModDepthPcnt && !changes.startTimeMs,
    "the absent texts stay absent");
}

void GOTestOrganSettingsTremulantsTab::TestParseRangeBorders() {
  GOTremulant::SynthParamChanges changes;
  Tab::ParseError error{};
  Tab::EditTexts texts;

  texts.periodMs = wxT("32");
  texts.pitchModPhaseDegrees = wxT("-180");
  texts.pitchModDepthCents = wxT("0");
  GOAssert(Tab::parseEdits(texts, changes, error), "the lower borders");
  GOAssert(
    changes.periodMs && *changes.periodMs == 32 && changes.pitchModPhaseDegrees
      && *changes.pitchModPhaseDegrees == -180 && changes.pitchModDepthCents
      && *changes.pitchModDepthCents == 0.0f,
    "the lower borders are parsed as they are");
  texts.periodMs = wxT("441000");
  texts.pitchModPhaseDegrees = wxT("359");
  texts.pitchModDepthCents = wxT("2400");
  GOAssert(Tab::parseEdits(texts, changes, error), "the upper borders");
  GOAssert(
    changes.periodMs && *changes.periodMs == 441000
      && changes.pitchModPhaseDegrees && *changes.pitchModPhaseDegrees == 359
      && changes.pitchModDepthCents && *changes.pitchModDepthCents == 2400.0f,
    "the upper borders are parsed as they are");

  texts = Tab::EditTexts();
  texts.periodMs = wxT("31");
  GOAssert(
    !Tab::parseEdits(texts, changes, error)
      && error.field == Tab::EditField::PERIOD_MS
      && error.problem == Tab::ParseProblem::OUT_OF_RANGE
      && error.rangeMin == 32.0 && error.rangeMax == 441000.0,
    "below the period range");
  texts.periodMs = wxT("441001");
  GOAssert(!Tab::parseEdits(texts, changes, error), "above the period range");

  texts = Tab::EditTexts();
  texts.pitchModPhaseDegrees = wxT("-181");
  GOAssert(!Tab::parseEdits(texts, changes, error), "below the phase range");
  texts.pitchModPhaseDegrees = wxT("360");
  GOAssert(!Tab::parseEdits(texts, changes, error), "above the phase range");

  texts = Tab::EditTexts();
  texts.pitchModDepthCents = wxT("2400.5");
  GOAssert(
    !Tab::parseEdits(texts, changes, error)
      && error.problem == Tab::ParseProblem::OUT_OF_RANGE,
    "above the pitch depth range by a fraction");
  texts.pitchModDepthCents = wxT("-0.5");
  GOAssert(!Tab::parseEdits(texts, changes, error), "below the pitch depth");
}

void GOTestOrganSettingsTremulantsTab::TestParseProblems() {
  GOTremulant::SynthParamChanges changes;
  Tab::ParseError error{};
  Tab::EditTexts texts;

  texts.periodMs = wxT("abc");
  GOAssert(
    !Tab::parseEdits(texts, changes, error)
      && error.problem == Tab::ParseProblem::NOT_A_NUMBER,
    "not a number");
  texts.periodMs = wxT("");
  GOAssert(
    !Tab::parseEdits(texts, changes, error)
      && error.problem == Tab::ParseProblem::NOT_A_NUMBER,
    "an empty text of a modified edit is not a number");
  texts.periodMs = wxT("100.5");
  GOAssert(
    !Tab::parseEdits(texts, changes, error)
      && error.problem == Tab::ParseProblem::NOT_AN_INTEGER,
    "a fraction in an integer edit");

  // NaN is not within any range; a float edit must not store it
  texts = Tab::EditTexts();
  texts.pitchModDepthCents = wxT("nan");
  GOAssert(
    !Tab::parseEdits(texts, changes, error) && !changes.pitchModDepthCents,
    "NaN in a float edit");
  texts = Tab::EditTexts();
  texts.startTimeMs = wxT("inf");
  GOAssert(!Tab::parseEdits(texts, changes, error), "inf in a float edit");
  texts = Tab::EditTexts();
  texts.ampModDepthPcnt = wxT("nan");
  GOAssert(!Tab::parseEdits(texts, changes, error), "NaN in an integer edit");
}

void GOTestOrganSettingsTremulantsTab::TestParseReportsFirstError() {
  GOTremulant::SynthParamChanges changes;
  Tab::ParseError error{};
  Tab::EditTexts texts;

  texts.periodMs = wxT("abc");
  texts.ampModDepthPcnt = wxT("200");
  texts.stopTimeMs = wxT("-1");
  GOAssert(
    !Tab::parseEdits(texts, changes, error)
      && error.field == Tab::EditField::PERIOD_MS,
    "the period is the first invalid edit");

  texts.periodMs = wxT("100");
  GOAssert(
    !Tab::parseEdits(texts, changes, error)
      && error.field == Tab::EditField::AMP_MOD_DEPTH_PCNT
      && error.problem == Tab::ParseProblem::OUT_OF_RANGE,
    "the amplitude depth is the first invalid edit");

  texts.ampModDepthPcnt = wxT("50");
  GOAssert(
    !Tab::parseEdits(texts, changes, error)
      && error.field == Tab::EditField::STOP_TIME_MS,
    "the stop time is the first invalid edit");
}

void GOTestOrganSettingsTremulantsTab::TestApplyChanges() {
  GOConfig config(TEST_NAME, "");
  RecordingOrganModel model(config);

  GOTremulant *const pFirst = model.GetTremulant(model.AddTremulant(
    init_tremulant_from_cmb(model, "PeriodMs=100\nAmpModDepthPcnt=20\n")));
  GOTremulant *const pSecond = model.GetTremulant(model.AddTremulant(
    init_tremulant_from_cmb(model, "PeriodMs=300\nAmpModDepthPcnt=30\n")));
  GOTremulant *const pThird = model.GetTremulant(model.AddTremulant(
    init_tremulant_from_cmb(model, "PeriodMs=500\nAmpModDepthPcnt=40\n")));
  GOTremulant::SynthParamChanges changes;

  Tab::applyChanges(model, {0, 1}, changes);
  GOAssert(model.m_RebuildCalls.empty(), "nothing is present: no hook call");

  changes.ampModDepthPcnt = 70;
  Tab::applyChanges(model, {0, 1}, changes);
  GOAssert(
    pFirst->GetAmpModDepthPcnt() == 70 && pSecond->GetAmpModDepthPcnt() == 70,
    "the present value is written to every selected tremulant");
  GOAssert(
    pFirst->GetPeriodMs() == 100 && pSecond->GetPeriodMs() == 300,
    "the other parameters of each stay its own");
  GOAssert(
    pThird->GetAmpModDepthPcnt() == 40 && pThird->GetPeriodMs() == 500,
    "an unselected tremulant is not touched");
  GOAssert(
    model.m_RebuildCalls.size() == 1
      && model.m_RebuildCalls[0] == std::set<unsigned>({0, 1}),
    "the hook is called once with both changed tremulants");

  Tab::applyChanges(model, {0, 1}, changes);
  GOAssert(
    model.m_RebuildCalls.size() == 1,
    "the same values: nothing has changed, no hook call");

  pSecond->SetAmpModDepthPcnt(10);
  Tab::applyChanges(model, {0, 1}, changes);
  GOAssert(
    model.m_RebuildCalls.size() == 2
      && model.m_RebuildCalls[1] == std::set<unsigned>({1}),
    "the hook gets only the tremulant that has really changed");
}

void GOTestOrganSettingsTremulantsTab::TestResetToDefault() {
  GOConfig config(TEST_NAME, "");
  RecordingOrganModel model(config);

  GOTremulant *const pChanged = model.GetTremulant(model.AddTremulant(
    init_tremulant_from_cmb(model, "PeriodMs=100\nPitchModDepthCents=7\n")));
  GOTremulant *const pDefault = model.GetTremulant(
    model.AddTremulant(init_tremulant_from_cmb(model, "")));
  GOTremulant *const pUnselected = model.GetTremulant(
    model.AddTremulant(init_tremulant_from_cmb(model, "PeriodMs=500\n")));

  Tab::resetToDefault(model, {0, 1});
  GOAssert(
    pChanged->GetPeriodMs() == pDefault->GetPeriodMs()
      && pChanged->GetPitchModDepthCents() == 0.0f,
    "the selected tremulant gets the default parameters");
  GOAssert(
    pUnselected->GetPeriodMs() == 500, "an unselected tremulant is untouched");
  GOAssert(
    model.m_RebuildCalls.size() == 1
      && model.m_RebuildCalls[0] == std::set<unsigned>({0}),
    "the hook is called once with the only changed tremulant");

  Tab::resetToDefault(model, {0, 1});
  GOAssert(
    model.m_RebuildCalls.size() == 1,
    "already default: nothing has changed, no hook call");
}

void GOTestOrganSettingsTremulantsTab::run() {
  GO_RUN_TEST(TestParseValid())
  GO_RUN_TEST(TestParseRangeBorders())
  GO_RUN_TEST(TestParseProblems())
  GO_RUN_TEST(TestParseReportsFirstError())
  GO_RUN_TEST(TestApplyChanges())
  GO_RUN_TEST(TestResetToDefault())
}
