/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTREMULANT_H
#define GOTREMULANT_H

#include <memory>
#include <vector>
#include <wx/string.h>

#include "GOCacheObject.h"
#include "GODrawstop.h"

class GOConfigReader;
class GOConfigWriter;
class GOMemoryPool;
class GOSoundTremulantProvider;

typedef enum { GOSynthTrem, GOWavTrem } GOTremulantType;

class GOTremulant : public GODrawstop, private GOCacheObject {
public:
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
  float m_StartTimeMs;
  float m_StopTimeMs;
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

  /**
   * Initialises an internal (not defined in the ODF) tremulant: a Synth one
   * with the default parameters, which the .cmb may override.
   */
  void Init(
    GOConfigReader &cfg, const wxString &group, const wxString &name) override;

  void Load(GOConfigReader &cfg, const wxString &group) override;
  GOTremulantType GetTremulantType() const;

  /**
   * Renders the loop and the ramp lengths for a sample rate. Called only from
   * the control thread; reads the parameters that nothing else changes at that
   * time (they are written by Load(), Init() and the setters, which the
   * settings dialog does not call yet).
   * Must not be called for a Wave tremulant.
   *
   * @param sampleRate the sample rate of the sound engine, in Hz
   * @param outDesc the result; overwritten
   */
  void Render(unsigned sampleRate, Description &outDesc) const;

  // The setters below store the value and mark the organ model as modified if
  // it changed. The sound changes when the wave form is rendered again
  // (GOSoundOrganEngine::PrepareTremulantWaveForms)
  void SetPeriodMs(int v) { SetParam(m_PeriodMs, v); }
  void SetStartTimeMs(float v) { SetParam(m_StartTimeMs, v); }
  void SetStopTimeMs(float v) { SetParam(m_StopTimeMs, v); }
  void SetAmpModDepthPcnt(int v) { SetParam(m_AmpModDepthPcnt, v); }
  void SetPitchModDepthCents(float v) { SetParam(m_PitchModDepthCents, v); }
  void SetPitchModPhaseDegrees(int v) { SetParam(m_PitchModPhaseDegrees, v); }
};

#endif /* GOTREMULANT_H_ */
