/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOGUICouplerManualsAndVolumePanel.h"

#include <algorithm>
#include <cassert>

#include <wx/intl.h>

#include "GOGUIButton.h"
#include "GOGUIEnclosure.h"
#include "GOGUIHW1Background.h"
#include "GOGUIManual.h"
#include "GOGUIManualBackground.h"
#include "GOGUIPanel.h"
#include "GOGUISetterDisplayMetrics.h"
#include "GOOrganController.h"
#include "gui/GOGuiOrgan.h"
#include "model/GOTremulant.h"

unsigned GOGUICouplerManualsAndVolumePanel::LayoutEngine::GetColumnWidth() {
  // keep in sync with x_size in GOGUISetterDisplayMetrics
  return std::max(r_metrics.GetEnclosureWidth(), r_metrics.GetDrawstopWidth());
}

unsigned GOGUICouplerManualsAndVolumePanel::LayoutEngine::GetRowHeight(
  int rowI) {
  assert(rowI >= 0 && rowI <= LAST_ROW);
  return rowI == ENCLOSURE_ROW ? r_metrics.GetEnclosureHeight()
                               : r_metrics.GetDrawstopHeight();
}

unsigned GOGUICouplerManualsAndVolumePanel::LayoutEngine::GetRowOffset(
  int rowI) {
  assert(rowI >= 0 && rowI <= LAST_ROW);

  unsigned offset = 0;

  // every row above this one takes its height and the gap below it
  for (int aboveRowI = 0; aboveRowI < rowI; ++aboveRowI)
    offset += GetRowHeight(aboveRowI) + GOGUIDisplayMetrics::ROW_GAP;
  return offset;
}

int GOGUICouplerManualsAndVolumePanel::LayoutEngine::GetCentredInColumnX(
  unsigned colN, unsigned width) {
  const int columnWidth = GetColumnWidth();
  const int areaX
    = ((int)r_metrics.GetScreenWidth() - (int)GetEnclosureAreaWidth()) / 2;

  return areaX + (int)(colN - 1) * columnWidth + (columnWidth - (int)width) / 2;
}

int GOGUICouplerManualsAndVolumePanel::LayoutEngine::GetEnclosureX(
  const GOGUIEnclosure *pEnclosure) {
  const auto it
    = std::find(m_Enclosures.begin(), m_Enclosures.end(), pEnclosure);

  if (it == m_Enclosures.end())
    throw wxString(_("enclosure not found")); // as the base class

  return GetCentredInColumnX(
    (it - m_Enclosures.begin()) + 1, r_metrics.GetEnclosureWidth());
}

void GOGUICouplerManualsAndVolumePanel::LayoutEngine::GetDrawstopBlitPosition(
  int rowI, int colN, int &outX, int &outY) {
  outX = GetCentredInColumnX(colN, r_metrics.GetDrawstopWidth());
  outY = GetRowY(rowI);
}

GOGUICouplerManualsAndVolumePanel::GOGUICouplerManualsAndVolumePanel(
  GOGuiOrgan &guiOrgan)
  : r_GuiOrgan(guiOrgan), p_OrganController(guiOrgan.GetOrganController()) {}

GOGUICouplerManualsAndVolumePanel::~GOGUICouplerManualsAndVolumePanel() {}

void GOGUICouplerManualsAndVolumePanel::CreatePanels(GOConfigReader &cfg) {
  r_GuiOrgan.AddPanel(CreatePanel(cfg));
}

GOGUIPanel *GOGUICouplerManualsAndVolumePanel::CreatePanel(
  GOConfigReader &cfg) {
  GOGUIPanel *pPanel = new GOGUIPanel(
    p_OrganController, r_GuiOrgan.GetImageCache(), r_GuiOrgan.GetMouseState());
  GOGUIDisplayMetrics *pMetrics = new GOGUISetterDisplayMetrics(
    cfg, p_OrganController, GOGUI_COUPLER_MANUALS_AND_VOLUME);

  pPanel->Init(
    cfg,
    pMetrics,
    _("Coupler manuals and volume"),
    // The .cmb group is still named SetterFloating for compatibility
    // with the existing organ settings
    wxT("SetterFloating"),
    wxT(""),
    new LayoutEngine(*pMetrics));

  GOGUIHW1Background *pBack = new GOGUIHW1Background(pPanel);
  pBack->Init(cfg, wxT("SetterFloating"));
  pPanel->AddControl(pBack);

  for (unsigned i = p_OrganController->GetODFManualCount();
       i <= p_OrganController->GetManualAndPedalCount();
       i++) {
    wxString group;
    group.Printf(
      wxT("SetterFloating%03d"),
      i - p_OrganController->GetODFManualCount() + 1);
    GOGUIManualBackground *pManualBack = new GOGUIManualBackground(
      pPanel, i - p_OrganController->GetODFManualCount());
    pManualBack->Init(cfg, group);
    pPanel->AddControl(pManualBack);

    GOGUIManual *pManual = new GOGUIManual(
      pPanel,
      p_OrganController->GetManual(i),
      i - p_OrganController->GetODFManualCount());
    pManual->Init(cfg, group);
    pPanel->AddControl(pManual);
  }

  GOGUIEnclosure *pEnclosure
    = new GOGUIEnclosure(pPanel, p_OrganController->GetMasterEnclosure());

  pEnclosure->Init(cfg, wxT("SetterMasterVolume"));
  pPanel->AddControl(pEnclosure);

  for (unsigned nWindchests = p_OrganController->GetWindchestCount(),
                windchestI = 0;
       windchestI < nWindchests;
       windchestI++) {
    const unsigned windchestN = windchestI + 1;

    pEnclosure = new GOGUIEnclosure(
      pPanel, p_OrganController->GetInternalEnclosure(windchestI));
    pEnclosure->Init(
      cfg, wxString::Format(wxT("SetterMaster%03d"), windchestN));
    pPanel->AddControl(pEnclosure);

    GOGUIButton *pTremulantButton = new GOGUIButton(
      pPanel, p_OrganController->GetInternalTremulant(windchestI), false);

    // The same name as the group of the tremulant object, like the enclosures
    // do. The windchest name is already shown on the enclosure above
    pTremulantButton->Init(
      cfg,
      GOOrganController::getInternalTremulantName(windchestN),
      // x_pos: the column of the enclosure; column 1 is the master volume
      windchestN + 1,
      // y_pos: the row below the enclosures
      LayoutEngine::TREMULANT_BUTTON_ROW,
      0,
      _("Tremulant"));
    pPanel->AddControl(pTremulantButton);
  }
  return pPanel;
}
