/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiObject.h"

#include <wx/intl.h>

#include "midi/elements/GOMidiReceiver.h"
#include "midi/elements/GOMidiSender.h"
#include "midi/elements/GOMidiShortcutReceiver.h"
#include "model/GOOrganModel.h"
#include "yaml/go-wx-yaml.h"

#include "GOMidiObjectContext.h"

#include "GOMidiObjectContext.h"

GOMidiObject::GOMidiObject(
  GOOrganModel &organModel,
  const wxString &midiTypeCode,
  const wxString &midiType)
  : r_OrganModel(organModel),
    r_MidiMap(organModel.GetConfig().GetMidiMap()),
    r_MidiTypeCode(midiTypeCode),
    r_MidiTypeName(midiType),
    p_MidiSender(nullptr),
    p_MidiReceiver(nullptr),
    p_ShortcutReceiver(nullptr),
    p_DivisionSender(nullptr),
    p_context(nullptr) {
  r_OrganModel.RegisterSoundStateHandler(this);
  r_OrganModel.RegisterMidiObject(this);
}

GOMidiObject::~GOMidiObject() {
  r_OrganModel.UnregisterSaveableObject(this);
  r_OrganModel.UnRegisterMidiObject(this);
  r_OrganModel.UnRegisterSoundStateHandler(this);
}

wxString GOMidiObject::GetContextTitle() const {
  return GOMidiObjectContext::getFullTitle(p_context);
}

bool GOMidiObject::IsMidiConfigured() const {
  return (p_MidiSender && p_MidiSender->IsMidiConfigured())
    || (p_MidiReceiver && p_MidiReceiver->IsMidiConfigured())
    || (p_ShortcutReceiver && p_ShortcutReceiver->IsMidiConfigured())
    || (p_DivisionSender && p_DivisionSender->IsMidiConfigured());
}

void GOMidiObject::InitMidiObject(
  GOConfigReader &cfg, const wxString &group, const wxString &name) {
  SetGroup(group);
  m_name = name;
  r_OrganModel.RegisterSaveableObject(this);
  LoadMidiObject(cfg, group, r_MidiMap);
}

void GOMidiObject::SubToYaml(
  YAML::Node &yamlNode, const char *pSubName, const GOMidiElement *pEl) const {
  if (pEl) {
    YAML::Node subNode;

    pEl->ToYaml(subNode, r_MidiMap);
    put_to_map_if_not_null(yamlNode, pSubName, subNode);
  }
}

void GOMidiObject::SubFromYaml(
  const YAML::Node &yamlNode, const char *pSubName, GOMidiElement *pEl) {
  if (pEl) {
    YAML::Node subNode = get_from_map_or_null(yamlNode, pSubName);

    pEl->FromYaml(subNode, r_MidiMap);
  }
}

const char *C_RECEIVE = "receive";
const char *C_SEND = "send";
const char *C_SHORTCUT = "shortcut";
const char *C_DIVISION = "division";

void GOMidiObject::ToYaml(YAML::Node &yamlNode) const {
  YAML::Node objNode;

  SubToYaml(objNode, C_RECEIVE, p_MidiReceiver);
  SubToYaml(objNode, C_SEND, p_MidiSender);
  SubToYaml(objNode, C_SHORTCUT, p_ShortcutReceiver);
  SubToYaml(objNode, C_DIVISION, p_DivisionSender);
  put_to_map_by_path_if_not_null(
    yamlNode,
    GOMidiObjectContext::getNames(p_context),
    GetNameForContext(),
    objNode);
}

void GOMidiObject::FromYaml(const YAML::Node &yamlNode) {
  YAML::Node objNode = get_from_map_by_path_or_null(
    yamlNode, GOMidiObjectContext::getNames(p_context), GetNameForContext());

  SubFromYaml(objNode, C_RECEIVE, p_MidiReceiver);
  SubFromYaml(objNode, C_SEND, p_MidiSender);
  SubFromYaml(objNode, C_SHORTCUT, p_ShortcutReceiver);
  SubFromYaml(objNode, C_DIVISION, p_DivisionSender);
}

void GOMidiObject::ShowConfigDialog() {
  const bool isReadOnly = IsReadOnly();
  const wxString title
    = wxString::Format(_("Midi-Settings for %s - %s"), r_MidiTypeName, m_name);
  const wxString selector
    = wxString::Format(wxT("%s.%s"), r_MidiTypeCode, m_name);

  r_OrganModel.ShowMIDIEventDialog(
    this,
    title,
    selector,
    isReadOnly ? nullptr : p_MidiReceiver,
    p_MidiSender,
    isReadOnly ? nullptr : p_ShortcutReceiver,
    p_DivisionSender,
    this);
}
