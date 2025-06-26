/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSettingsMidiInitial.h"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "config/GOConfig.h"
#include "gui/dialogs/midi-event/GOMidiEventDialog.h"

BEGIN_EVENT_TABLE(GOSettingsMidiInitial, wxPanel)
EVT_LIST_ITEM_SELECTED(ID_EVENTS, GOSettingsMidiInitial::OnEventsClick)
EVT_LIST_ITEM_ACTIVATED(ID_EVENTS, GOSettingsMidiInitial::OnEventsDoubleClick)
EVT_BUTTON(ID_PROPERTIES, GOSettingsMidiInitial::OnProperties)
END_EVENT_TABLE()

GOSettingsMidiInitial::GOSettingsMidiInitial(
  GOConfig &settings, GOMidi &midi, wxWindow *parent)
  : wxPanel(parent, wxID_ANY), m_config(settings), m_midi(midi) {
  wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);
  topSizer->AddSpacer(5);
  topSizer->Add(
    new wxStaticText(
      this,
      wxID_ANY,
      _("Attention:\nThese initial MIDI settings only affect "
        "the first load of a organ.\nRight-click on manuals, "
        "stops, ... to do further changes.")),
    0,
    wxALL);
  topSizer->AddSpacer(5);

  m_Initials = new wxListView(
    this,
    ID_EVENTS,
    wxDefaultPosition,
    wxDefaultSize,
    wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
  m_Initials->InsertColumn(0, _("Group"));
  m_Initials->InsertColumn(1, _("Element"));
  m_Initials->InsertColumn(2, _("MIDI Event"));
  topSizer->Add(m_Initials, 1, wxEXPAND | wxALL, 5);
  m_Properties = new wxButton(this, ID_PROPERTIES, _("P&roperties..."));
  m_Properties->Disable();
  topSizer->Add(m_Properties, 0, wxALIGN_RIGHT | wxALL, 5);

  for (unsigned l = m_config.GetMidiInitialCount(), i = 0; i < l; i++) {
    const GOConfigMidiObject *pObj = m_config.GetMidiInitialObject(i);

    m_Initials->InsertItem(i, m_config.GetInitialMidiGroup(i));
    m_Initials->SetItemPtrData(i, (wxUIntPtr)pObj);
    m_Initials->SetItem(i, 1, m_config.GetInitialMidiName(i));
    m_Initials->SetItem(i, 2, pObj->IsMidiConfigured() ? _("Yes") : _("No"));
  }

  topSizer->AddSpacer(5);
  this->SetSizer(topSizer);
  topSizer->Fit(this);

  m_Initials->SetColumnWidth(0, wxLIST_AUTOSIZE);
  m_Initials->SetColumnWidth(1, wxLIST_AUTOSIZE);
  m_Initials->SetColumnWidth(2, wxLIST_AUTOSIZE_USEHEADER);
}

void GOSettingsMidiInitial::OnEventsClick(wxListEvent &event) {
  m_Properties->Enable();
}

void GOSettingsMidiInitial::OnEventsDoubleClick(wxListEvent &event) {
  m_Properties->Enable();
  int index = m_Initials->GetFirstSelected();

  GOConfigMidiObject *pObj = (GOConfigMidiObject *)m_Initials->GetItemData(
    m_Initials->GetFirstSelected());
  GOMidiEventDialog dlg(
    nullptr,
    this,
    wxString::Format(
      _("Initial MIDI settings for %s"), m_config.GetInitialMidiName(index)),
    m_config,
    wxT("InitialSettings"),
    *pObj);
  dlg.RegisterMIDIListener(&m_midi);
  dlg.ShowModal();
  m_Initials->SetItem(index, 2, pObj->IsMidiConfigured() ? _("Yes") : _("No"));
}

void GOSettingsMidiInitial::OnProperties(wxCommandEvent &event) {
  wxListEvent listevent;
  OnEventsDoubleClick(listevent);
}
