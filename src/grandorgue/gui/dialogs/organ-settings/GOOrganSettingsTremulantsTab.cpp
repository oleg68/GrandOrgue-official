/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOOrganSettingsTremulantsTab.h"

#include <cmath>
#include <optional>
#include <set>
#include <type_traits>

#include <wx/checkbox.h>
#include <wx/gbsizer.h>
#include <wx/panel.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include "model/GOOrganModel.h"

#include "GOEvent.h"

static const wxSize EDIT_SIZE = wxSize(60, -1);
static const wxString WX_TAB_CODE = wxT("Tremulants");
static const wxString WX_TAB_TITLE = _("Tremulants");

static const wxString WX_INT_FORMAT = wxT("%d");
static const wxString WX_PITCH_DEPTH_CENTS_FORMAT = wxT("%.1f");
static const wxString WX_TIME_MS_FORMAT = wxT("%.0f");

enum {
  ID_EVENT_TREE = 300,
  ID_EVENT_PERIOD_MS,
  ID_EVENT_AMP_MOD_DEPTH_PCNT,
  ID_EVENT_PITCH_MOD_DEPTH_CENTS,
  ID_EVENT_PITCH_MOD_PHASE_DEGREES,
  ID_EVENT_START_TIME_MS,
  ID_EVENT_STOP_TIME_MS,
};

BEGIN_EVENT_TABLE(GOOrganSettingsTremulantsTab, wxPanel)
EVT_TREE_SEL_CHANGING(
  ID_EVENT_TREE, GOOrganSettingsTremulantsTab::OnTreeChanging)
EVT_TREE_SEL_CHANGED(ID_EVENT_TREE, GOOrganSettingsTremulantsTab::OnTreeChanged)
EVT_TEXT(ID_EVENT_PERIOD_MS, GOOrganSettingsTremulantsTab::OnEditChanged)
EVT_TEXT(
  ID_EVENT_AMP_MOD_DEPTH_PCNT, GOOrganSettingsTremulantsTab::OnEditChanged)
EVT_TEXT(
  ID_EVENT_PITCH_MOD_DEPTH_CENTS, GOOrganSettingsTremulantsTab::OnEditChanged)
EVT_TEXT(
  ID_EVENT_PITCH_MOD_PHASE_DEGREES, GOOrganSettingsTremulantsTab::OnEditChanged)
EVT_TEXT(ID_EVENT_START_TIME_MS, GOOrganSettingsTremulantsTab::OnEditChanged)
EVT_TEXT(ID_EVENT_STOP_TIME_MS, GOOrganSettingsTremulantsTab::OnEditChanged)
END_EVENT_TABLE()

class GOOrganSettingsTremulantsTab::ItemData : public wxTreeItemData {
public:
  const unsigned m_TremulantI; // the index in the organ model
  GOTremulant &r_Tremulant;

  ItemData(unsigned tremulantI, GOTremulant &tremulant)
    : m_TremulantI(tremulantI), r_Tremulant(tremulant) {}
};

/**
 * Adds a row "label: edit" to a grid sizer of a panel.
 * @param panel the parent of the new controls
 * @param sizer the sizer of the panel
 * @param row the row of the sizer
 * @param label the text of the label
 * @param editId the id of the new edit
 * @return the new edit; it is owned by the panel
 */
static wxTextCtrl *add_edit_row(
  wxPanel &panel,
  wxGridBagSizer &sizer,
  int row,
  const wxString &label,
  int editId) {
  wxTextCtrl *const pEdit = new wxTextCtrl(
    &panel, editId, wxEmptyString, wxDefaultPosition, EDIT_SIZE);

  sizer.Add(
    new wxStaticText(&panel, wxID_ANY, label),
    wxGBPosition(row, 0),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
    5);
  sizer.Add(
    pEdit,
    wxGBPosition(row, 1),
    wxDefaultSpan,
    wxEXPAND | wxRIGHT | wxBOTTOM,
    5);
  return pEdit;
}

