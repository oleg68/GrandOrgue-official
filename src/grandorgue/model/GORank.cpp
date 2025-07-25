/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GORank.h"

#include <algorithm>

#include <wx/intl.h>

#include "config/GOConfigReader.h"

#include "GODummyPipe.h"
#include "GOOrganModel.h"
#include "GOReferencePipe.h"
#include "GOSoundingPipe.h"
#include "GOWindchest.h"

GORank::GORank(GOOrganModel &organModel)
  : GOMidiSendingObject(organModel, OBJECT_TYPE_RANK, MIDI_SEND_MANUAL),
    r_OrganModel(organModel),
    m_StopCount(0),
    m_NoteStopVelocities(),
    m_MaxNoteVelocities(),
    m_FirstMidiNoteNumber(0),
    m_WindchestN(0),
    m_HarmonicNumber(8),
    m_MinVolume(100),
    m_MaxVolume(100),
    m_RetuneRank(true),
    m_PipeConfig(NULL, &organModel, NULL) {}

void GORank::LoadMidiObject(
  GOConfigReader &cfg, const wxString &group, GOMidiMap &midiMap) {
  GOMidiSendingObject::LoadMidiObject(cfg, group + wxT("Rank"), midiMap);
}

void GORank::Resize() {
  m_MaxNoteVelocities.resize(m_Pipes.size());
  m_NoteStopVelocities.resize(m_Pipes.size());
  for (unsigned i = 0; i < m_NoteStopVelocities.size(); i++)
    m_NoteStopVelocities[i].resize(m_StopCount);
}

void GORank::Init(
  GOConfigReader &cfg,
  const wxString &group,
  const wxString &name,
  unsigned firstMidiNoteNumber,
  unsigned windchestN) {
  GOMidiSendingObject::Init(cfg, group, name);

  m_FirstMidiNoteNumber = firstMidiNoteNumber;
  m_PipeConfig.Init(cfg, group, wxEmptyString);
  m_WindchestN = windchestN;
  m_HarmonicNumber = 8;
  m_MinVolume = 100;
  m_MaxVolume = 100;
  m_RetuneRank = false;

  GOWindchest *windchest = r_OrganModel.GetWindchest(m_WindchestN - 1);

  windchest->AddRank(this);
  m_PipeConfig.SetParent(&windchest->GetPipeConfig());

  m_Pipes.clear();
  m_PipeConfig.SetName(GetName());
  Resize();
}

void GORank::Load(
  GOConfigReader &cfg, const wxString &group, int defaultFirstMidiNoteNumber) {
  GOMidiSendingObject::Load(
    cfg, group, cfg.ReadString(ODFSetting, group, wxT("Name"), true));

  m_FirstMidiNoteNumber = cfg.ReadInteger(
    ODFSetting,
    group,
    wxT("FirstMidiNoteNumber"),
    0,
    256,
    defaultFirstMidiNoteNumber < 0,
    std::max(defaultFirstMidiNoteNumber, 0));

  unsigned number_of_logical_pipes
    = cfg.ReadInteger(ODFSetting, group, wxT("NumberOfLogicalPipes"), 1, 192);
  m_PipeConfig.Load(cfg, group, wxEmptyString);
  m_WindchestN = cfg.ReadInteger(
    ODFSetting,
    group,
    wxT("WindchestGroup"),
    1,
    r_OrganModel.GetWindchestCount());
  m_HarmonicNumber = cfg.ReadInteger(
    ODFSetting, group, wxT("HarmonicNumber"), 1, 1024, false, 8);
  m_MinVolume = cfg.ReadFloat(
    ODFSetting, group, wxT("MinVelocityVolume"), 0, 1000, false, 100);
  m_MaxVolume = cfg.ReadFloat(
    ODFSetting, group, wxT("MaxVelocityVolume"), 0, 1000, false, 100);
  m_RetuneRank
    = cfg.ReadBoolean(ODFSetting, group, wxT("AcceptsRetuning"), false, true);

  GOWindchest *windchest = r_OrganModel.GetWindchest(m_WindchestN - 1);

  windchest->AddRank(this);
  m_PipeConfig.SetParent(&windchest->GetPipeConfig());

  m_Pipes.clear();
  for (unsigned i = 0; i < number_of_logical_pipes; i++) {
    wxString buffer;
    buffer.Printf(wxT("Pipe%03u"), i + 1);
    wxString name = cfg.ReadStringTrim(ODFSetting, group, buffer);
    if (name == wxT("DUMMY")) {
      m_Pipes.push_back(
        new GODummyPipe(&r_OrganModel, this, m_FirstMidiNoteNumber + i));
    } else if (name.StartsWith(wxT("REF:"))) {
      m_Pipes.push_back(
        new GOReferencePipe(&r_OrganModel, this, m_FirstMidiNoteNumber + i));
    } else {
      m_Pipes.push_back(new GOSoundingPipe(
        &r_OrganModel,
        this,
        m_WindchestN,
        m_FirstMidiNoteNumber + i,
        m_HarmonicNumber,
        m_MinVolume,
        m_MaxVolume,
        m_RetuneRank));
    }
    m_Pipes[i]->Load(cfg, group, buffer);
  }
  m_PipeConfig.SetName(GetName());
  Resize();
}

