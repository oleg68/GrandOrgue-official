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

#include "model/GOManual.h"
#include "model/GOTremulant.h"

#include "GOGUIButton.h"
#include "GOGUIEnclosure.h"
#include "GOGUIHW1Background.h"
#include "GOGUILabel.h"
#include "GOGUIManual.h"
#include "GOGUIManualBackground.h"
#include "GOGUIPanel.h"
#include "GOGUISetterDisplayMetrics.h"
#include "GOOrganController.h"
#include "gui/GOGuiOrgan.h"

namespace {
/**
 * The metrics of the panel "Coupler manuals and volume". The enclosure grid has
 * two rows: the enclosures, then the embedded tremulant pistons and the label.
 * The column 1 is the master volume (it has no piston; the label is there), the
 * enclosure of the windchest number N and its piston are in the column N + 1.
 * The master column is as wide as the wider of an enclosure and the label (so
 * the master enclosure is a bit apart from the others), the other columns as
 * the wider of an enclosure and a piston cell. The row of the pistons is as
 * high as the higher of the label and a piston cell.
 */
class PanelMetrics : public GOGUISetterDisplayMetrics {
public:
  /** The rows of the enclosure grid, from the top */
  enum Row {
    ENCLOSURE_ROW = 1,
    TREMULANT_BUTTON_ROW,
    LAST_ROW = TREMULANT_BUTTON_ROW
  };

  /**
   * The size of label01.png, which the label of the panel has: keep in sync
   * with the picture
   */
  static constexpr unsigned LABEL_WIDTH = 80;
  static constexpr unsigned LABEL_HEIGHT = 25;

  PanelMetrics(GOConfigReader &ini, GOOrganController *pOrganController);

  unsigned GetEnclosureColumnWidth(unsigned colN) override;
  unsigned GetEnclosureRowHeight(int rowN) override;
  unsigned GetNEnclosureRows() override { return LAST_ROW; }
};

PanelMetrics::PanelMetrics(
  GOConfigReader &ini, GOOrganController *pOrganController)
  : GOGUISetterDisplayMetrics(
    ini, pOrganController, GOGUI_COUPLER_MANUALS_AND_VOLUME) {
  // The virtual methods do not work in the constructor of the base, so the
  // screen size is extended here: one column per windchest + the master volume
  const unsigned nColumns = pOrganController->GetWindchestCount() + 1;
  const unsigned areaWidth = GOGUILayoutEngine::computeCellOffset(
    nColumns,
    [this](unsigned colI) { return GetEnclosureColumnWidth(colI + 1); },
    0);

  m_DispScreenSizeHoriz = std::max(
    m_DispScreenSizeHoriz,
    // the 6 of GOGUILayoutEngine::computeEnclosureAreaX
    areaWidth + 6);
  // the row of the pistons and the gap above it
  m_DispScreenSizeVert += ROW_GAP + GetEnclosureRowHeight(TREMULANT_BUTTON_ROW);
}

unsigned PanelMetrics::GetEnclosureColumnWidth(unsigned colN) {
  return std::max(
    GetEnclosureWidth(), colN == 1 ? LABEL_WIDTH : GetButtonWidth());
}

unsigned PanelMetrics::GetEnclosureRowHeight(int rowN) {
  assert(rowN >= ENCLOSURE_ROW && rowN <= LAST_ROW);
  return rowN == ENCLOSURE_ROW ? GetEnclosureHeight()
                               : std::max(LABEL_HEIGHT, GetButtonHeight());
}
} // namespace

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
  GOGUIDisplayMetrics *pMetrics = new PanelMetrics(cfg, p_OrganController);
  const unsigned tremulantRowN = PanelMetrics::TREMULANT_BUTTON_ROW;

  pPanel->Init(
    cfg,
    pMetrics,
    _("Coupler manuals and volume"),
    // The .cmb group is still named SetterFloating for compatibility
    // with the existing organ settings
    wxT("SetterFloating"),
    wxT(""));

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

  // The label of the row of the pistons: in the column of the master volume,
  // which has no piston
  GOGUILabel *pTremulantLabel = new GOGUILabel(pPanel, nullptr);

  pTremulantLabel->Init(
    cfg,
    wxT("InternalTremulantLabel"),
    1,             // x_pos: the column 1, the master volume
    tremulantRowN, // y_pos: the row of the pistons
    _("Tremulants:"),
    1, // label01.png
    0, // its own width, as the metrics assume
    0, // its own height
    GOGUILayoutEngine::GRID_ENCLOSURE);
  pPanel->AddControl(pTremulantLabel);

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
      pPanel, p_OrganController->GetInternalTremulant(windchestI), true);

    // The same name as the group of the tremulant object, like the enclosures
    // do. A piston without text: the windchest name is already shown on the
    // enclosure above
    pTremulantButton->Init(
      cfg,
      GOOrganController::getInternalTremulantName(windchestN),
      windchestN + 1, // x_pos: the column of the enclosure; 1 is the master
      tremulantRowN,  // y_pos: the row below the enclosures
      0,              // the default set of the images
      wxT(" "), // a space draws no text (an empty one gives the control name)
      GOGUILayoutEngine::GRID_ENCLOSURE);
    pPanel->AddControl(pTremulantButton);
  }
  return pPanel;
}
