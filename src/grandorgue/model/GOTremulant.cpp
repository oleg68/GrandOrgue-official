/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTremulant.h"

#include <cassert>
#include <cmath>
#include <numbers>
#include <wx/intl.h>

#include "config/GOConfigReader.h"
#include "config/GOConfigWriter.h"
#include "model/GOOrganModel.h"
#include "sound/effects/GOSoundVibratoPitchRateCurve.h"
#include "sound/providers/GOSoundTremulantProviderSynth.h"

static const GOConfigEnum TREMULANT_TYPES({
  {wxT("Synth"), GOSynthTrem},
  {wxT("Wave"), GOWavTrem},
});

static const wxString WX_PERIOD_MS = wxT("PeriodMs");
static const wxString WX_START_TIME_MS = wxT("StartTimeMs");
static const wxString WX_STOP_TIME_MS = wxT("StopTimeMs");
static const wxString WX_AMP_MOD_DEPTH_PCNT = wxT("AmpModDepthPcnt");
static const wxString WX_PITCH_MOD_DEPTH_CENTS = wxT("PitchModDepthCents");
static const wxString WX_PITCH_MOD_PHASE_DEGREES = wxT("PitchModPhaseDegrees");

// The parameters of a tremulant that is not defined in the ODF
static constexpr int PERIOD_MS_DEFAULT = 200;
static constexpr int START_TIME_MS_DEFAULT = 300;
static constexpr int STOP_TIME_MS_DEFAULT = 300;
static constexpr int AMP_MOD_DEPTH_PCNT_DEFAULT = 50;
static constexpr float PITCH_MOD_DEPTH_CENTS_DEFAULT = 0.0f;
static constexpr int PITCH_MOD_PHASE_DEGREES_DEFAULT = 0;

/**
 * Converts a time to a number of frames, rounded to the nearest. Unlike
 * GOSoundVibratoProcessor.cpp (rounds up) and GOSoundSamplerPlayer::MsToSamples
 * (truncates) this is deliberate: the loop of a tremulant should be as close
 * as possible to the requested period. It takes a double because the period
 * can be fractional (the natural period of an LFO) and a float loses frames
 * at 441 s.
 */
static unsigned ms_to_frames(double ms, unsigned sampleRate) {
  return (unsigned)std::llround(ms * sampleRate / 1000.0);
}

GOTremulant::GOTremulant(GOOrganModel &organModel)
  : GODrawstop(organModel, OBJECT_TYPE_TREMULANT),
    m_IsOdfDefined(false),
    m_TremulantType(GOSynthTrem),
    m_PeriodMs(0),
    m_StartTimeMs(0),
    m_StopTimeMs(0),
    m_AmpModDepthPcnt(0),
    m_PitchModDepthCents(0.0f),
    m_PitchModPhaseDegrees(0) {
  r_OrganModel.RegisterCacheObject(this);
}

// Defined here because GOSoundTremulantProvider is incomplete in the header
GOTremulant::~GOTremulant() { r_OrganModel.UnRegisterCacheObject(this); }

void GOTremulant::Initialize() {}

/*
 * Nothing to load for Synth: the wave form is rendered by the sound engine
 * because it depends on the sample rate. Lfo (PR 12) loads and caches its wav
 * here.
 */
void GOTremulant::LoadData(const GOFileStore &fileStore, GOMemoryPool &pool) {}

bool GOTremulant::LoadCache(GOMemoryPool &pool, GOCache &cache) { return true; }

void GOTremulant::LoadFromCmb(GOConfigReader &cfg) {
  const wxString &group = GODrawstop::m_group;

  m_PeriodMs = cfg.ReadInteger(
    CMBSetting, group, WX_PERIOD_MS, 32, 441000, false, PERIOD_MS_DEFAULT);
  m_StartTimeMs = cfg.ReadInteger(
    CMBSetting,
    group,
    WX_START_TIME_MS,
    0,
    10000,
    false,
    START_TIME_MS_DEFAULT);
  m_StopTimeMs = cfg.ReadInteger(
    CMBSetting, group, WX_STOP_TIME_MS, 0, 10000, false, STOP_TIME_MS_DEFAULT);
  m_AmpModDepthPcnt = cfg.ReadInteger(
    CMBSetting,
    group,
    WX_AMP_MOD_DEPTH_PCNT,
    0,
    100,
    false,
    AMP_MOD_DEPTH_PCNT_DEFAULT);
  m_PitchModDepthCents = cfg.ReadFloat(
    CMBSetting,
    group,
    WX_PITCH_MOD_DEPTH_CENTS,
    0.0,
    GOSoundVibratoPitchRateCurve::MAX_PITCH_DEVIATION_CENTS,
    false,
    PITCH_MOD_DEPTH_CENTS_DEFAULT);
  m_PitchModPhaseDegrees = cfg.ReadInteger(
    CMBSetting,
    group,
    WX_PITCH_MOD_PHASE_DEGREES,
    -180,
    359,
    false,
    PITCH_MOD_PHASE_DEGREES_DEFAULT);
}

