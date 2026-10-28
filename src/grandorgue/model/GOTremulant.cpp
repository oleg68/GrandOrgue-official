/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTremulant.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <wx/intl.h>

#include "config/GOConfigReader.h"
#include "config/GOConfigWriter.h"
#include "model/GOOrganModel.h"
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

/* The deepest pitch modulation: at 2400 cents k = 1 - 2^-2 = 0.75, so the
 * lowest frequency deviation -0.75 is
 * GOSoundVibratoPitchRateCurve::MIN_DEVIATION (checked by GOTestTremulant:
 * exp2() is not constexpr, so no static_assert) */
static constexpr float MAX_PITCH_MOD_DEPTH_CENTS = 2400.0f;

const GOParamRange<int> GOTremulant::PERIOD_MS_RANGE{32, 441000};
const GOParamRange<int> GOTremulant::START_TIME_MS_RANGE{0, 10000};
const GOParamRange<int> GOTremulant::STOP_TIME_MS_RANGE{0, 10000};
const GOParamRange<int> GOTremulant::AMP_MOD_DEPTH_PCNT_RANGE{0, 100};
const GOParamRange<float> GOTremulant::PITCH_MOD_DEPTH_CENTS_RANGE{
  0, MAX_PITCH_MOD_DEPTH_CENTS};
const GOParamRange<int> GOTremulant::PITCH_MOD_PHASE_DEGREES_RANGE{-180, 359};

/**
 * Assigns a new value to a parameter if it is present, clamped to the range.
 * @param newValue the new value, if the parameter is to be changed
 * @param range the allowed range of the parameter
 * @param ioField the field of the parameter
 * @return whether the value has changed
 */
template <typename T>
static bool assign_param(
  const std::optional<T> &newValue, const GOParamRange<T> &range, T &ioField) {
  bool isChanged = false;

  if (newValue) {
    const T clamped = range.Clamp(*newValue);

    isChanged = clamped != ioField;
    ioField = clamped;
  }
  return isChanged;
}

/**
 * Applies the depth to the normalised shape, in place: multiplies every item
 * by it. A zero depth means no modulation: the shape is emptied.
 */
static void apply_depth(std::vector<float> &shape, float depth) {
  if (depth == 0.0f)
    shape.clear();
  else
    std::transform(
      shape.begin(), shape.end(), shape.begin(), [depth](float value) {
        return value * depth;
      });
}

/**
 * Advances the shape cyclically: every item becomes the item that is
 * nShiftFrames ahead, that is the shape is shifted to the left. A later
 * feature of the shape thus comes nShiftFrames frames earlier: the shape
 * leads.
 * @param nShiftFrames less than the length of the shape (any if it is empty)
 */
static void advance_shape(std::vector<float> &shape, unsigned nShiftFrames) {
  if (nShiftFrames > 0 && !shape.empty()) {
    assert(nShiftFrames < shape.size());
    std::rotate(shape.begin(), shape.begin() + nShiftFrames, shape.end());
  }
}

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

// The amplitude modulation depth of an ODF tremulant that does not give it
static constexpr int ODF_AMP_MOD_DEPTH_PCNT_DEFAULT = 0;

// A value outside every range that marks a key as absent in the ODF
static constexpr int ODF_VALUE_ABSENT = -1;

/**
 * Reads the duration of a ramp, given either as a rate or as a time. Exactly
 * one of the two keys must be present.
 * @param cfg the ODF reader
 * @param group the section of the tremulant
 * @param rateKey the key of the rate (1 - 100), the duration is 1000 / rate ms
 *   rounded to the nearest integer
 * @param timeKey the key of the time in ms
 * @param timeRange the allowed range of the time
 * @return the duration in integer ms
 */