GOOrganSettingsTremulantsTab::GOOrganSettingsTremulantsTab(
  GOOrganModel &organModel, GOOrganSettingsDialogBase *pDlg)
  : GOOrganSettingsTab(pDlg, WX_TAB_CODE, WX_TAB_TITLE),
    r_OrganModel(organModel) {
  wxGridBagSizer *const mainSizer = new wxGridBagSizer(5, 5);

  p_Tree = new wxTreeCtrl(
    this,
    ID_EVENT_TREE,
    wxDefaultPosition,
    wxDefaultSize,
    wxTR_MULTIPLE | wxTR_HIDE_ROOT);
  mainSizer->Add(
    p_Tree, wxGBPosition(0, 0), wxGBSpan(3, 1), wxEXPAND | wxALL, 5);

  p_IsOdfDefined = new wxCheckBox(
    this, wxID_ANY, _("This tremulant is ODF defined and may not be altered"));
  p_IsOdfDefined->Disable();
  mainSizer->Add(
    p_IsOdfDefined,
    wxGBPosition(0, 1),
    wxGBSpan(1, 3),
    wxALIGN_LEFT | wxEXPAND | wxALL,
    5);

  mainSizer->Add(
    new wxStaticText(this, wxID_ANY, _("Type:")),
    wxGBPosition(1, 1),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxLEFT | wxBOTTOM,
    5);
  p_TypeText = new wxStaticText(this, wxID_ANY, wxEmptyString);
  mainSizer->Add(
    p_TypeText,
    wxGBPosition(1, 2),
    wxGBSpan(1, 2),
    wxEXPAND | wxRIGHT | wxBOTTOM,
    5);

  p_SynthParamsPanel = new wxPanel(this);

  wxGridBagSizer *const paramsSizer = new wxGridBagSizer(5, 5);

  p_PeriodMsEdit = add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    0,
    _("Period (ms):"),
    ID_EVENT_PERIOD_MS);
  p_AmpModDepthPcntEdit = add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    1,
    _("Amplitude depth (%):"),
    ID_EVENT_AMP_MOD_DEPTH_PCNT);
  p_PitchModDepthCentsEdit = add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    2,
    _("Pitch depth (cents):"),
    ID_EVENT_PITCH_MOD_DEPTH_CENTS);
  p_PitchModPhaseDegreesEdit = add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    3,
    _("Pitch phase (degrees):"),
    ID_EVENT_PITCH_MOD_PHASE_DEGREES);
  p_StartTimeMsEdit = add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    4,
    _("Start time (ms):"),
    ID_EVENT_START_TIME_MS);
  p_StopTimeMsEdit = add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    5,
    _("Stop time (ms):"),
    ID_EVENT_STOP_TIME_MS);
  p_SynthParamsPanel->SetSizer(paramsSizer);
  // the panel keeps its place when hidden, so the dialog does not resize
  mainSizer->Add(
    p_SynthParamsPanel,
    wxGBPosition(2, 1),
    wxGBSpan(1, 3),
    wxEXPAND | wxRESERVE_SPACE_EVEN_IF_HIDDEN,
    0);

  mainSizer->AddGrowableCol(0, 1);
  mainSizer->AddGrowableCol(3, 1);
  mainSizer->AddGrowableRow(2, 1);
  SetSizerAndFit(mainSizer);
}

void GOOrganSettingsTremulantsTab::DoForSelectedTremulants(
  const std::function<void(unsigned tremulantI, GOTremulant &tremulant)> &f) {
  wxArrayTreeItemIds entries;

  p_Tree->GetSelections(entries);
  for (auto id : entries) {
    const ItemData *pData = (ItemData *)p_Tree->GetItemData(id);

    f(pData->m_TremulantI, pData->r_Tremulant);
  }
}

/**
 * Puts a text into an edit as if the user had not edited it. ChangeValue()
 * does not send EVT_TEXT, so the tab is not marked as modified.
 */
static void set_edit_text(wxTextCtrl &edit, const wxString &text) {
  edit.ChangeValue(text);
  // ReadEdits() treats only the edits with the modified flag set as changed by
  // the user, and the flag left by an earlier typing is not guaranteed to be
  // cleared by ChangeValue()
  edit.DiscardEdits();
}

