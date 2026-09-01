/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOEnclosure.h"

#include <wx/intl.h>

#include "config/GOConfigReader.h"
#include "config/GOConfigWriter.h"

#include "GOOrganModel.h"

GOEnclosure::GOEnclosure(GOOrganModel &organModel)
  : GOMidiObjectWithShortcut(
    organModel,
    OBJECT_TYPE_ENCLOSURE,
    MIDI_SEND_ENCLOSURE,
    MIDI_RECV_ENCLOSURE,
    KEY_RECV_ENCLOSURE),
    m_IsOdfDefined(false),
    m_DefaultAmpMinimumLevel(0),
    m_Displayed1(false),
    m_Displayed2(false),
    m_AmpMinimumLevel(0),
    m_MIDIValue(0),
    m_DefaultLowShelfFrequency(0.0f),
    m_DefaultLowShelfAttenuationDb(0.0f),
    m_DefaultHighShelfFrequency(0.0f),
    m_DefaultHighShelfAttenuationDb(0.0f),
    m_LowShelfFrequency(0.0f),
    m_LowShelfAttenuationDb(0.0f),
    m_HighShelfFrequency(0.0f),
    m_HighShelfAttenuationDb(0.0f) {}

static const wxString WX_AMP_MINIMUM_LEVEL = wxT("AmpMinimumLevel");
static const wxString WX_VALUE = wxT("Value");
static const wxString WX_LOW_SHELF_FREQUENCY = wxT("LowShelfFrequency");
static const wxString WX_LOW_SHELF_ATTENUATION_DB
  = wxT("LowShelfAttenuationDb");
static const wxString WX_HIGH_SHELF_FREQUENCY = wxT("HighShelfFrequency");
static const wxString WX_HIGH_SHELF_ATTENUATION_DB
  = wxT("HighShelfAttenuationDb");

void GOEnclosure::LoadFromCmb(GOConfigReader &cfg, uint8_t defaultValue) {
  m_AmpMinimumLevel = cfg.ReadInteger(
    CMBSetting,
    m_group,
    WX_AMP_MINIMUM_LEVEL,
    0,
    100,
    false,
    m_DefaultAmpMinimumLevel);
  m_LowShelfFrequency = cfg.ReadFloat(
    CMBSetting,
    m_group,
    WX_LOW_SHELF_FREQUENCY,
    0,
    0,
    false,
    m_DefaultLowShelfFrequency);
  m_LowShelfAttenuationDb = cfg.ReadFloat(
    CMBSetting,
    m_group,
    WX_LOW_SHELF_ATTENUATION_DB,
    0,
    121,
    false,
    m_DefaultLowShelfAttenuationDb);
  m_HighShelfFrequency = cfg.ReadFloat(
    CMBSetting,
    m_group,
    WX_HIGH_SHELF_FREQUENCY,
    0,
    0,
    false,
    m_DefaultHighShelfFrequency);
  m_HighShelfAttenuationDb = cfg.ReadFloat(
    CMBSetting,
    m_group,
    WX_HIGH_SHELF_ATTENUATION_DB,
    0,
    121,
    false,
    m_DefaultHighShelfAttenuationDb);
  SetEnclosureValue(cfg.ReadInteger(
    CMBSetting, m_group, WX_VALUE, 0, MAX_MIDI_VALUE, false, defaultValue));
}

void GOEnclosure::Init(
  GOConfigReader &cfg,
  const wxString &group,
  const wxString &name,
  uint8_t defaultValue) {
  m_IsOdfDefined = false;
  GOMidiReceivingSendingObject::Init(cfg, group, name);
  m_DefaultAmpMinimumLevel = 0;
  m_DefaultLowShelfFrequency = 0.0f;
  m_DefaultLowShelfAttenuationDb = 0.0f;
  m_DefaultHighShelfFrequency = 0.0f;
  m_DefaultHighShelfAttenuationDb = 0.0f;
  LoadFromCmb(cfg, defaultValue);
}