static int read_ramp_time_ms(
  GOConfigReader &cfg,
  const wxString &group,
  const wxString &rateKey,
  const wxString &timeKey,
  const GOParamRange<int> &timeRange) {
  const int rate = cfg.ReadInteger(
    ODFSetting, group, rateKey, 1, 100, false, ODF_VALUE_ABSENT);
  const int timeMs = cfg.ReadInteger(
    ODFSetting,
    group,
    timeKey,
    timeRange.m_min,
    timeRange.m_max,
    false,
    ODF_VALUE_ABSENT);
  const bool hasRate = rate != ODF_VALUE_ABSENT;
  const bool hasTime = timeMs != ODF_VALUE_ABSENT;

  if (hasRate == hasTime)
    throw wxString::Format(
      _("Exactly one of '%s' and '%s' must be given at section '%s'"),
      rateKey,
      timeKey,
      group);
  return hasRate ? (int)std::lround(1000.0 / rate) : timeMs;
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
    CMBSetting,
    group,
    WX_PERIOD_MS,
    PERIOD_MS_RANGE.m_min,
    PERIOD_MS_RANGE.m_max,
    false,
    PERIOD_MS_DEFAULT);
  m_StartTimeMs = cfg.ReadInteger(
    CMBSetting,
    group,
    WX_START_TIME_MS,
    START_TIME_MS_RANGE.m_min,
    START_TIME_MS_RANGE.m_max,
    false,
    START_TIME_MS_DEFAULT);
  m_StopTimeMs = cfg.ReadInteger(
    CMBSetting,
    group,
    WX_STOP_TIME_MS,
    STOP_TIME_MS_RANGE.m_min,
    STOP_TIME_MS_RANGE.m_max,
    false,
    STOP_TIME_MS_DEFAULT);
  m_AmpModDepthPcnt = cfg.ReadInteger(
    CMBSetting,
    group,
    WX_AMP_MOD_DEPTH_PCNT,
    AMP_MOD_DEPTH_PCNT_RANGE.m_min,
    AMP_MOD_DEPTH_PCNT_RANGE.m_max,
    false,
    AMP_MOD_DEPTH_PCNT_DEFAULT);
  m_PitchModDepthCents = cfg.ReadFloat(
    CMBSetting,
    group,
    WX_PITCH_MOD_DEPTH_CENTS,
    PITCH_MOD_DEPTH_CENTS_RANGE.m_min,
    PITCH_MOD_DEPTH_CENTS_RANGE.m_max,
    false,
    PITCH_MOD_DEPTH_CENTS_DEFAULT);
  m_PitchModPhaseDegrees = cfg.ReadInteger(
    CMBSetting,
    group,
    WX_PITCH_MOD_PHASE_DEGREES,
    PITCH_MOD_PHASE_DEGREES_RANGE.m_min,
    PITCH_MOD_PHASE_DEGREES_RANGE.m_max,
    false,
    PITCH_MOD_PHASE_DEGREES_DEFAULT);
}

bool GOTremulant::ApplySynthParamChanges(const SynthParamChanges &changes) {
  assert(m_TremulantType == GOSynthTrem);

  bool isChanged = assign_param(changes.periodMs, PERIOD_MS_RANGE, m_PeriodMs);

  isChanged
    = assign_param(
        changes.ampModDepthPcnt, AMP_MOD_DEPTH_PCNT_RANGE, m_AmpModDepthPcnt)
    || isChanged;
  isChanged = assign_param(
                changes.pitchModDepthCents,
                PITCH_MOD_DEPTH_CENTS_RANGE,
                m_PitchModDepthCents)
    || isChanged;
  isChanged = assign_param(
                changes.pitchModPhaseDegrees,
                PITCH_MOD_PHASE_DEGREES_RANGE,
                m_PitchModPhaseDegrees)
    || isChanged;
  isChanged
    = assign_param(changes.startTimeMs, START_TIME_MS_RANGE, m_StartTimeMs)
    || isChanged;
  isChanged = assign_param(changes.stopTimeMs, STOP_TIME_MS_RANGE, m_StopTimeMs)
    || isChanged;
  if (isChanged)
    SetOrganModified();
  return isChanged;
}

bool GOTremulant::ResetSynthParamsToDefault() {
  return ApplySynthParamChanges(
    {.periodMs = PERIOD_MS_DEFAULT,
     .ampModDepthPcnt = AMP_MOD_DEPTH_PCNT_DEFAULT,
     .pitchModDepthCents = PITCH_MOD_DEPTH_CENTS_DEFAULT,
     .pitchModPhaseDegrees = PITCH_MOD_PHASE_DEGREES_DEFAULT,
     .startTimeMs = START_TIME_MS_DEFAULT,
     .stopTimeMs = STOP_TIME_MS_DEFAULT});
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
    o_provider = std::make_unique<GOSoundTremulantProviderSynth>();
    m_PeriodMs = cfg.ReadLong(
      ODFSetting,
      group,
      wxT("Period"),
      PERIOD_MS_RANGE.m_min,
      PERIOD_MS_RANGE.m_max);
    m_StartTimeMs = read_ramp_time_ms(
      cfg, group, wxT("StartRate"), WX_START_TIME_MS, START_TIME_MS_RANGE);
    m_StopTimeMs = read_ramp_time_ms(
      cfg, group, wxT("StopRate"), WX_STOP_TIME_MS, STOP_TIME_MS_RANGE);
    m_AmpModDepthPcnt = cfg.ReadInteger(
      ODFSetting,
      group,
      wxT("AmpModDepth"),
      AMP_MOD_DEPTH_PCNT_RANGE.m_min,
      AMP_MOD_DEPTH_PCNT_RANGE.m_max,
      false,
      ODF_AMP_MOD_DEPTH_PCNT_DEFAULT);
    m_PitchModDepthCents = cfg.ReadFloat(
      ODFSetting,
      group,
      WX_PITCH_MOD_DEPTH_CENTS,
      PITCH_MOD_DEPTH_CENTS_RANGE.m_min,
      PITCH_MOD_DEPTH_CENTS_RANGE.m_max,
      false,
      PITCH_MOD_DEPTH_CENTS_DEFAULT);
    m_PitchModPhaseDegrees = cfg.ReadInteger(
      ODFSetting,
      group,
      WX_PITCH_MOD_PHASE_DEGREES,
      PITCH_MOD_PHASE_DEGREES_RANGE.m_min,
      PITCH_MOD_PHASE_DEGREES_RANGE.m_max,
      false,
      PITCH_MOD_PHASE_DEGREES_DEFAULT);
  }
  GODrawstop::Load(cfg, group);
}