void GOTremulant::Init(
  GOConfigReader &cfg, const wxString &group, const wxString &name) {
  m_IsOdfDefined = false;
  GODrawstop::Init(cfg, group, name);
  m_TremulantType = GOSynthTrem;
  o_provider = std::make_unique<GOSoundTremulantProviderSynth>();
  LoadFromCmb(cfg);
}

void GOTremulant::Load(GOConfigReader &cfg, const wxString &group) {
  m_IsOdfDefined = true;
  SetGroupAndPrefix(group, wxEmptyString);
  m_TremulantType = (GOTremulantType)cfg.ReadEnum(
    ODFSetting,
    group,
    wxT("TremulantType"),
    TREMULANT_TYPES,
    false,
    GOSynthTrem);
  if (m_TremulantType == GOSynthTrem) {
    const int startRate
      = cfg.ReadInteger(ODFSetting, group, wxT("StartRate"), 1, 100);
    const int stopRate
      = cfg.ReadInteger(ODFSetting, group, wxT("StopRate"), 1, 100);

    o_provider = std::make_unique<GOSoundTremulantProviderSynth>();
    m_PeriodMs = cfg.ReadLong(ODFSetting, group, wxT("Period"), 32, 441000);
    m_StartTimeMs = (int)std::lround(1000.0 / startRate);
    m_StopTimeMs = (int)std::lround(1000.0 / stopRate);
    m_AmpModDepthPcnt
      = cfg.ReadInteger(ODFSetting, group, wxT("AmpModDepth"), 1, 100);
  }
  GODrawstop::Load(cfg, group);
}

void GOTremulant::SetupIsToStoreInCmb() {
  GODrawstop::SetupIsToStoreInCmb();
  m_IsToStoreInDivisional
    = m_IsToStoreInDivisional && r_OrganModel.DivisionalsStoreTremulants();
}

void GOTremulant::OnDrawstopStateChanged(bool on) {
  if (m_TremulantType == GOWavTrem)
    r_OrganModel.UpdateTremulant(this);
}

void GOTremulant::StartPlayback() {
  GODrawstop::StartPlayback();

  if (m_TremulantType == GOWavTrem)
    r_OrganModel.UpdateTremulant(this);
}

GOTremulantType GOTremulant::GetTremulantType() const {
  return m_TremulantType;
}

void GOTremulant::SetOrganModified() {
  r_OrganModel.SetOrganModelModified(true);
}

void GOTremulant::Save(GOConfigWriter &cfg) {
  GODrawstop::Save(cfg);
  // An ODF tremulant takes its parameters only from the ODF
  if (!m_IsOdfDefined) {
    const wxString &group = GODrawstop::m_group;

    cfg.WriteInteger(group, WX_PERIOD_MS, m_PeriodMs);
    cfg.WriteInteger(group, WX_START_TIME_MS, m_StartTimeMs);
    cfg.WriteInteger(group, WX_STOP_TIME_MS, m_StopTimeMs);
    cfg.WriteInteger(group, WX_AMP_MOD_DEPTH_PCNT, m_AmpModDepthPcnt);
    cfg.WriteFloat(group, WX_PITCH_MOD_DEPTH_CENTS, m_PitchModDepthCents);
    cfg.WriteInteger(group, WX_PITCH_MOD_PHASE_DEGREES, m_PitchModPhaseDegrees);
  }
}

void GOTremulant::Render(unsigned sampleRate, Description &outDesc) const {
  assert(o_provider);

  // The phase may be any integer (the ODF allows -180 .. 359): reduce it to
  // 0 .. 359 degrees; the second modulo makes a negative remainder positive
  const unsigned phaseDegrees = ((m_PitchModPhaseDegrees % 360) + 360) % 360;
  // The provider takes radians
  const float phaseRads = (float)(phaseDegrees * std::numbers::pi / 180.0);
  const GOSoundTremulantProvider::Scales scales{
    m_AmpModDepthPcnt / 100.0f, m_PitchModDepthCents, phaseRads};
  const double periodMs
    = m_PeriodMs ? m_PeriodMs : o_provider->GetNaturalPeriodMs();
  const unsigned nLoopFrames
    = ms_to_frames(o_provider->GetNPeriodsInLoop() * periodMs, sampleRate);

  assert(nLoopFrames > 0);
  o_provider->RenderLoop(scales, nLoopFrames, outDesc.amp, outDesc.pitchCents);
  outDesc.nStartFrames = ms_to_frames(m_StartTimeMs, sampleRate);
  outDesc.nStopFrames = ms_to_frames(m_StopTimeMs, sampleRate);
}
