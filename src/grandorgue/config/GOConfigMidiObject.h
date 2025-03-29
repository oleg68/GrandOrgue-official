/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOCONFIGMIDIOBJECT_H
#define GOCONFIGMIDIOBJECT_H

#include "midi/objects/GOMidiObject.h"

/** It is a dummy MIDI object that is used only for storing MIDI configuration
 */

class GOConfigMidiObject : public GOMidiObject {
private:
  wxString m_path;
  GOMidiSender *mp_MidiSender;
  GOMidiReceiver *mp_MidiReceiver;
  GOMidiShortcutReceiver *mp_ShortcutReceiver;
  GOMidiSender *mp_DivisionSender;

public:
  GOConfigMidiObject(
    GOMidiMap &midiMap,
    const wxString &midiTypeCode,
    const wxString &midiTypeName);
};

#endif /* GOCONFIGMIDIOBJECT_H */