void GOTremulant::SetupIsToStoreInCmb() {
  GODrawstop::SetupIsToStoreInCmb();
  m_IsToStoreInDivisional
    = m_IsToStoreInDivisional && r_OrganModel.DivisionalsStoreTremulants();
}

void GOTremulant::Render(unsigned sampleRate, Description &outDesc) const {
  // A Wave tremulant has no provider and is not rendered
  assert(m_TremulantType != GOWavTrem);
  assert(o_provider);

  const unsigned nPeriodFrames = ms_to_frames(m_PeriodMs, sampleRate);
  const unsigned nStartFrames = ms_to_frames(m_StartTimeMs, sampleRate);
  const unsigned nStopFrames = ms_to_frames(m_StopTimeMs, sampleRate);

  assert(nPeriodFrames > 0);
  outDesc.nPeriodFrames = nPeriodFrames;
  outDesc.nStartFrames = nStartFrames;
  outDesc.nStopFrames = nStopFrames;
  o_provider->RenderOnePeriod(
    nPeriodFrames, outDesc.ampDeviation, outDesc.pitchDeviation);
  // The provider must honour its contract: every shape is empty or one period
  assert(
    outDesc.ampDeviation.empty()
    || outDesc.ampDeviation.size() == nPeriodFrames);
  assert(
    outDesc.pitchDeviation.empty()
    || outDesc.pitchDeviation.size() == nPeriodFrames);

  /* How the normalised shapes are modified: the depths as multipliers and the
   * pitch phase. TODO calculate for lfo; until then an Lfo tremulant gets 0,
   * that is no modulation */
  const bool isSynth = m_TremulantType == GOSynthTrem;
  const float ampFactor = isSynth ? m_AmpModDepthPcnt / 100.0f : 0.0f;
  /* Linear in frequency: the pitch goes down by exactly m_PitchModDepthCents.
   * A zero depth is set to an exact 0, whatever the rounding of exp2() */
  const float pitchFactor = isSynth && m_PitchModDepthCents != 0.0f
    ? 1.0f - std::exp2(-m_PitchModDepthCents / 1200.0f)
    : 0.0f;
  /* The code below turns a negative phase into its 0 .. 359 equivalent by
   * adding 360, so it relies on -180 .. 359 rather than on the parameter range
   */
  assert(m_PitchModPhaseDegrees >= -180 && m_PitchModPhaseDegrees <= 359);
  /* The phase may be any integer in -180 .. 359: a negative value becomes its
   * 0 .. 359 equivalent by adding 360 */
  const unsigned phaseDegrees = isSynth
    ? (
      unsigned)(m_PitchModPhaseDegrees < 0 ? m_PitchModPhaseDegrees + 360 : m_PitchModPhaseDegrees)
    : 0;
  /* The phase as a shift in frames: a positive phase makes the pitch lead.
   * It is rounded to the nearest frame, which is at most half a frame
   * (180 degrees / nPeriodFrames) off; the modulo keeps it below the period */
  const unsigned nPitchShiftFrames
    = (unsigned)(((uint64_t)phaseDegrees * nPeriodFrames + 180) / 360)
    % nPeriodFrames;

  // A zero depth empties the shape: no modulation
  apply_depth(outDesc.ampDeviation, ampFactor);
  apply_depth(outDesc.pitchDeviation, pitchFactor);
  advance_shape(outDesc.pitchDeviation, nPitchShiftFrames);
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
