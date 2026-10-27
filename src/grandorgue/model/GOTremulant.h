/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTREMULANT_H
#define GOTREMULANT_H

#include <memory>
#include <optional>
#include <vector>
#include <wx/string.h>

#include "GOCacheObject.h"
#include "GODrawstop.h"
#include "GOParamRange.h"

class GOConfigReader;
class GOConfigWriter;
class GOMemoryPool;
class GOSoundTremulantProvider;

typedef enum { GOSynthTrem, GOWavTrem } GOTremulantType;

class GOTremulant : public GODrawstop, private GOCacheObject {
public:
  // The allowed ranges of the parameters of a Synth tremulant, both ends
  // included. Defined in the .cpp so that this header does not include the
  // sound engine headers that the upper bound of the pitch depth comes from
  static const GOParamRange<int> PERIOD_MS_RANGE;
  static const GOParamRange<int> START_TIME_MS_RANGE;
  static const GOParamRange<int> STOP_TIME_MS_RANGE;
  static const GOParamRange<int> AMP_MOD_DEPTH_PCNT_RANGE;
  static const GOParamRange<float> PITCH_MOD_DEPTH_CENTS_RANGE;
  static const GOParamRange<int> PITCH_MOD_PHASE_DEGREES_RANGE;

  /**
   * The wave form of a tremulant as one loop, in frames of a given sample rate.
   * Built by Render() from the parameters; read by GOSoundTremulantTask.
   */
  struct Description {
    // the amplitude multiplier per frame, 1.0 = no modulation
    std::vector<float> amp;
    // the pitch deviation per frame, in cents; empty if there is no pitch
    // modulation
    std::vector<float> pitchCents;
    // the length of the depth ramp on engaging, in frames
    unsigned nStartFrames;
    // the length of the depth ramp on disengaging, in frames
    unsigned nStopFrames;
  };

  /**
   * The changes of the parameters of a Synth tremulant. A value is present
   * only if the parameter is to be changed.
   */
  struct SynthParamChanges {
    std::optional<int> periodMs;
    std::optional<int> ampModDepthPcnt;
    std::optional<float> pitchModDepthCents;
    std::optional<int> pitchModPhaseDegrees;
    std::optional<int> startTimeMs;
    std::optional<int> stopTimeMs;
  };

private:
  /**
   * Whether the tremulant is defined in the ODF. Its parameters then come only
   * from the ODF and are never saved to the .cmb. Otherwise it is an internal
   * tremulant and its parameters come only from the .cmb.
   */
  bool m_IsOdfDefined;

  GOTremulantType m_TremulantType;
  // The period of the modulation, in ms
  int m_PeriodMs;
  // The lengths of the depth ramps on engaging / disengaging, in ms; 0 means an
  // instant ramp
  int m_StartTimeMs;
  int m_StopTimeMs;
  // The amplitude modulation depth, percent
  int m_AmpModDepthPcnt;
  // The peak pitch deviation, in cents, up to
  // GOSoundVibratoPitchRateCurve::MAX_PITCH_DEVIATION_CENTS; 0 means no pitch
  // modulation. A deep or slow modulation is limited by the delay ring of the
  // vibrato processor, so the effective depth may be smaller
  float m_PitchModDepthCents;
  // The pitch phase against the amplitude, in degrees, any integer; positive:
  // the pitch leads
  int m_PitchModPhaseDegrees;
  // Renders the loop of a Synth tremulant; nullptr for a Wave tremulant
  std::unique_ptr<GOSoundTremulantProvider> o_provider;

  // Marks the organ model as modified. Defined in the .cpp so that this header
  // does not include GOOrganModel.h
  void SetOrganModified();

  /**
   * Stores a new value of a parameter and marks the organ model as modified
   * if it differs from the current one. Not used by Load() or LoadFromCmb(),
   * which set the fields directly, so loading never marks the organ as
   * modified.
   */
  template <typename T> void SetParam(T &field, T newValue) {
    if (newValue != field) {
      field = newValue;
      SetOrganModified();
    }
  }

