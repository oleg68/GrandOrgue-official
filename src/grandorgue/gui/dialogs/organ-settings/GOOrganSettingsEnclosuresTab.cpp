/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOOrganSettingsEnclosuresTab.h"

#include <wx/checkbox.h>
#include <wx/gbsizer.h>
#include <wx/listbox.h>
#include <wx/stattext.h>
#include <wx/treectrl.h>

#include "model/GOEnclosure.h"
#include "model/GOOrganModel.h"
#include "model/GOWindchest.h"

#include "GOEvent.h"

static const wxSize EDIT_SIZE = wxSize(60, -1);
static const wxString WX_FLOAT_FORMAT = wxT("%.1f");
static const wxString WX_TAB_CODE = wxT("Enclosures");
static const wxString WX_TAB_TITLE = _("Enclosures");

enum {
  ID_EVENT_TREE = 200,
  ID_EVENT_MIN_AMP_LEVEL,
  ID_EVENT_LOW_SHELF_FREQUENCY,
  ID_EVENT_LOW_SHELF_ATTENUATION_DB,
  ID_EVENT_HIGH_SHELF_FREQUENCY,
  ID_EVENT_HIGH_SHELF_ATTENUATION_DB,
};

BEGIN_EVENT_TABLE(GOOrganSettingsEnclosuresTab, wxPanel)
EVT_TREE_SEL_CHANGING(
  ID_EVENT_TREE, GOOrganSettingsEnclosuresTab::OnTreeChanging)
EVT_TREE_SEL_CHANGED(ID_EVENT_TREE, GOOrganSettingsEnclosuresTab::OnTreeChanged)
EVT_TEXT(
  ID_EVENT_MIN_AMP_LEVEL, GOOrganSettingsEnclosuresTab::OnMinAmpLevelChanged)
EVT_TEXT(
  ID_EVENT_LOW_SHELF_FREQUENCY,
  GOOrganSettingsEnclosuresTab::OnLowShelfFrequencyChanged)
EVT_TEXT(
  ID_EVENT_LOW_SHELF_ATTENUATION_DB,
  GOOrganSettingsEnclosuresTab::OnLowShelfAttenuationDbChanged)
EVT_TEXT(
  ID_EVENT_HIGH_SHELF_FREQUENCY,
  GOOrganSettingsEnclosuresTab::OnHighShelfFrequencyChanged)
EVT_TEXT(
  ID_EVENT_HIGH_SHELF_ATTENUATION_DB,
  GOOrganSettingsEnclosuresTab::OnHighShelfAttenuationDbChanged)
END_EVENT_TABLE()

class GOOrganSettingsEnclosuresTab::ItemData : public wxTreeItemData {
public:
  enum ItemType {
    ORGAN,
    WINDCHEST,
    ENCLOSURE,
  } m_type;

  union {
    GOOrganModel *p_organ;
    GOWindchest *p_windchest;
    GOEnclosure *p_enclosure;
  };

  ItemData(GOOrganModel &organModel) : m_type(ORGAN), p_organ(&organModel) {}
  ItemData(GOWindchest &windchest)
    : m_type(WINDCHEST), p_windchest(&windchest) {}
  ItemData(GOEnclosure &enclosure)
    : m_type(ENCLOSURE), p_enclosure(&enclosure) {}
};

