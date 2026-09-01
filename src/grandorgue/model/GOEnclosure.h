/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOENCLOSURE_H_
#define GOENCLOSURE_H_

#include <algorithm>
#include <cstdint>

#include <wx/string.h>

#include "control/GOControl.h"
#include "midi/objects/GOMidiObjectWithShortcut.h"

class GOConfigReader;
class GOConfigWriter;
class GOOrganModel;

class GOEnclosure : public GOControl, public GOMidiObjectWithShortcut {
public:
  static const wxString WX_MIDI_TYPE_CODE;
  static const wxString WX_MIDI_TYPE_NAME;

private:
  bool m_IsOdfDefined;
  uint8_t m_DefaultAmpMinimumLevel;
  bool m_Displayed1;
  bool m_Displayed2;

  uint8_t m_AmpMinimumLevel;
  uint8_t m_MIDIValue;

  // The ODF-authored (or, for internal enclosures, always-0) shelf
  // parameters. Serve two purposes, same as m_DefaultAmpMinimumLevel: (1)
  // the fallback LoadFromCmb() substitutes when a .cmb has no saved
  // override for the current-value fields below, since LoadFromCmb() runs
  // unconditionally for both ODF-defined and internal enclosures; (2) the
  // value GOOrganSettingsEnclosuresTab::ResetToDefault() restores the
  // current fields to.
  float m_DefaultLowShelfFrequency;
  float m_DefaultLowShelfAttenuationDb;
  float m_DefaultHighShelfFrequency;
  float m_DefaultHighShelfAttenuationDb;

  // The current, effective shelf parameters: loaded from .cmb (falling back
  // to m_Default* above), editable via Organ Settings only for internal
  // enclosures (IsOdfDefined() == false). Frequency is in Hz; attenuation
  // is a non-negative dB cut (0..121), distinct from m_AmpMinimumLevel's
  // unitless linear fraction - GetCurrentLowShelfGainDb()/
  // GetCurrentHighShelfGainDb() convert it to the signed gain
  // GOSoundShelfFilterProcessor::SetLowShelf()/SetHighShelf() expects.
  float m_LowShelfFrequency;
  float m_LowShelfAttenuationDb;
  float m_HighShelfFrequency;
  float m_HighShelfAttenuationDb;

  /** Linearly interpolates a quantity by this enclosure's current MIDI
   * position: atClosed when m_MIDIValue == 0, atOpen when m_MIDIValue ==
   * 127, linear in between. Shared by GetAttenuation() and the shelf-EQ
   * effective gain getters - they differ only in their two endpoint
   * values. */
  float InterpolateByMidiValue(float atClosed, float atOpen) const {
    return atClosed + (atOpen - atClosed) * m_MIDIValue / 127.0f;
  }

  void OnMidiReceived(
    const GOMidiEvent &event,
    GOMidiMatchType matchType,
    int key,
    int value) override;
  void OnShortcutKeyReceived(
    GOMidiShortcutReceiver::MatchType matchType, int key) override;

  // Load all customizable values from the .cmb file
  void LoadFromCmb(GOConfigReader &cfg, uint8_t defaultValue);
  void Save(GOConfigWriter &cfg) override;

  void SendCurrentMidiValue() override { SendMidiValue(m_MIDIValue); }
  void SendEmptyMidiValue() override { SendMidiValue(0); }

  void SetIntEnclosureValue(int n) { SetEnclosureValue(std::clamp(n, 0, 127)); }

public:
  static constexpr uint8_t MAX_MIDI_VALUE = 127;

  GOEnclosure(GOOrganModel &organModel);

  bool IsOdfDefined() const { return m_IsOdfDefined; }

  using GOMidiObjectWithShortcut::Init; // for avoiding a warning
  void Init(
    GOConfigReader &cfg,
    const wxString &group,
    const wxString &name,
    uint8_t defValue);
  using GOMidiObjectWithShortcut::Load; // for avoiding a warning
  void Load(GOConfigReader &cfg, const wxString &group);

  uint8_t GetDefaultAmpMinimumLevel() const { return m_DefaultAmpMinimumLevel; }
  uint8_t GetAmpMinimumLevel() const { return m_AmpMinimumLevel; }
  void SetAmpMinimumLevel(uint8_t v) { m_AmpMinimumLevel = v; }
  void SetEnclosureValue(uint8_t n);
  int GetEnclosureValue() const { return m_MIDIValue; }
  float GetAttenuation() {
    return InterpolateByMidiValue(m_AmpMinimumLevel / 100.0f, 1.0f);
  }

  float GetDefaultLowShelfFrequency() const {
    return m_DefaultLowShelfFrequency;
  }
  float GetDefaultLowShelfAttenuationDb() const {
    return m_DefaultLowShelfAttenuationDb;
  }
  float GetDefaultHighShelfFrequency() const {
    return m_DefaultHighShelfFrequency;
  }
  float GetDefaultHighShelfAttenuationDb() const {
    return m_DefaultHighShelfAttenuationDb;
  }

  float GetLowShelfFrequency() const { return m_LowShelfFrequency; }
  void SetLowShelfFrequency(float v) { m_LowShelfFrequency = v; }
  float GetLowShelfAttenuationDb() const { return m_LowShelfAttenuationDb; }
  void SetLowShelfAttenuationDb(float v) { m_LowShelfAttenuationDb = v; }
  float GetHighShelfFrequency() const { return m_HighShelfFrequency; }
  void SetHighShelfFrequency(float v) { m_HighShelfFrequency = v; }
  float GetHighShelfAttenuationDb() const { return m_HighShelfAttenuationDb; }
  void SetHighShelfAttenuationDb(float v) { m_HighShelfAttenuationDb = v; }

  /** @return the low shelf's current effective gain in dB, ready to pass
   * straight into GOSoundShelfFilterProcessor::SetLowShelf(): 0 (no-op) at
   * fully open, -m_LowShelfAttenuationDb at fully closed, linear in
   * between. */
  float GetCurrentLowShelfGainDb() const {
    return InterpolateByMidiValue(-m_LowShelfAttenuationDb, 0.0f);
  }
  /** Same contract as GetCurrentLowShelfGainDb(), for the high band. */
  float GetCurrentHighShelfGainDb() const {
    return InterpolateByMidiValue(-m_HighShelfAttenuationDb, 0.0f);
  }

  bool IsKeyboardInputUsed() const override {
    return GOMidiObjectWithShortcut::IsKeyboardInputUsed();
  }

  void Scroll(bool scroll_up);
  bool IsDisplayed(bool new_format);

  wxString GetElementStatus() override;
  std::vector<wxString> GetElementActions() override;
  void TriggerElementActions(unsigned no) override;
};

#endif /* GOENCLOSURE_H_ */
