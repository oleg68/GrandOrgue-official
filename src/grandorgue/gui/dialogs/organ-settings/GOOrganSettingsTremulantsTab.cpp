/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOOrganSettingsTremulantsTab.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <type_traits>

#include <wx/checkbox.h>
#include <wx/gbsizer.h>
#include <wx/panel.h>
#include <wx/spinbutt.h>
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

enum {
  ID_EVENT_TREE = 300,
  ID_EVENT_PERIOD_MS,
  ID_EVENT_AMP_MOD_DEPTH_PCNT,
  ID_EVENT_PITCH_MOD_DEPTH_CENTS,
  ID_EVENT_PITCH_MOD_PHASE_DEGREES,
  ID_EVENT_START_TIME_MS,
  ID_EVENT_STOP_TIME_MS,
};

// The id of the spin button of an edit is the id of the edit plus this offset
static constexpr int SPIN_ID_OFFSET = 100;

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
EVT_COMMAND_RANGE(
  ID_EVENT_PERIOD_MS + SPIN_ID_OFFSET,
  ID_EVENT_STOP_TIME_MS + SPIN_ID_OFFSET,
  wxEVT_SPIN,
  GOOrganSettingsTremulantsTab::OnSpinChanged)
END_EVENT_TABLE()

class GOOrganSettingsTremulantsTab::ItemData : public wxTreeItemData {
public:
  const unsigned m_TremulantI; // the index in the organ model
  GOTremulant &r_Tremulant;

  ItemData(unsigned tremulantI, GOTremulant &tremulant)
    : m_TremulantI(tremulantI), r_Tremulant(tremulant) {}
};

/**
 * Adds a row "label: edit, spin" to a grid sizer of a panel.
 * @tparam T the type of the parameter
 * @param panel the parent of the new controls
 * @param sizer the sizer of the panel
 * @param row the row of the sizer
 * @param label the text of the label
 * @param editId the id of the new edit; the id of the spin is
 *   editId + SPIN_ID_OFFSET
 * @param range the allowed range of the parameter; the range of the spin. All
 *   its bounds are integers (even for the float parameters)
 * @param outPEdit the new edit; it is owned by the panel
 * @param outPSpin the new spin button; it is owned by the panel
 */
template <typename T>
static void add_edit_row(
  wxPanel &panel,
  wxGridBagSizer &sizer,
  int row,
  const wxString &label,
  int editId,
  const GOParamRange<T> &range,
  wxTextCtrl *&outPEdit,
  wxSpinButton *&outPSpin) {
  outPEdit = new wxTextCtrl(
    &panel, editId, wxEmptyString, wxDefaultPosition, EDIT_SIZE);
  outPSpin = new wxSpinButton(&panel, editId + SPIN_ID_OFFSET);
  outPSpin->SetRange(
    static_cast<int>(range.m_Min), static_cast<int>(range.m_Max));

  sizer.Add(
    new wxStaticText(&panel, wxID_ANY, label),
    wxGBPosition(row, 0),
    wxDefaultSpan,
    wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxLEFT | wxBOTTOM,
    5);
  sizer.Add(
    outPEdit, wxGBPosition(row, 1), wxDefaultSpan, wxEXPAND | wxBOTTOM, 5);
  sizer.Add(
    outPSpin,
    wxGBPosition(row, 2),
    wxDefaultSpan,
    wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM,
    5);
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

  add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    0,
    _("Period (ms):"),
    ID_EVENT_PERIOD_MS,
    GOTremulant::PERIOD_MS_RANGE,
    p_PeriodMsEdit,
    p_PeriodMsSpin);
  add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    1,
    _("Amplitude depth (%):"),
    ID_EVENT_AMP_MOD_DEPTH_PCNT,
    GOTremulant::AMP_MOD_DEPTH_PCNT_RANGE,
    p_AmpModDepthPcntEdit,
    p_AmpModDepthPcntSpin);
  add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    2,
    _("Pitch depth (cents):"),
    ID_EVENT_PITCH_MOD_DEPTH_CENTS,
    GOTremulant::PITCH_MOD_DEPTH_CENTS_RANGE,
    p_PitchModDepthCentsEdit,
    p_PitchModDepthCentsSpin);
  add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    3,
    _("Pitch phase (degrees):"),
    ID_EVENT_PITCH_MOD_PHASE_DEGREES,
    GOTremulant::PITCH_MOD_PHASE_DEGREES_RANGE,
    p_PitchModPhaseDegreesEdit,
    p_PitchModPhaseDegreesSpin);
  add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    4,
    _("Start time (ms):"),
    ID_EVENT_START_TIME_MS,
    GOTremulant::START_TIME_MS_RANGE,
    p_StartTimeMsEdit,
    p_StartTimeMsSpin);
  add_edit_row(
    *p_SynthParamsPanel,
    *paramsSizer,
    5,
    _("Stop time (ms):"),
    ID_EVENT_STOP_TIME_MS,
    GOTremulant::STOP_TIME_MS_RANGE,
    p_StopTimeMsEdit,
    p_StopTimeMsSpin);
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