void GORank::SaveMidiObject(
  GOConfigWriter &cfg, const wxString &group, GOMidiMap &midiMap) const {
  GOMidiSendingObject::SaveMidiObject(cfg, group + wxT("Rank"), midiMap);
}

void GORank::AddPipe(GOPipe *pipe) {
  m_Pipes.push_back(pipe);
  Resize();
}

unsigned GORank::RegisterStop(GOStop *stop) {
  unsigned id = m_StopCount++;
  Resize();
  return id;
}

void GORank::SetKey(int note, unsigned velocity, unsigned stopID) {
  if (note >= 0 && note < (int)m_Pipes.size()) {
    auto &allStopVelocities = m_NoteStopVelocities[note];
    unsigned &thisStopVelocity = allStopVelocities[stopID];
    unsigned oldThisStopVelocity = thisStopVelocity;
    unsigned &maxVelocity = m_MaxNoteVelocities[note];

    thisStopVelocity = velocity;
    if (velocity > maxVelocity || velocity < oldThisStopVelocity) {
      // the max velocity of the pipe is changed
      // find the new max velocity
      maxVelocity = velocity >= maxVelocity
        ? velocity
        : *std::max_element(allStopVelocities.begin(), allStopVelocities.end());
      m_Pipes[note]->SetVelocity(maxVelocity);
    }
  }
}

GOPipe *GORank::GetPipe(unsigned index) { return m_Pipes[index]; }

unsigned GORank::GetPipeCount() { return m_Pipes.size(); }

GOPipeConfigNode &GORank::GetPipeConfig() { return m_PipeConfig; }

void GORank::SetTemperament(const GOTemperament &temperament) {
  for (unsigned j = 0; j < m_Pipes.size(); j++)
    m_Pipes[j]->SetTemperament(temperament);
}

void GORank::PreparePlayback() {
  SendEmptyMidiKey();
  for (unsigned i = 0; i < m_MaxNoteVelocities.size(); i++)
    m_MaxNoteVelocities[i] = 0;
  for (unsigned i = 0; i < m_NoteStopVelocities.size(); i++)
    for (unsigned j = 0; j < m_NoteStopVelocities[i].size(); j++)
      m_NoteStopVelocities[i][j] = 0;
  GOMidiSendingObject::PreparePlayback();
}

wxString GORank::GetElementStatus() { return _("-"); }

std::vector<wxString> GORank::GetElementActions() {
  std::vector<wxString> actions;
  return actions;
}

void GORank::TriggerElementActions(unsigned no) {
  // Never called
}