void GOEnclosure::Load(GOConfigReader &cfg, const wxString &group) {
  m_IsOdfDefined = true;
  GOMidiReceivingSendingObject::Load(
    cfg, group, cfg.ReadStringNotEmpty(ODFSetting, group, wxT("Name")), true);
  m_Displayed1
    = cfg.ReadBoolean(ODFSetting, m_group, wxT("Displayed"), false, true);
  m_Displayed2
    = cfg.ReadBoolean(ODFSetting, m_group, wxT("Displayed"), false, false);
  m_DefaultAmpMinimumLevel
    = cfg.ReadInteger(ODFSetting, m_group, WX_AMP_MINIMUM_LEVEL, 0, 100);
  m_DefaultLowShelfAttenuationDb = cfg.ReadFloat(
    ODFSetting, m_group, WX_LOW_SHELF_ATTENUATION_DB, 0, 121, false, 0);
  m_DefaultLowShelfFrequency = m_DefaultLowShelfAttenuationDb > 0
    ? cfg.ReadFloat(ODFSetting, m_group, WX_LOW_SHELF_FREQUENCY, 0, 0, true)
    : 0.0f;
  m_DefaultHighShelfAttenuationDb = cfg.ReadFloat(
    ODFSetting, m_group, WX_HIGH_SHELF_ATTENUATION_DB, 0, 121, false, 0);
  m_DefaultHighShelfFrequency = m_DefaultHighShelfAttenuationDb > 0
    ? cfg.ReadFloat(ODFSetting, m_group, WX_HIGH_SHELF_FREQUENCY, 0, 0, true)
    : 0.0f;
  LoadFromCmb(cfg, MAX_MIDI_VALUE);
}

void GOEnclosure::Save(GOConfigWriter &cfg) {
  GOMidiReceivingSendingObject::Save(cfg);
  cfg.WriteInteger(m_group, WX_AMP_MINIMUM_LEVEL, m_AmpMinimumLevel);
  cfg.WriteInteger(m_group, WX_VALUE, m_MIDIValue);
  cfg.WriteFloat(m_group, WX_LOW_SHELF_FREQUENCY, m_LowShelfFrequency);
  cfg.WriteFloat(m_group, WX_LOW_SHELF_ATTENUATION_DB, m_LowShelfAttenuationDb);
  cfg.WriteFloat(m_group, WX_HIGH_SHELF_FREQUENCY, m_HighShelfFrequency);
  cfg.WriteFloat(
    m_group, WX_HIGH_SHELF_ATTENUATION_DB, m_HighShelfAttenuationDb);
}

void GOEnclosure::SetEnclosureValue(uint8_t n) {
  if (n != m_MIDIValue) {
    m_MIDIValue = n;
    SendCurrentMidiValue();
  }
  r_OrganModel.UpdateVolume();
  r_OrganModel.SendControlChanged(this);
}

void GOEnclosure::Scroll(bool scroll_up) {
  SetIntEnclosureValue(m_MIDIValue + (scroll_up ? 4 : -4));
}

void GOEnclosure::OnMidiReceived(
  const GOMidiEvent &event, GOMidiMatchType matchType, int key, int value) {
  if (matchType == MIDI_MATCH_CHANGE)
    SetEnclosureValue(value);
}

void GOEnclosure::OnShortcutKeyReceived(
  GOMidiShortcutReceiver::MatchType matchType, int key) {
  switch (matchType) {
  case GOMidiShortcutReceiver::KEY_MATCH:
    SetIntEnclosureValue(m_MIDIValue + 8);
    break;

  case GOMidiShortcutReceiver::KEY_MATCH_MINUS:
    SetIntEnclosureValue(m_MIDIValue - 8);
    break;
  default:
    break;
  }
}

bool GOEnclosure::IsDisplayed(bool new_format) {
  if (new_format)
    return m_Displayed2;
  else
    return m_Displayed1;
}

wxString GOEnclosure::GetElementStatus() {
  return wxString::Format(_("%.3f %%"), (m_MIDIValue * 100.0 / 127));
}

std::vector<wxString> GOEnclosure::GetElementActions() {
  std::vector<wxString> actions;
  actions.push_back(_("-"));
  actions.push_back(_("+"));
  return actions;
}

void GOEnclosure::TriggerElementActions(unsigned no) {
  if (no == 0)
    Scroll(false);
  if (no == 1)
    Scroll(true);
}