template <typename T>
bool GOOrganSettingsTremulantsTab::parseParam(
  const std::optional<wxString> &text,
  const GOParamRange<T> &range,
  EditField field,
  std::optional<T> &outValue,
  ParseError &outError) {
  std::optional<ParseProblem> problem;

  outValue.reset();
  if (text) {
    double value = 0.0;

    if (!text->ToDouble(&value))
      problem = ParseProblem::NOT_A_NUMBER;
    else if (std::is_integral_v<T> && value != std::floor(value))
      problem = ParseProblem::NOT_AN_INTEGER;
    else if (!range.Contains(value)) // before casting to T; false for NaN
      problem = ParseProblem::OUT_OF_RANGE;
    else
      outValue = static_cast<T>(value);
  }
  if (problem)
    outError = ParseError{
      field,
      *problem,
      static_cast<double>(range.m_Min),
      static_cast<double>(range.m_Max)};
  return !problem;
}

bool GOOrganSettingsTremulantsTab::parseEdits(
  const EditTexts &texts,
  GOTremulant::SynthParamChanges &outChanges,
  ParseError &outError) {
  outChanges = GOTremulant::SynthParamChanges();

  bool isOk = parseParam(
    texts.periodMs,
    GOTremulant::PERIOD_MS_RANGE,
    EditField::PERIOD_MS,
    outChanges.periodMs,
    outError);

  // the next edits are parsed only if all the previous ones are valid, so only
  // the first invalid edit is reported
  isOk = isOk
    && parseParam(
           texts.ampModDepthPcnt,
           GOTremulant::AMP_MOD_DEPTH_PCNT_RANGE,
           EditField::AMP_MOD_DEPTH_PCNT,
           outChanges.ampModDepthPcnt,
           outError);
  isOk = isOk
    && parseParam(
           texts.pitchModDepthCents,
           GOTremulant::PITCH_MOD_DEPTH_CENTS_RANGE,
           EditField::PITCH_MOD_DEPTH_CENTS,
           outChanges.pitchModDepthCents,
           outError);
  isOk = isOk
    && parseParam(
           texts.pitchModPhaseDegrees,
           GOTremulant::PITCH_MOD_PHASE_DEGREES_RANGE,
           EditField::PITCH_MOD_PHASE_DEGREES,
           outChanges.pitchModPhaseDegrees,
           outError);
  isOk = isOk
    && parseParam(
           texts.startTimeMs,
           GOTremulant::START_TIME_MS_RANGE,
           EditField::START_TIME_MS,
           outChanges.startTimeMs,
           outError);
  isOk = isOk
    && parseParam(
           texts.stopTimeMs,
           GOTremulant::STOP_TIME_MS_RANGE,
           EditField::STOP_TIME_MS,
           outChanges.stopTimeMs,
           outError);
  return isOk;
}

/**
 * Applies a change to the given tremulants and re-renders the really changed
 * ones with one call of GOOrganModel::RebuildTremulantWaveForms(). Does not
 * call it if nothing has changed.
 * @param organModel the organ model of the tremulants
 * @param tremulantIndices the indices of the tremulants in the organ model
 * @param change changes a tremulant; returns whether it has really changed
 */
static void change_and_rebuild(
  GOOrganModel &organModel,
  const std::set<unsigned> &tremulantIndices,
  const std::function<bool(GOTremulant &tremulant)> &change) {
  std::set<unsigned> changedIndices;

  for (unsigned tremulantI : tremulantIndices)
    if (change(*organModel.GetTremulant(tremulantI)))
      changedIndices.insert(tremulantI);
  if (!changedIndices.empty())
    organModel.RebuildTremulantWaveForms(changedIndices);
}

void GOOrganSettingsTremulantsTab::applyChanges(
  GOOrganModel &organModel,
  const std::set<unsigned> &tremulantIndices,
  const GOTremulant::SynthParamChanges &changes) {
  change_and_rebuild(organModel, tremulantIndices, [&](GOTremulant &tremulant) {
    return tremulant.ApplySynthParamChanges(changes);
  });
}

