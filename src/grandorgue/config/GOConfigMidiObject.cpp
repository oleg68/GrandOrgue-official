/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOConfigMidiObject.h"

GOConfigMidiObject::GOConfigMidiObject(
  GOMidiMap &midiMap,
  const wxString &midiTypeCode,
  const wxString &midiTypeName)
  : GOMidiObject(midiMap, midiTypeCode, midiTypeName),
    mp_MidiSender(nullptr),
    mp_MidiReceiver(nullptr),
    mp_ShortcutReceiver(nullptr),
    mp_DivisionSender(nullptr) {}