GOOrganSettingsEnclosuresTab::GOOrganSettingsEnclosuresTab(
  GOOrganModel &organModel, GOOrganSettingsDialogBase *pDlg)
  : GOOrganSettingsTab(pDlg, WX_TAB_CODE, WX_TAB_TITLE),
    r_OrganModel(organModel) {
  wxGridBagSizer *const mainSizer = new wxGridBagSizer(5, 5);

  // wxListBox would be better but it does not allow to veto selections
  // so we use wxTreeCtrl instead of wxListBox
  m_tree = new wxTreeCtrl(
    this,
    ID_EVENT_TREE,
    wxDefaultPosition,
    wxDefaultSize,
    wxTR_MULTIPLE | wxTR_HIDE_ROOT);
  mainSizer->Add(
    m_tree, wxGBPosition(0, 0), wxGBSpan(8, 1), wxEXPAND | wxALL, 5);

  m_IsOdfDefined = new wxCheckBox(
    this, wxID_ANY, _("This enclosure is ODF defined and may not be altered"));
  m_IsOdfDefined->Disable();

  mainSizer->Add(
    m_IsOdfDefined,
    wxGBPosition(0, 1),
    wxGBSpan(1, 3),
    wxALIGN_LEFT | wxEXPAND | wxALL,
    5);
  mainSizer->Add(
    new wxStaticText(this, wxID_ANY, _("Affected windchests:")),
    wxGBPosition(1, 1),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxTOP | wxLEFT | wxBOTTOM,
    5);
  m_WindchestList = new wxListBox(
    this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
  m_WindchestList->Disable();
  mainSizer->Add(
    m_WindchestList,
    wxGBPosition(1, 2),
    wxGBSpan(2, 2),
    wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM,
    5);
  mainSizer->Add(
    new wxStaticText(this, wxID_ANY, _("Minimum amplitude level:")),
    wxGBPosition(3, 1),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
    5);
  m_MinAmpLevelEdit = new wxTextCtrl(
    this, ID_EVENT_MIN_AMP_LEVEL, wxEmptyString, wxDefaultPosition, EDIT_SIZE);
  mainSizer->Add(
    m_MinAmpLevelEdit,
    wxGBPosition(3, 2),
    wxDefaultSpan,
    wxEXPAND | wxRIGHT | wxBOTTOM,
    5);

  mainSizer->Add(
    new wxStaticText(this, wxID_ANY, _("Low shelf frequency (Hz):")),
    wxGBPosition(4, 1),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
    5);
  m_LowShelfFrequencyEdit = new wxTextCtrl(
    this,
    ID_EVENT_LOW_SHELF_FREQUENCY,
    wxEmptyString,
    wxDefaultPosition,
    EDIT_SIZE);
  mainSizer->Add(
    m_LowShelfFrequencyEdit,
    wxGBPosition(4, 2),
    wxDefaultSpan,
    wxEXPAND | wxRIGHT | wxBOTTOM,
    5);

  mainSizer->Add(
    new wxStaticText(this, wxID_ANY, _("Low shelf attenuation (dB):")),
    wxGBPosition(5, 1),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
    5);
  m_LowShelfAttenuationDbEdit = new wxTextCtrl(
    this,
    ID_EVENT_LOW_SHELF_ATTENUATION_DB,
    wxEmptyString,
    wxDefaultPosition,
    EDIT_SIZE);
  mainSizer->Add(
    m_LowShelfAttenuationDbEdit,
    wxGBPosition(5, 2),
    wxDefaultSpan,
    wxEXPAND | wxRIGHT | wxBOTTOM,
    5);

  mainSizer->Add(
    new wxStaticText(this, wxID_ANY, _("High shelf frequency (Hz):")),
    wxGBPosition(6, 1),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
    5);
  m_HighShelfFrequencyEdit = new wxTextCtrl(
    this,
    ID_EVENT_HIGH_SHELF_FREQUENCY,
    wxEmptyString,
    wxDefaultPosition,
    EDIT_SIZE);
  mainSizer->Add(
    m_HighShelfFrequencyEdit,
    wxGBPosition(6, 2),
    wxDefaultSpan,
    wxEXPAND | wxRIGHT | wxBOTTOM,
    5);

  mainSizer->Add(
    new wxStaticText(this, wxID_ANY, _("High shelf attenuation (dB):")),
    wxGBPosition(7, 1),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
    5);
  m_HighShelfAttenuationDbEdit = new wxTextCtrl(
    this,
    ID_EVENT_HIGH_SHELF_ATTENUATION_DB,
    wxEmptyString,
    wxDefaultPosition,
    EDIT_SIZE);
  mainSizer->Add(
    m_HighShelfAttenuationDbEdit,
    wxGBPosition(7, 2),
    wxDefaultSpan,
    wxEXPAND | wxRIGHT | wxBOTTOM,
    5);

  mainSizer->AddGrowableCol(0, 1);
  mainSizer->AddGrowableCol(3, 1);
  mainSizer->AddGrowableRow(2, 1);
  SetSizerAndFit(mainSizer);
}

bool GOOrganSettingsEnclosuresTab::TransferDataToWindow() {
  auto rootItem = m_tree->AddRoot(
    r_OrganModel.GetRootPipeConfigNode().GetName(),
    -1,
    -1,
    new ItemData(r_OrganModel));

  // fill m_WindchestsByEnclosures
  for (GOWindchest *pW : r_OrganModel.GetWindchests())
    for (GOEnclosure *pE : pW->GetEnclosures())
      m_WindchestsByEnclosures[pE].push_back(pW->GetName());

  // list internal enclosures
  for (GOEnclosure *pE : r_OrganModel.GetEnclosures())
    if (!pE->IsOdfDefined())
      m_tree->AppendItem(rootItem, pE->GetName(), -1, -1, new ItemData(*pE));

  // list odf-defined enclosures
  for (GOEnclosure *pE : r_OrganModel.GetEnclosures())
    if (pE->IsOdfDefined())
      m_tree->AppendItem(rootItem, pE->GetName(), -1, -1, new ItemData(*pE));
  return true;
}

void GOOrganSettingsEnclosuresTab::OnTreeChanging(wxTreeEvent &e) {
  if (CheckForUnapplied())
    e.Veto();
}

void GOOrganSettingsEnclosuresTab::LoadValues() {
  wxArrayTreeItemIds entries;
  GOEnclosure *pSelectedEnclosure = nullptr;

  m_tree->GetSelections(entries);

  const unsigned nSelected = entries.size();

  // set pSelectedEnclosure if only one enclosure is selected
  if (nSelected == 1) {
    ItemData *pData = (ItemData *)m_tree->GetItemData(entries[0]);

    if (pData->m_type == ItemData::ENCLOSURE)
      pSelectedEnclosure = pData->p_enclosure;
  }

  // display data of the selected enclosure
  m_WindchestList->Clear();
  if (pSelectedEnclosure) {
    if (auto iWindchests = m_WindchestsByEnclosures.find(pSelectedEnclosure);
        iWindchests != m_WindchestsByEnclosures.end())
      m_WindchestList->Append(iWindchests->second);
    m_MinAmpLevelEdit->ChangeValue(
      wxString::Format("%u", pSelectedEnclosure->GetAmpMinimumLevel()));
    m_MinAmpLevelEdit->DiscardEdits();
    m_LowShelfFrequencyEdit->ChangeValue(wxString::Format(
      WX_FLOAT_FORMAT, pSelectedEnclosure->GetLowShelfFrequency()));
    m_LowShelfFrequencyEdit->DiscardEdits();
    m_LowShelfAttenuationDbEdit->ChangeValue(wxString::Format(
      WX_FLOAT_FORMAT, pSelectedEnclosure->GetLowShelfAttenuationDb()));
    m_LowShelfAttenuationDbEdit->DiscardEdits();
    m_HighShelfFrequencyEdit->ChangeValue(wxString::Format(
      WX_FLOAT_FORMAT, pSelectedEnclosure->GetHighShelfFrequency()));
    m_HighShelfFrequencyEdit->DiscardEdits();
    m_HighShelfAttenuationDbEdit->ChangeValue(wxString::Format(
      WX_FLOAT_FORMAT, pSelectedEnclosure->GetHighShelfAttenuationDb()));
    m_HighShelfAttenuationDbEdit->DiscardEdits();
  } else {
    m_MinAmpLevelEdit->Clear();
    m_LowShelfFrequencyEdit->Clear();
    m_LowShelfAttenuationDbEdit->Clear();
    m_HighShelfFrequencyEdit->Clear();
    m_HighShelfAttenuationDbEdit->Clear();
  }

  // check that all selected items are internal enclosures
  bool areInternalEnclosuresSelected = false;
  bool areOnlyEnclosuresSelected = false;

  for (auto id : entries) {
    ItemData *pData = (ItemData *)m_tree->GetItemData(id);
    bool isEnclosure = pData->m_type == ItemData::ENCLOSURE;
    bool isOdfDefined = isEnclosure && pData->p_enclosure->IsOdfDefined();

    if (isEnclosure) {
      areOnlyEnclosuresSelected = true;
      if (!isOdfDefined)
        areInternalEnclosuresSelected = true;
    }

    if (!isEnclosure || isOdfDefined) {
      // now we do not allow to change min value of odf-defined enclosures
      areInternalEnclosuresSelected = false;
      if (!isEnclosure)
        areOnlyEnclosuresSelected = false;
      break;
    }
  }
  m_IsOdfDefined->SetValue(
    areOnlyEnclosuresSelected && !areInternalEnclosuresSelected);
  m_MinAmpLevelEdit->Enable(areInternalEnclosuresSelected);
  m_LowShelfFrequencyEdit->Enable(areInternalEnclosuresSelected);
  m_LowShelfAttenuationDbEdit->Enable(areInternalEnclosuresSelected);
  m_HighShelfFrequencyEdit->Enable(areInternalEnclosuresSelected);
  m_HighShelfAttenuationDbEdit->Enable(areInternalEnclosuresSelected);
  m_IsDefaultEnabled = areInternalEnclosuresSelected;
  NotifyModified(false);
}

void GOOrganSettingsEnclosuresTab::DoForAllEnclosures(
  const std::function<void(GOEnclosure &enclosure)> &f) {
  wxArrayTreeItemIds entries;

  m_tree->GetSelections(entries);
  for (auto id : entries) {
    ItemData *pData = (ItemData *)m_tree->GetItemData(id);

    if (pData->m_type == ItemData::ENCLOSURE)
      f(*(pData->p_enclosure));
  }
}

void GOOrganSettingsEnclosuresTab::ResetToDefault() {
  DoForAllEnclosures([](GOEnclosure &enclosure) {
    enclosure.SetAmpMinimumLevel(enclosure.GetDefaultAmpMinimumLevel());
    enclosure.SetLowShelfFrequency(enclosure.GetDefaultLowShelfFrequency());
    enclosure.SetLowShelfAttenuationDb(
      enclosure.GetDefaultLowShelfAttenuationDb());
    enclosure.SetHighShelfFrequency(enclosure.GetDefaultHighShelfFrequency());
    enclosure.SetHighShelfAttenuationDb(
      enclosure.GetDefaultHighShelfAttenuationDb());
  });
  LoadValues();
}

void GOOrganSettingsEnclosuresTab::ApplyChanges() {
  if (m_MinAmpLevelEdit->IsModified()) {
    long minAmpVal;

    if (
      m_MinAmpLevelEdit->GetValue().ToLong(&minAmpVal) && minAmpVal >= 0
      && minAmpVal <= 100)
      DoForAllEnclosures([minAmpVal](GOEnclosure &enclosure) {
        enclosure.SetAmpMinimumLevel(minAmpVal);
      });
    else
      GOMessageBox(
        _("Minimal amplitude level is invalid"),
        _("Error"),
        wxOK | wxICON_ERROR,
        this);
  }
  if (m_LowShelfFrequencyEdit->IsModified()) {
    double frequency;

    if (
      m_LowShelfFrequencyEdit->GetValue().ToDouble(&frequency) && frequency > 0)
      DoForAllEnclosures([frequency](GOEnclosure &enclosure) {
        enclosure.SetLowShelfFrequency(frequency);
      });
    else
      GOMessageBox(
        _("Low shelf frequency is invalid"),
        _("Error"),
        wxOK | wxICON_ERROR,
        this);
  }
  if (m_LowShelfAttenuationDbEdit->IsModified()) {
    double attenuationDb;

    if (
      m_LowShelfAttenuationDbEdit->GetValue().ToDouble(&attenuationDb)
      && attenuationDb >= 0 && attenuationDb <= 121)
      DoForAllEnclosures([attenuationDb](GOEnclosure &enclosure) {
        enclosure.SetLowShelfAttenuationDb(attenuationDb);
      });
    else
      GOMessageBox(
        _("Low shelf attenuation is invalid"),
        _("Error"),
        wxOK | wxICON_ERROR,
        this);
  }
  if (m_HighShelfFrequencyEdit->IsModified()) {
    double frequency;

    if (
      m_HighShelfFrequencyEdit->GetValue().ToDouble(&frequency)
      && frequency > 0)
      DoForAllEnclosures([frequency](GOEnclosure &enclosure) {
        enclosure.SetHighShelfFrequency(frequency);
      });
    else
      GOMessageBox(
        _("High shelf frequency is invalid"),
        _("Error"),
        wxOK | wxICON_ERROR,
        this);
  }
  if (m_HighShelfAttenuationDbEdit->IsModified()) {
    double attenuationDb;

    if (
      m_HighShelfAttenuationDbEdit->GetValue().ToDouble(&attenuationDb)
      && attenuationDb >= 0 && attenuationDb <= 121)
      DoForAllEnclosures([attenuationDb](GOEnclosure &enclosure) {
        enclosure.SetHighShelfAttenuationDb(attenuationDb);
      });
    else
      GOMessageBox(
        _("High shelf attenuation is invalid"),
        _("Error"),
        wxOK | wxICON_ERROR,
        this);
  }
  NotifyModified(false);
}
