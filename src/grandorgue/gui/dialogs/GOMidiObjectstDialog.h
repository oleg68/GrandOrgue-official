/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDIOBJECTSDIALOG_H
#define GOMIDIOBJECTSDIALOG_H

#include <vector>

#include "common/GOSimpleDialog.h"
#include "document-base/GOView.h"

class wxButton;
class wxGridEvent;

class GOConfig;
class GOGrid;
class GOMidiObject;
class GOOrganModel;

class GOMidiObjectsDialog : public GOSimpleDialog, public GOView {
private:
  wxString m_ExportImportDir;
  wxString m_OrganName;
  const std::vector<GOMidiObject *> &r_MidiObjects;

  GOGrid *m_ObjectsGrid;
  wxButton *m_ConfigureButton;
  wxButton *m_StatusButton;
  std::vector<wxButton *> m_ActionButtons;

  wxButton *m_ExportButton;
  wxButton *m_ImportButton;

public:
  GOMidiObjectsDialog(
    GODocumentBase *doc, wxWindow *parent, GOOrganModel &organModel);

private:
  void ApplyAdditionalSizes(const GOAdditionalSizeKeeper &sizeKeeper) override;
  void CaptureAdditionalSizes(
    GOAdditionalSizeKeeper &sizeKeeper) const override;

  bool TransferDataToWindow() override;

  GOMidiObject *GetSelectedObject() const;
  void ConfigureSelectedObject();

  void OnSelectCell(wxGridEvent &event);
  void OnObjectDoubleClick(wxGridEvent &event) { ConfigureSelectedObject(); }
  void OnConfigureButton(wxCommandEvent &event) { ConfigureSelectedObject(); }
  void OnStatusButton(wxCommandEvent &event);
  void OnActionButton(wxCommandEvent &event);

  bool IsToImportSettingsFor(
    const wxString &fileName, const wxString &savedOrganName) const;
  wxString ExportMidiSettings(const wxString &fileName) const;
  wxString ImportMidiSettings(const wxString &fileName);

  void OnExportButton(wxCommandEvent &event);
  void OnImportButton(wxCommandEvent &event);

  DECLARE_EVENT_TABLE()
};

#endif // GOMIDIOBJECTSDIALOG_H