void GOOrganSettingsTremulantsTab::FillEdits(const GOTremulant &tremulant) {
  set_edit_text(
    *p_PeriodMsEdit, wxString::Format(WX_INT_FORMAT, tremulant.GetPeriodMs()));
  set_edit_text(
    *p_AmpModDepthPcntEdit,
    wxString::Format(WX_INT_FORMAT, tremulant.GetAmpModDepthPcnt()));
  set_edit_text(
    *p_PitchModDepthCentsEdit,
    wxString::Format(
      WX_PITCH_DEPTH_CENTS_FORMAT, tremulant.GetPitchModDepthCents()));
  set_edit_text(
    *p_PitchModPhaseDegreesEdit,
    wxString::Format(WX_INT_FORMAT, tremulant.GetPitchModPhaseDegrees()));
  set_edit_text(
    *p_StartTimeMsEdit,
    wxString::Format(WX_TIME_MS_FORMAT, tremulant.GetStartTimeMs()));
  set_edit_text(
    *p_StopTimeMsEdit,
    wxString::Format(WX_TIME_MS_FORMAT, tremulant.GetStopTimeMs()));
}

void GOOrganSettingsTremulantsTab::ClearEdits() {
  set_edit_text(*p_PeriodMsEdit, wxEmptyString);
  set_edit_text(*p_AmpModDepthPcntEdit, wxEmptyString);
  set_edit_text(*p_PitchModDepthCentsEdit, wxEmptyString);
  set_edit_text(*p_PitchModPhaseDegreesEdit, wxEmptyString);
  set_edit_text(*p_StartTimeMsEdit, wxEmptyString);
  set_edit_text(*p_StopTimeMsEdit, wxEmptyString);
}

/**
 * Reads an edit if it was modified and validates the value against a range.
 * If it is not valid, shows the reason to the user and moves the focus to the
 * edit.
 * All bounds are integers (even for the float parameters), so "%g" prints them
 * without a decimal part; it would also print a non-integer bound.
 * @param pParent the parent window of the error message box
 * @param edit the edit to read
 * @param fieldName the name of the field, for the message
 * @param range the allowed range of the value
 * @param outValue the value if the edit is modified and valid, otherwise empty
 * @return true if the edit is not modified or holds a valid value
 */
template <typename T>
static bool read_edit(
  wxWindow *pParent,
  wxTextCtrl &edit,
  const wxString &fieldName,
  const GOParamRange<T> &range,
  std::optional<T> &outValue) {
  wxString reason;
  double value = 0.0;

  outValue.reset();
  if (edit.IsModified()) {
    if (!edit.GetValue().ToDouble(&value))
      reason = _("The value must be a number");
    else if (std::is_integral_v<T> && value != std::floor(value))
      reason = _("The value must be an integer");
    else if (value < range.m_Min || value > range.m_Max) // before casting to T
      reason = wxString::Format(
        _("The value must be between %g and %g"),
        static_cast<double>(range.m_Min),
        static_cast<double>(range.m_Max));
    else
      outValue = static_cast<T>(value);
    if (!reason.IsEmpty()) {
      GOMessageBox(
        fieldName + wxT(": ") + reason,
        _("Error"),
        wxOK | wxICON_ERROR,
        pParent);
      edit.SetFocus();
    }
  }
  return reason.IsEmpty();
}

bool GOOrganSettingsTremulantsTab::ReadEdits(
  GOTremulant::SynthParamChanges &outChanges) {
  bool isOk = read_edit(
    this,
    *p_PeriodMsEdit,
    _("Period"),
    GOTremulant::PERIOD_MS_RANGE,
    outChanges.periodMs);

  // the next edits are read only if all the previous ones are valid, so only
  // the first invalid edit is reported
  isOk = isOk
    && read_edit(
           this,
           *p_AmpModDepthPcntEdit,
           _("Amplitude depth"),
           GOTremulant::AMP_MOD_DEPTH_PCNT_RANGE,
           outChanges.ampModDepthPcnt);
  isOk = isOk
    && read_edit(
           this,
           *p_PitchModDepthCentsEdit,
           _("Pitch depth"),
           GOTremulant::PITCH_MOD_DEPTH_CENTS_RANGE,
           outChanges.pitchModDepthCents);
  isOk = isOk
    && read_edit(
           this,
           *p_PitchModPhaseDegreesEdit,
           _("Pitch phase"),
           GOTremulant::PITCH_MOD_PHASE_DEGREES_RANGE,
           outChanges.pitchModPhaseDegrees);
  isOk = isOk
    && read_edit(
           this,
           *p_StartTimeMsEdit,
           _("Start time"),
           GOTremulant::START_TIME_MS_RANGE,
           outChanges.startTimeMs);
  isOk = isOk
    && read_edit(
           this,
           *p_StopTimeMsEdit,
           _("Stop time"),
           GOTremulant::STOP_TIME_MS_RANGE,
           outChanges.stopTimeMs);
  return isOk;
}