void GOOrganSettingsTremulantsTab::resetToDefault(
  GOOrganModel &organModel, const std::set<unsigned> &tremulantIndices) {
  change_and_rebuild(organModel, tremulantIndices, [](GOTremulant &tremulant) {
    return tremulant.ResetSynthParamsToDefault();
  });
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

std::set<unsigned> GOOrganSettingsTremulantsTab::GetSelectedTremulantIndices() {
  std::set<unsigned> tremulantIndices;

  DoForSelectedTremulants([&](unsigned tremulantI, GOTremulant &) {
    tremulantIndices.insert(tremulantI);
  });
  return tremulantIndices;
}

/**
 * Sets a spin button to the number in a text if it is a finite number. The
 * value is limited by the range of the spin button; a fraction is truncated.
 * @return whether the text is a finite number
 */
static bool set_spin_from_text(wxSpinButton &spin, const wxString &text) {
  double value = 0.0;
  const bool isNumber = text.ToDouble(&value) && std::isfinite(value);

  if (isNumber)
    spin.SetValue(static_cast<int>(std::clamp(
      value,
      static_cast<double>(spin.GetMin()),
      static_cast<double>(spin.GetMax()))));
  return isNumber;
}

/**
 * Puts a text into an edit as if the user had not edited it, and the number
 * of the text into its spin button (the minimum if the text is not a number).
 * ChangeValue() does not send EVT_TEXT, so the tab is not marked as modified.
 */
static void set_edit_text(
  wxTextCtrl &edit, wxSpinButton &spin, const wxString &text) {
  edit.ChangeValue(text);
  // ReadEdits() treats only the edits with the modified flag set as changed by
  // the user, and the flag left by an earlier typing is not guaranteed to be
  // cleared by ChangeValue()
  edit.DiscardEdits();
  if (!set_spin_from_text(spin, text))
    spin.SetValue(spin.GetMin());
}

void GOOrganSettingsTremulantsTab::FillEdits(const GOTremulant &tremulant) {
  set_edit_text(
    *p_PeriodMsEdit,
    *p_PeriodMsSpin,
    wxString::Format(WX_INT_FORMAT, tremulant.GetPeriodMs()));
  set_edit_text(
    *p_AmpModDepthPcntEdit,
    *p_AmpModDepthPcntSpin,
    wxString::Format(WX_INT_FORMAT, tremulant.GetAmpModDepthPcnt()));
  set_edit_text(
    *p_PitchModDepthCentsEdit,
    *p_PitchModDepthCentsSpin,
    wxString::Format(
      WX_PITCH_DEPTH_CENTS_FORMAT, tremulant.GetPitchModDepthCents()));
  set_edit_text(
    *p_PitchModPhaseDegreesEdit,
    *p_PitchModPhaseDegreesSpin,
    wxString::Format(WX_INT_FORMAT, tremulant.GetPitchModPhaseDegrees()));
  set_edit_text(
    *p_StartTimeMsEdit,
    *p_StartTimeMsSpin,
    wxString::Format(WX_INT_FORMAT, tremulant.GetStartTimeMs()));
  set_edit_text(
    *p_StopTimeMsEdit,
    *p_StopTimeMsSpin,
    wxString::Format(WX_INT_FORMAT, tremulant.GetStopTimeMs()));
}

void GOOrganSettingsTremulantsTab::ClearEdits() {
  set_edit_text(*p_PeriodMsEdit, *p_PeriodMsSpin, wxEmptyString);
  set_edit_text(*p_AmpModDepthPcntEdit, *p_AmpModDepthPcntSpin, wxEmptyString);
  set_edit_text(
    *p_PitchModDepthCentsEdit, *p_PitchModDepthCentsSpin, wxEmptyString);
  set_edit_text(
    *p_PitchModPhaseDegreesEdit, *p_PitchModPhaseDegreesSpin, wxEmptyString);
  set_edit_text(*p_StartTimeMsEdit, *p_StartTimeMsSpin, wxEmptyString);
  set_edit_text(*p_StopTimeMsEdit, *p_StopTimeMsSpin, wxEmptyString);
}

void GOOrganSettingsTremulantsTab::ShowParseError(const ParseError &error) {
  wxTextCtrl *pEdit = nullptr;
  wxString fieldName;
  wxString reason;

  switch (error.field) {
  case EditField::PERIOD_MS:
    pEdit = p_PeriodMsEdit;
    fieldName = _("Period");
    break;
  case EditField::AMP_MOD_DEPTH_PCNT:
    pEdit = p_AmpModDepthPcntEdit;
    fieldName = _("Amplitude depth");
    break;
  case EditField::PITCH_MOD_DEPTH_CENTS:
    pEdit = p_PitchModDepthCentsEdit;
    fieldName = _("Pitch depth");
    break;
  case EditField::PITCH_MOD_PHASE_DEGREES:
    pEdit = p_PitchModPhaseDegreesEdit;
    fieldName = _("Pitch phase");
    break;
  case EditField::START_TIME_MS:
    pEdit = p_StartTimeMsEdit;
    fieldName = _("Start time");
    break;
  case EditField::STOP_TIME_MS:
    pEdit = p_StopTimeMsEdit;
    fieldName = _("Stop time");
    break;
  }
  switch (error.problem) {
  case ParseProblem::NOT_A_NUMBER:
    reason = _("The value must be a number");
    break;
  case ParseProblem::NOT_AN_INTEGER:
    reason = _("The value must be an integer");
    break;
  case ParseProblem::OUT_OF_RANGE:
    // All bounds are integers (even for the float parameters), so "%g" prints
    // them without a decimal part; it would also print a non-integer bound.
    reason = wxString::Format(
      _("The value must be between %g and %g"), error.rangeMin, error.rangeMax);
    break;
  }
  GOMessageBox(
    fieldName + wxT(": ") + reason, _("Error"), wxOK | wxICON_ERROR, this);
  pEdit->SetFocus();
}

/**
 * @return the text of the edit if it was modified by the user, otherwise empty
 */
static std::optional<wxString> get_modified_text(const wxTextCtrl &edit) {
  return edit.IsModified() ? std::optional<wxString>(edit.GetValue())
                           : std::nullopt;
}

bool GOOrganSettingsTremulantsTab::ReadEdits(
  GOTremulant::SynthParamChanges &outChanges) {
  const EditTexts texts{
    get_modified_text(*p_PeriodMsEdit),
    get_modified_text(*p_AmpModDepthPcntEdit),
    get_modified_text(*p_PitchModDepthCentsEdit),
    get_modified_text(*p_PitchModPhaseDegreesEdit),
    get_modified_text(*p_StartTimeMsEdit),
    get_modified_text(*p_StopTimeMsEdit)};
  ParseError error{};
  const bool isOk = parseEdits(texts, outChanges, error);

  if (!isOk)
    ShowParseError(error);
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

void GOOrganSettingsTremulantsTab::OnEditChanged(wxCommandEvent &e) {
  wxSpinButton *const pSpin = static_cast<wxSpinButton *>(
    p_SynthParamsPanel->FindWindow(e.GetId() + SPIN_ID_OFFSET));

  set_spin_from_text(*pSpin, e.GetString());
  NotifyModified();
}

/**
 * Formats the value of a spin button as the text of its edit.
 * @param editId the id of the edit
 * @param spinValue the value of the spin button
 */
static wxString format_spin_value(int editId, int spinValue) {
  return editId == ID_EVENT_PITCH_MOD_DEPTH_CENTS
    ? wxString::Format(
      WX_PITCH_DEPTH_CENTS_FORMAT, static_cast<double>(spinValue))
    : wxString::Format(WX_INT_FORMAT, spinValue);
}

void GOOrganSettingsTremulantsTab::OnSpinChanged(wxCommandEvent &e) {
  const int editId = e.GetId() - SPIN_ID_OFFSET;
  const wxSpinButton *const pSpin
    = static_cast<wxSpinButton *>(p_SynthParamsPanel->FindWindow(e.GetId()));
  wxTextCtrl *const pEdit
    = static_cast<wxTextCtrl *>(p_SynthParamsPanel->FindWindow(editId));

  pEdit->ChangeValue(format_spin_value(editId, pSpin->GetValue()));
  // ChangeValue() clears the modified flag, but ReadEdits() needs it
  pEdit->MarkDirty();
  NotifyModified();
}

void GOOrganSettingsTremulantsTab::ApplyChanges() {
  GOTremulant::SynthParamChanges changes;

  if (ReadEdits(changes)) {
    applyChanges(r_OrganModel, GetSelectedTremulantIndices(), changes);
    // shows the stored values and clears the modified flags of the edits
    DisplaySelectedTremulantParameters();
  }
}

void GOOrganSettingsTremulantsTab::ResetToDefault() {
  resetToDefault(r_OrganModel, GetSelectedTremulantIndices());
  DisplaySelectedTremulantParameters();
}
