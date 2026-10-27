/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOORGANSETTINGSTREMULANTSTAB_H
#define GOORGANSETTINGSTREMULANTSTAB_H

#include <functional>

#include <wx/event.h>

#include "model/GOTremulant.h"

#include "GOOrganSettingsTab.h"

class wxCheckBox;
class wxPanel;
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
  class ItemData;

  GOOrganModel &r_OrganModel;

  wxTreeCtrl *p_Tree;
  wxCheckBox *p_IsOdfDefined;
  wxStaticText *p_TypeText;
  // contains the edits of the Synth parameters; shown only for Synth
  // tremulants
  wxPanel *p_SynthParamsPanel;
  wxTextCtrl *p_PeriodMsEdit;
  wxTextCtrl *p_AmpModDepthPcntEdit;
  wxTextCtrl *p_PitchModDepthCentsEdit;
  wxTextCtrl *p_PitchModPhaseDegreesEdit;
  wxTextCtrl *p_StartTimeMsEdit;
  wxTextCtrl *p_StopTimeMsEdit;

  /**
   * Calls f for every selected tremulant.
   * @param f receives the index of the tremulant in the organ model and the
   *   tremulant itself
   */
  void DoForSelectedTremulants(
    const std::function<void(unsigned tremulantI, GOTremulant &tremulant)> &f);

  /** Fills the edits with the parameters of a Synth tremulant. */
  void FillEdits(const GOTremulant &tremulant);

  /** Empties the edits. */
  void ClearEdits();

  /**
   * Reads the modified edits and validates them against the ranges of
   * GOTremulant. Shows an error message for every invalid one.
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
  void OnEditChanged(wxCommandEvent &e) { NotifyModified(); }

public:
  GOOrganSettingsTremulantsTab(
    GOOrganModel &organModel, GOOrganSettingsDialogBase *pDlg);

  void ResetToDefault() override;
  void DiscardChanges() override { DisplaySelectedTremulantParameters(); }
  void ApplyChanges() override;

  DECLARE_EVENT_TABLE()
};

#endif /* GOORGANSETTINGSTREMULANTSTAB_H */
