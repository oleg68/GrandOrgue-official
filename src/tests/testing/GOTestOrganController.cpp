/*
 * Copyright 2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestOrganController.h"

#include <set>
#include <string>
#include <vector>

#include "combinations/model/GOCombinationDefinition.h"
#include "combinations/model/GOGeneralCombination.h"
#include "combinations/model/GOSetterState.h"
#include "config/GOConfig.h"
#include "config/GOConfigFileWriter.h"
#include "config/GOConfigWriter.h"
#include "loader/GOOrganReader.h"
#include "loader/GOProgressMonitor.h"
#include "model/GOEnclosure.h"
#include "model/GOManual.h"
#include "model/GOTremulant.h"
#include "model/GOWindchest.h"

#include "GOBuffer.h"
#include "GOOrgan.h"
#include "GOOrganController.h"
#include "GOTestScope.h"

// Smallest ODF that survives LoadOrganCoreData() to completion: one
// windchest group, one manual, no stops/couplers/tremulants/switches/ranks
// (all optional, default to 0). Checked into src/tests/testing/resources
// rather than generated at runtime.
static const wxString MINIMAL_ODF_PATH
  = wxT(GO_TEST_RESOURCES_DIR "/minimal.organ");

static class GOTestProgressMonitor : public GOProgressMonitor {
public:
  void Setup(long max, const wxString &title, const wxString &msg) override {}
  void Reset(long max, const wxString &msg) override {}
  bool Update(unsigned value, const wxString &msg) override { return true; }
} s_ProgressMonitor;

GOTestOrganController::~GOTestOrganController() {}

std::string GOTestOrganController::GetName() { return name; }

void GOTestOrganController::run() {
  try {
    GO_RUN_TEST(TestOrganCoreData())
  } catch (const wxString &e) {
    throw GOTestException(
      std::string("Unexpected wxString exception: ") + e.ToStdString());
  }
}

// Deliberately does not go through the public Load()/Clear(), since those
// unconditionally call LoadObjects() (whose error path pops up a
// GOMessageBox, needing a display, when GOMetronome's sound files aren't
// installed in this environment). GOTestOrganController is a friend of
// GOOrganController precisely so this test can call the non-cache phase
// methods (LoadOrganCoreData/SaveOrganCoreData/ClearOrganCoreData)
// directly, skipping the other phases entirely.
void GOTestOrganController::TestOrganCoreData() {
  const GOOrgan organ(MINIMAL_ODF_PATH);

  GOOrganReader organReader(
    controller->m_config,
    organ,
    wxEmptyString,
    controller->m_FileStore,
    s_ProgressMonitor);

  controller->LoadOrganCoreData(organReader.GetConfigReader());
  organReader.ReportUnused();

  GOAssert(
    controller->GetOrganName() == wxT("Test Organ"),
    "LoadOrganCoreData() should populate the organ name from the ODF");
  // 1 windchest group from the ODF (NumberOfWindchestGroups=1) plus 1 more
  // that GOMetronome::createWindchest() creates to route its own sound and
  // LoadOrganCoreData() registers, regardless of the ODF's own count.
  GOAssert(
    controller->GetWindchestCount() == 2,
    "LoadOrganCoreData() should populate the ODF's windchest group plus "
    "GOMetronome's own, got: "
      + std::to_string(controller->GetWindchestCount()));

  // The internal enclosures: the master and one per windchest (including the
  // metronome's), created by the controller and not by the GUI panel.
  const unsigned nWindchests = controller->GetWindchestCount();

  GOAssert(
    controller->GetEnclosureCount() == 1 + nWindchests,
    "InitInternalEnclosures() should create the master and one enclosure per "
    "windchest, got: "
      + std::to_string(controller->GetEnclosureCount()));
  GOAssert(
    controller->GetODFEnclosureCount() == 0,
    "The internal enclosures should not be counted as ODF enclosures, got: "
      + std::to_string(controller->GetODFEnclosureCount()));
  GOAssert(
    controller->GetMasterEnclosure()
      && controller->GetMasterEnclosure()->GetName() == wxT("Master")
      && controller->GetEnclosureElement(0) == controller->GetMasterEnclosure(),
    "The master enclosure should be the first enclosure");
  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++) {
    GOEnclosure *pEnc = controller->GetInternalEnclosure(windchestI);
    const std::vector<GOEnclosure *> &windchestEncs
      = controller->GetWindchest(windchestI)->GetEnclosures();
    const std::string suffix
      = ", windchest index: " + std::to_string(windchestI);

    GOAssert(
      pEnc == controller->GetEnclosureElement(windchestI + 1),
      "Internal enclosures should follow the master in the windchest "
      "order"
        + suffix);
    GOAssert(
      pEnc->GetName() == controller->GetWindchest(windchestI)->GetName(),
      "An internal enclosure should be named after its windchest" + suffix);
    GOAssert(
      windchestEncs.size() == 2
        && windchestEncs[0] == controller->GetMasterEnclosure()
        && windchestEncs[1] == pEnc,
      "A windchest should have the master and its own enclosure" + suffix);
  }
  GOAssert(
    controller->GetInternalEnclosure(0)->GetHardName() == wxT("Windchest001"),
    "An ODF windchest without a hard name should give Windchest001");
  GOAssert(
    controller->GetInternalEnclosure(nWindchests - 1)->GetHardName()
      == wxT("Metronome"),
    "The metronome windchest should have an internal enclosure with its hard "
    "name");

  // The internal tremulants: one per windchest, after the ODF ones.
  GOAssert(
    controller->GetODFTremulantCount() == 0,
    "The internal tremulants should not be counted as ODF tremulants, got: "
      + std::to_string(controller->GetODFTremulantCount()));
  GOAssert(
    controller->GetTremulantCount()
      == controller->GetODFTremulantCount() + nWindchests,
    "InitInternalTremulants() should create one tremulant per windchest, got: "
      + std::to_string(controller->GetTremulantCount()));
  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++) {
    GOTremulant *pTremulant = controller->GetInternalTremulant(windchestI);
    GOWindchest *pWindchest = controller->GetWindchest(windchestI);
    const std::string suffix
      = ", windchest index: " + std::to_string(windchestI);

    GOAssert(
      pTremulant
        == controller->GetTremulant(
          controller->GetODFTremulantCount() + windchestI),
      "Internal tremulants should follow the ODF ones in the windchest order"
        + suffix);
    GOAssert(
      pWindchest->GetTremulantCount() == 1
        && pWindchest->GetTremulantId(0)
          == controller->GetODFTremulantCount() + windchestI,
      "A windchest should have its own internal tremulant" + suffix);
    GOAssert(
      pTremulant->GetTremulantType() == GOSynthTrem
        && !pTremulant->IsOdfDefined(),
      "An internal tremulant should be a not-ODF synth one" + suffix);
    GOAssert(
      pTremulant->GetContext()
        && pTremulant->GetContext()->GetName() == wxT("tremulants-internal"),
      "An internal tremulant should be in the tremulants-internal context"
        + suffix);
    GOAssert(
      pTremulant->GetHardName()
        == controller->GetInternalEnclosure(windchestI)->GetHardName(),
      "An internal tremulant should have the hard name of its windchest"
        + suffix);
  }
  GOAssert(
    GOOrganController::getInternalTremulantName(2)
      == wxT("InternalTremulant002"),
    "getInternalTremulantName() should give a 3-digit group name");

  // Regression test: GOOrganModel::Load() must register every ODF windchest
  // in m_WindchestNByPipeConfig (via AddWindchest()), not just push it onto
  // m_windchests directly. GOCommonControllerTest-based fixtures always
  // build windchests through AddWindchest() themselves, so only a real
  // Load() call - as done here - can catch a regression where the ODF
  // windchest loop bypasses it and CollectWindchestsForNode() then fails
  // its assert(windchestN) for every real ODF windchest.
  for (unsigned windchestI = 0; windchestI < controller->GetWindchestCount();
       windchestI++) {
    std::set<unsigned> outWindchests;

    controller->CollectWindchestsForNode(
      controller->GetWindchest(windchestI)->GetPipeConfig(), outWindchests);
    GOAssert(
      outWindchests == std::set<unsigned>{windchestI + 1},
      "CollectWindchestsForNode() should resolve a real ODF-loaded "
      "windchest's own node to its windchest number, windchest index: "
        + std::to_string(windchestI));
  }

  // SaveOrganCoreData() round-trip.
  GOConfigFileWriter cfgFile;
  GOConfigWriter cfgWriter(cfgFile, false);

  controller->SaveOrganCoreData(cfgWriter);

  const wxString cmbPath
    = wxString::Format(wxT("%s/test.cmb"), organ_directory);

  GOAssert(
    cfgFile.Save(cmbPath), "SaveOrganCoreData() output should be writable");
  GOAssert(wxFileExists(cmbPath), "The .cmb file should have been created");

  {
    // the file is compressed, so take the plain content from the writer
    GOBuffer<uint8_t> cmbBuf;

    GOAssert(
      cfgFile.GetFileContent(cmbBuf, false),
      "The plain .cmb content should be available");

    const std::string cmbText((const char *)cmbBuf.get(), cmbBuf.GetSize());

    for (const char *pGroup :
         {"[SetterMasterVolume]",
          "[SetterMaster001]",
          "[SetterMaster002]",
          "[InternalTremulant001]",
          "[InternalTremulant002]"})
      GOAssert(
        cmbText.find(pGroup) != std::string::npos,
        std::string("The saved .cmb should contain ") + pGroup);
  }

  CheckInternalTremulantsInCombinations();

  // ClearOrganCoreData() idempotency: calling it multiple times must not
  // crash, and a second call must be a safe no-op (this is exactly what
  // the m_IsOrganCoreDataLoaded flag is for).
  controller->ClearOrganCoreData();
  GOAssert(
    controller->GetWindchestCount() == 0,
    "ClearOrganCoreData() should remove the windchest group");
  GOAssert(
    !controller->GetMasterEnclosure(),
    "ClearOrganCoreData() should reset the master enclosure");
  controller->ClearOrganCoreData();
  GOAssert(
    controller->GetWindchestCount() == 0,
    "A second ClearOrganCoreData() call should be a safe no-op");
}

void GOTestOrganController::CheckInternalTremulantsInCombinations() {
  const unsigned nWindchests = controller->GetWindchestCount();
  const unsigned nOdfTremulants = controller->GetODFTremulantCount();
  const GOCombinationDefinition &generalTemplate
    = controller->GetGeneralTemplate();
  GOGeneralCombination general(*controller, false);
  std::vector<int> elementNs;

  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++) {
    const int elementN = generalTemplate.FindElement(
      GOCombinationDefinition::COMBINATION_TREMULANT,
      -1,
      nOdfTremulants + windchestI + 1);
    const std::string suffix
      = ", windchest index: " + std::to_string(windchestI);

    GOAssert(
      elementN >= 0,
      "The general template should contain the internal tremulant" + suffix);
    GOAssert(
      generalTemplate.GetElements()[elementN].control
          == controller->GetInternalTremulant(windchestI)
        && generalTemplate.GetElements()[elementN].store_unconditional,
      "The template element should be the internal tremulant stored "
      "unconditionally"
        + suffix);
    elementNs.push_back(elementN);
  }

  GOSetterState regularStore;
  GOSetterState scopeStore;
  GOSetterState scopedStore;
  const GOSetterState restore;

  regularStore.isSetActive = true;
  scopeStore.isSetActive = true;
  scopeStore.m_SetterType = GOSetterState::SETTER_SCOPE;
  scopedStore.isSetActive = true;
  scopedStore.m_SetterType = GOSetterState::SETTER_SCOPED;

  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++) {
    GOTremulant *pTremulant = controller->GetInternalTremulant(windchestI);
    const std::string suffix
      = ", windchest index: " + std::to_string(windchestI);

    pTremulant->SetCombinationState(true, wxEmptyString);
  }

  // A regular general stores the engaged tremulants and engages them back
  general.Push(regularStore);
  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++) {
    GOAssert(
      general.GetElementState(elementNs[windchestI]) == BOOL3_TRUE,
      "A regular general should store an engaged internal tremulant, "
      "windchest index: "
        + std::to_string(windchestI));
    controller->GetInternalTremulant(windchestI)
      ->SetCombinationState(false, wxEmptyString);
  }
  general.Push(restore);
  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++) {
    GOAssert(
      controller->GetInternalTremulant(windchestI)->IsEngaged(),
      "A general should engage the stored internal tremulant back, windchest "
      "index: "
        + std::to_string(windchestI));
  }

  // A regular general stores a disengaged tremulant as disengaged
  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++)
    controller->GetInternalTremulant(windchestI)
      ->SetCombinationState(false, wxEmptyString);
  general.Push(regularStore);
  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++) {
    GOAssert(
      general.GetElementState(elementNs[windchestI]) == BOOL3_FALSE,
      "A regular general should store a disengaged internal tremulant as "
      "disengaged, windchest index: "
        + std::to_string(windchestI));
    controller->GetInternalTremulant(windchestI)
      ->SetCombinationState(true, wxEmptyString);
  }
  general.Push(restore);
  for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++) {
    GOAssert(
      !controller->GetInternalTremulant(windchestI)->IsEngaged(),
      "A general should disengage the stored internal tremulant, windchest "
      "index: "
        + std::to_string(windchestI));
  }

  // A scope general takes only the engaged tremulants into the scope
  controller->GetInternalTremulant(0)->SetCombinationState(true, wxEmptyString);
  for (unsigned windchestI = 1; windchestI < nWindchests; windchestI++)
    controller->GetInternalTremulant(windchestI)
      ->SetCombinationState(false, wxEmptyString);
  general.Push(scopeStore);
  GOAssert(
    general.GetElementState(elementNs[0]) == BOOL3_TRUE,
    "A scope general should take an engaged internal tremulant into the "
    "scope");
  for (unsigned windchestI = 1; windchestI < nWindchests; windchestI++)
    GOAssert(
      general.GetElementState(elementNs[windchestI]) == BOOL3_DEFAULT,
      "A scope general should leave a disengaged internal tremulant out of "
      "the scope, windchest index: "
        + std::to_string(windchestI));

  // A scoped general does not touch a tremulant that is out of its scope
  controller->GetInternalTremulant(0)->SetCombinationState(
    false, wxEmptyString);
  for (unsigned windchestI = 1; windchestI < nWindchests; windchestI++)
    controller->GetInternalTremulant(windchestI)
      ->SetCombinationState(true, wxEmptyString);
  general.Push(scopedStore);
  GOAssert(
    general.GetElementState(elementNs[0]) == BOOL3_FALSE,
    "A scoped general should refresh an internal tremulant that is in its "
    "scope");
  for (unsigned windchestI = 1; windchestI < nWindchests; windchestI++)
    GOAssert(
      general.GetElementState(elementNs[windchestI]) == BOOL3_DEFAULT,
      "A scoped general should not take an internal tremulant that is out of "
      "its scope, windchest index: "
        + std::to_string(windchestI));
  general.Push(restore);
  for (unsigned windchestI = 1; windchestI < nWindchests; windchestI++)
    GOAssert(
      controller->GetInternalTremulant(windchestI)->IsEngaged(),
      "A general should not touch an internal tremulant that is out of its "
      "scope, windchest index: "
        + std::to_string(windchestI));

  // The divisionals never contain the internal tremulants
  for (unsigned manualN = controller->GetFirstManualIndex();
       manualN <= controller->GetManualAndPedalCount();
       manualN++) {
    GOManual *pManual = controller->GetManual(manualN);

    for (unsigned tremulantI = 0; tremulantI < pManual->GetTremulantCount();
         tremulantI++)
      for (unsigned windchestI = 0; windchestI < nWindchests; windchestI++)
        GOAssert(
          pManual->GetTremulant(tremulantI)
            != controller->GetInternalTremulant(windchestI),
          "A manual should not contain an internal tremulant, manual: "
            + std::to_string(manualN));
  }
}
