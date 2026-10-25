/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOELEMENTCREATOR_H
#define GOELEMENTCREATOR_H

#include <wx/string.h>

#include "GOButtonCallback.h"
#include "ptrvector.h"

class GOButtonControl;
class GOConfigReader;
class GOEnclosure;
class GOLabelControl;
class GOMidiObjectContext;
class GOOrganModel;

class GOElementCreator : private GOButtonCallback {
public:
  struct ButtonDefinitionEntry {
    wxString name;
    int value;
    bool is_public;
    bool is_pushbutton;
    bool is_piston;
    const GOMidiObjectContext *p_MidiContext = nullptr;
  };

  const ButtonDefinitionEntry *p_ButtonDefinitions;

protected:
  ptr_vector<GOButtonControl> m_buttons;

  void CreateButtons(
    GOOrganModel &organModel, const ButtonDefinitionEntry *pEntries);
  virtual void ButtonStateChanged(int id, bool newState) = 0;

public:
  virtual void Load(GOConfigReader &cfg) = 0;

  /**
   * The counterpart of Load(): releases everything that Load() loaded or
   * registered and returns the creator to the state after the constructor.
   * It must keep what the constructor created (the buttons, the labels and
   * their registrations). Overridden by the creators that survive Clear()
   * (GOSetter) or that add objects to the model (GOMetronome adds a rank).
   * Idempotent. Called by
   * GOOrganController::ClearOrganCoreData() for every creator in the reverse
   * order of loading, before the creators made by LoadOrganCoreData() are
   * deleted. The default does nothing.
   *
   * It is not named Clear() because GOMidiRecorder::Clear() already means
   * erasing the recorded events, and not Cleanup() because that is the
   * counterpart of Setup().
   */
  virtual void ClearLoaded() {}

  virtual GOEnclosure *GetEnclosure(const wxString &name, bool is_panel) = 0;
  virtual GOLabelControl *GetLabelControl(const wxString &name, bool is_panel)
    = 0;
  virtual GOButtonControl *GetButtonControl(
    const wxString &name, bool is_panel);

  void ButtonStateChanged(GOButtonControl *button, bool newState) override;
};

#endif
