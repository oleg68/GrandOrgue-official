/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOCONFIGMIDIOBJECT_H
#define GOCONFIGMIDIOBJECT_H

#include "midi/events/GOMidiReceiverType.h"
#include "midi/events/GOMidiSenderType.h"
#include "midi/events/GOMidiShortcutReceiverType.h"
#include "midi/objects/GOMidiObject.h"

/** It is a dummy MIDI object that is used only for storing MIDI configuration
 */

class GOConfigMidiObject : public GOMidiObject {
public:
  static constexpr int ELEMENT_TYPE_NONE = -1;

private:
  wxString m_path;
  GOMidiSender *mp_MidiSender;
  GOMidiReceiver *mp_MidiReceiver;
  GOMidiShortcutReceiver *mp_ShortcutReceiver;
  GOMidiSender *mp_DivisionSender;

  template <typename MidiElementType>
  void ClearMidiElement(MidiElementType *pMidiElement);

  template <typename MidiElementType>
  int GetElementType(const MidiElementType *pEl) const;

  template <typename MidiElementType, typename MidiElementTypeEnum>
  void SetElementType(
    int newElementType,
    MidiElementType *&slot,
    void (GOMidiObject::*setPointerFun)(MidiElementType *pNewElement));

public:
  GOConfigMidiObject(GOMidiMap &midiMap, ObjectType objectType);

  ~GOConfigMidiObject();

  int GetSenderType() const { return GetElementType(mp_MidiSender); }
  void SetSenderType(int senderType);

  int GetReceiverType() const { return GetElementType(mp_MidiReceiver); }
  void SetReceiverType(int receiverType);

  int GetShortcutReceiverType() const {
    return GetElementType(mp_ShortcutReceiver);
  }

  void SetShortcutReceiverType(int shortcutReceiverType);

  int GetDivisionSenderType() const {
    return GetElementType(mp_DivisionSender);
  }

  // Only MIDI_SEND_MANUAL and ELEMENT_TYPE_NONE are allowed
  void SetDivisionSenderType(int senderType);

  virtual void LoadMidiObject(
    GOConfigReader &cfg, const wxString &group, GOMidiMap &midiMap) override;

  virtual void SaveMidiObject(
    GOConfigWriter &cfg,
    const wxString &group,
    GOMidiMap &midiMap) const override;
};

#endif /* GOCONFIGMIDIOBJECT_H */