  /**
   * Reads the parameters of an internal tremulant from the .cmb. A missing key
   * gets its default value. Called only from Init().
   */
  void LoadFromCmb(GOConfigReader &cfg);

  void OnDrawstopStateChanged(bool on) override;
  void SetupIsToStoreInCmb() override;

  void Initialize() override;
  void LoadData(const GOFileStore &fileStore, GOMemoryPool &pool) override;
  bool LoadCache(GOMemoryPool &pool, GOCache &cache) override;
  bool SaveCache(GOCacheWriter &cache) const override { return true; }
  void UpdateHash(GOHash &hash) const override {}
  const wxString &GetLoadTitle() const override { return GetName(); };

  void StartPlayback() override;
  void Save(GOConfigWriter &cfg) override;

public:
  GOTremulant(GOOrganModel &organModel);
  ~GOTremulant();

  /** @return whether the tremulant is defined in the ODF */
  bool IsOdfDefined() const { return m_IsOdfDefined; }

  // The setters store the value and mark the organ model as modified if it
  // changed. The sound changes when the wave form is rendered again
  // (GOSoundOrganEngine::PrepareTremulantWaveForms)
  int GetPeriodMs() const { return m_PeriodMs; }
  void SetPeriodMs(int v) { SetParam(m_PeriodMs, v); }
  int GetStartTimeMs() const { return m_StartTimeMs; }
  void SetStartTimeMs(int v) { SetParam(m_StartTimeMs, v); }
  int GetStopTimeMs() const { return m_StopTimeMs; }
  void SetStopTimeMs(int v) { SetParam(m_StopTimeMs, v); }
  int GetAmpModDepthPcnt() const { return m_AmpModDepthPcnt; }
  void SetAmpModDepthPcnt(int v) { SetParam(m_AmpModDepthPcnt, v); }
  float GetPitchModDepthCents() const { return m_PitchModDepthCents; }
  void SetPitchModDepthCents(float v) { SetParam(m_PitchModDepthCents, v); }
  int GetPitchModPhaseDegrees() const { return m_PitchModPhaseDegrees; }
  void SetPitchModPhaseDegrees(int v) { SetParam(m_PitchModPhaseDegrees, v); }

  /**
   * Applies the changes to the parameters of a Synth tremulant and marks the
   * organ model as modified if anything has changed. The ranges are not
   * checked here.
   * @param changes the parameters to change
   * @return whether any parameter has really changed
   */
  bool ApplySynthParamChanges(const SynthParamChanges &changes);

  /**
   * Sets the parameters of a Synth tremulant to their default values and marks
   * the organ model as modified if anything has changed. Whether it is allowed
   * for an ODF-defined tremulant is decided by the caller.
   * @return whether any parameter has really changed
   */
  bool ResetSynthParamsToDefault();

  /**
   * Initialises an internal (not defined in the ODF) tremulant: a Synth one
   * with the default parameters, which the .cmb may override.
   *
   * It is never displayed by the ODF panels: GODrawstop::Init() calls
   * GOButtonControl::Init(), which resets the displayed flag; the GUI creates
   * its control explicitly.
   */
  void Init(
    GOConfigReader &cfg, const wxString &group, const wxString &name) override;

  void Load(GOConfigReader &cfg, const wxString &group) override;
  GOTremulantType GetTremulantType() const;

  /**
   * Renders the loop and the ramp lengths for a sample rate. Called only from
   * the control thread; reads the parameters written by Load(), Init(), the
   * setters, ApplySynthParamChanges() and ResetSynthParamsToDefault(), which
   * also run only in the control thread. The audio thread never reads them.
   * Must not be called for a Wave tremulant.
   *
   * @param sampleRate the sample rate of the sound engine, in Hz
   * @param outDesc the result; overwritten
   */
  void Render(unsigned sampleRate, Description &outDesc) const;
};

#endif /* GOTREMULANT_H_ */