void GOOrganSettingsTremulantsTab::DisplaySelectedTremulantParameters() {
  unsigned nSelected = 0;
  unsigned nInternal = 0;
  unsigned nSynth = 0;
  const GOTremulant *pLastTremulant = nullptr;

  DoForSelectedTremulants([&](unsigned, GOTremulant &tremulant) {
    nSelected++;
    if (!tremulant.IsOdfDefined())
      nInternal++;
    if (tremulant.GetTremulantType() == GOSynthTrem)
      nSynth++;
    pLastTremulant = &tremulant;
  });

  const bool isAnySelected = nSelected > 0;
  const bool isAllSynth = isAnySelected && nSynth == nSelected;
  const bool isAllWave = isAnySelected && nSynth == 0;
  const bool isAllInternal = isAnySelected && nInternal == nSelected;
  const bool isAllOdfDefined = isAnySelected && nInternal == 0;
  // only the parameters of internal Synth tremulants may be edited
  const bool areEditable = isAllInternal && isAllSynth;

  p_IsOdfDefined->SetValue(isAllOdfDefined);
  p_TypeText->SetLabel(
    isAllSynth ? _("Synth") : (isAllWave ? _("Wave") : wxString()));
  p_SynthParamsPanel->Show(isAllSynth);
  if (nSelected == 1 && isAllSynth)
    FillEdits(*pLastTremulant);
  else
    ClearEdits();
  p_SynthParamsPanel->Enable(areEditable);
  m_IsDefaultEnabled = areEditable;
  Layout();
  NotifyModified(false);
}

bool GOOrganSettingsTremulantsTab::TransferDataToWindow() {
  auto rootItem = p_Tree->AddRoot(wxEmptyString, -1, -1);

  for (bool isOdfDefined : {false, true})
    for (unsigned n = r_OrganModel.GetTremulantCount(), tremulantI = 0;
         tremulantI < n;
         tremulantI++) {
      GOTremulant &tremulant = *r_OrganModel.GetTremulant(tremulantI);

      if (tremulant.IsOdfDefined() == isOdfDefined)
        p_Tree->AppendItem(
          rootItem,
          tremulant.GetName(),
          -1,
          -1,
          new ItemData(tremulantI, tremulant));
    }
  // nothing is selected yet: hides the Synth panel
  DisplaySelectedTremulantParameters();
  return true;
}

void GOOrganSettingsTremulantsTab::OnTreeChanging(wxTreeEvent &e) {
  if (CheckForUnapplied())
    e.Veto();
}

void GOOrganSettingsTremulantsTab::ApplyChanges() {
  GOTremulant::SynthParamChanges changes;

  if (ReadEdits(changes)) {
    std::set<unsigned> tremulantIndices;

    DoForSelectedTremulants([&](unsigned tremulantI, GOTremulant &tremulant) {
      if (tremulant.ApplySynthParamChanges(changes))
        tremulantIndices.insert(tremulantI);
    });
    // once; no-op if empty
    r_OrganModel.RebuildTremulantWaveForms(tremulantIndices);
    // shows the stored values and clears the modified flags of the edits
    DisplaySelectedTremulantParameters();
  }
}

void GOOrganSettingsTremulantsTab::ResetToDefault() {
  std::set<unsigned> tremulantIndices;

  DoForSelectedTremulants([&](unsigned tremulantI, GOTremulant &tremulant) {
    if (tremulant.ResetSynthParamsToDefault())
      tremulantIndices.insert(tremulantI);
  });
  r_OrganModel.RebuildTremulantWaveForms(tremulantIndices);
  DisplaySelectedTremulantParameters();
}
