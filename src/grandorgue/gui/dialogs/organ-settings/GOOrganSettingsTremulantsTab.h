/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOORGANSETTINGSTREMULANTSTAB_H
#define GOORGANSETTINGSTREMULANTSTAB_H

#include <functional>
#include <optional>
#include <set>

#include <wx/event.h>
#include <wx/string.h>

#include "model/GOTremulant.h"

#include "GOOrganSettingsTab.h"

class wxCheckBox;
class wxPanel;
class wxSpinButton;
class wxStaticText;
class wxTextCtrl;
class wxTreeCtrl;
class wxTreeEvent;

class GOOrganModel;

/**
 * The Organ Settings tab that displays all tremulants of the organ and edits
 * the parameters of the internal (not defined in the ODF) Synth tremulants.
 * Applying the changes re-renders the wave forms of the changed tremulants in
 * the running sound engine (GOOrganModel::RebuildTremulantWaveForms()).
 */
class GOOrganSettingsTremulantsTab : public GOOrganSettingsTab {
private:
  // tests the logic that does not depend on the widgets without creating a tab
  friend class GOTestOrganSettingsTremulantsTab;

  class ItemData;

  // The texts of the modified edits; empty if the edit is not modified
  struct EditTexts {
    std::optional<wxString> periodMs;
    std::optional<wxString> ampModDepthPcnt;
    std::optional<wxString> pitchModDepthCents;
    std::optional<wxString> pitchModPhaseDegrees;
    std::optional<wxString> startTimeMs;
    std::optional<wxString> stopTimeMs;
  };

  // The edits from top to bottom
  enum class EditField {
    PERIOD_MS,
    AMP_MOD_DEPTH_PCNT,
    PITCH_MOD_DEPTH_CENTS,
    PITCH_MOD_PHASE_DEGREES,
    START_TIME_MS,
    STOP_TIME_MS
  };

  enum class ParseProblem { NOT_A_NUMBER, NOT_AN_INTEGER, OUT_OF_RANGE };

  // The first invalid edit
  struct ParseError {
    EditField field;
    ParseProblem problem;
    // the allowed range of the field, for the message
    double rangeMin;
    double rangeMax;
  };

  GOOrganModel &r_OrganModel;

  wxTreeCtrl *p_Tree;
  wxCheckBox *p_IsOdfDefined;
  wxStaticText *p_TypeText;
  // contains the edits of the Synth parameters; shown only for Synth
  // tremulants
  wxPanel *p_SynthParamsPanel;
  wxTextCtrl *p_PeriodMsEdit;
  wxSpinButton *p_PeriodMsSpin;
  wxTextCtrl *p_AmpModDepthPcntEdit;
  wxSpinButton *p_AmpModDepthPcntSpin;
  wxTextCtrl *p_PitchModDepthCentsEdit;
  wxSpinButton *p_PitchModDepthCentsSpin;
  wxTextCtrl *p_PitchModPhaseDegreesEdit;
  wxSpinButton *p_PitchModPhaseDegreesSpin;
  wxTextCtrl *p_StartTimeMsEdit;
  wxSpinButton *p_StartTimeMsSpin;
  wxTextCtrl *p_StopTimeMsEdit;
  wxSpinButton *p_StopTimeMsSpin;

  /**
   * Parses and validates the text of one edit.
   * @tparam T the type of the parameter
   * @param text the text of the edit; empty if the edit is not modified
   * @param range the allowed range of the parameter
   * @param field the edit, for the error
   * @param outValue the parsed value; empty if the text is empty or invalid
   * @param outError filled only if the text is invalid
   * @return whether the text is empty or holds a valid value
   */
  template <typename T>
  static bool parseParam(
    const std::optional<wxString> &text,
    const GOParamRange<T> &range,
    EditField field,
    std::optional<T> &outValue,
    ParseError &outError);

  /**
   * Parses and validates the texts of the modified edits. Reports only the
   * first invalid edit, top to bottom.
   * @param texts the texts of the modified edits
   * @param outChanges the valid modified values; unspecified if not all are
   *   valid
   * @param outError the first invalid edit; filled only if not all are valid
   * @return whether all present texts are valid
   */
  static bool parseEdits(
    const EditTexts &texts,
    GOTremulant::SynthParamChanges &outChanges,
    ParseError &outError);

  /**
   * Applies the changes to the given tremulants. Re-renders the really changed
   * ones with one call of GOOrganModel::RebuildTremulantWaveForms(); does not
   * call it if nothing has changed.
   * @param organModel the organ model of the tremulants
   * @param tremulantIndices the indices of the tremulants in the organ model
   * @param changes the changes to apply
   */
  static void applyChanges(
    GOOrganModel &organModel,
    const std::set<unsigned> &tremulantIndices,
    const GOTremulant::SynthParamChanges &changes);

  /**
   * Sets the given tremulants to their default parameters. Re-renders the
   * really changed ones in the same way as applyChanges().
   * @param organModel the organ model of the tremulants
   * @param tremulantIndices the indices of the tremulants in the organ model
   */
  static void resetToDefault(
    GOOrganModel &organModel, const std::set<unsigned> &tremulantIndices);

  /**
   * Calls f for every selected tremulant.
   * @param f receives the index of the tremulant in the organ model and the
   *   tremulant itself
   */
  void DoForSelectedTremulants(
    const std::function<void(unsigned tremulantI, GOTremulant &tremulant)> &f);

  /** @return the indices of the selected tremulants in the organ model */
  std::set<unsigned> GetSelectedTremulantIndices();

  /** Fills the edits with the parameters of a Synth tremulant. */
  void FillEdits(const GOTremulant &tremulant);

  /** Empties the edits. */
  void ClearEdits();

  /**
   * Shows the reason why an edit is invalid and moves the focus to the edit.
   * @param error the first invalid edit
   */
  void ShowParseError(const ParseError &error);

  /**
   * Reads the modified edits and validates them against the ranges of
   * GOTremulant. Shows an error message for the first invalid one and moves the
   * focus to it.
   * @param outChanges the valid modified values
   * @return whether all modified edits are valid
   */
  bool ReadEdits(GOTremulant::SynthParamChanges &outChanges);

  /**
   * Displays the type, the ODF flag and the parameters of the selected
   * tremulants, and enables the edits only if every selected tremulant is
   * internal.
   */
  void DisplaySelectedTremulantParameters();

  bool TransferDataToWindow() override;
  void OnTreeChanging(wxTreeEvent &e);
  void OnTreeChanged(wxTreeEvent &e) { DisplaySelectedTremulantParameters(); }
  void OnEditChanged(wxCommandEvent &e);
  void OnSpinChanged(wxCommandEvent &e);

public:
  GOOrganSettingsTremulantsTab(
    GOOrganModel &organModel, GOOrganSettingsDialogBase *pDlg);

  void ResetToDefault() override;
  void DiscardChanges() override { DisplaySelectedTremulantParameters(); }
  void ApplyChanges() override;

  DECLARE_EVENT_TABLE()
};

#endif /* GOORGANSETTINGSTREMULANTSTAB_H */
