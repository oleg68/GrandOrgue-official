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
#include <utility>
#include <wx/intl.h>

#include "GOHash.h"
#include "config/GOConfigReader.h"
#include "config/GOConfigWriter.h"
#include "model/GOOrganModel.h"
#include "sound/dsp-kernels/GOSoundConversions.h"
#include "sound/providers/GOSoundTremulantProviderLfo.h"
#include "sound/providers/GOSoundTremulantProviderSynth.h"

static const GOConfigEnum TREMULANT_TYPES({
  {wxT("Synth"), GOTremulant::SYNTH},
  {wxT("Wave"), GOTremulant::WAVE},
  {wxT("Lfo"), GOTremulant::LFO},
});

static const wxString WX_PERIOD_MS = wxT("PeriodMs");
static const wxString WX_START_TIME_MS = wxT("StartTimeMs");
static const wxString WX_STOP_TIME_MS = wxT("StopTimeMs");
static const wxString WX_AMP_MOD_DEPTH_PCNT = wxT("AmpModDepthPcnt");
static const wxString WX_PITCH_MOD_DEPTH_CENTS = wxT("PitchModDepthCents");
static const wxString WX_PITCH_MOD_PHASE_DEGREES = wxT("PitchModPhaseDegrees");

static const wxString WX_LFO_WAVE_FILE = wxT("LfoWaveFile");
static const wxString WX_LFO_PITCH_CHANNEL = wxT("LfoPitchChannel");
static const wxString WX_LFO_AMP_CHANNEL = wxT("LfoAmpChannel");
static const wxString WX_LFO_AMP_DEPTH_ADJUST_DB = wxT("LfoAmpDepthAdjustDb");
static const wxString WX_LFO_PITCH_DEPTH_ADJUST_PCNT
  = wxT("LfoPitchDepthAdjustPcnt");

// The amplitude modulation depth of an ODF tremulant that does not give it
static constexpr int ODF_AMP_MOD_DEPTH_PCNT_DEFAULT = 0;

// A value outside every range that marks a key as absent in the ODF
static constexpr int ODF_VALUE_ABSENT = -1;

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
// 0 means that the file has no such shape
const GOParamRange<int> GOTremulant::LFO_CHANNEL_N_RANGE{0, 2};
const GOParamRange<float> GOTremulant::LFO_AMP_DEPTH_ADJUST_DB_RANGE{
  -60.0f, 20.0f};
const GOParamRange<float> GOTremulant::LFO_PITCH_DEPTH_ADJUST_PCNT_RANGE{
  0.0f, 400.0f};

// The parameters of a tremulant that is not defined in the ODF
static constexpr int PERIOD_MS_DEFAULT = 200;
static constexpr int START_TIME_MS_DEFAULT = 300;
static constexpr int STOP_TIME_MS_DEFAULT = 300;
static constexpr int AMP_MOD_DEPTH_PCNT_DEFAULT = 50;
static constexpr float PITCH_MOD_DEPTH_CENTS_DEFAULT = 0.0f;
static constexpr int PITCH_MOD_PHASE_DEGREES_DEFAULT = 0;

// The defaults of the Lfo keys of an ODF tremulant
static constexpr int LFO_PITCH_CHANNEL_N_DEFAULT = 1;
static constexpr int LFO_AMP_CHANNEL_N_DEFAULT = 2;
static constexpr float LFO_AMP_DEPTH_ADJUST_DB_DEFAULT = 0.0f;
static constexpr float LFO_PITCH_DEPTH_ADJUST_PCNT_DEFAULT = 100.0f;

GOTremulant::GOTremulant(GOOrganModel &organModel)
  : GODrawstop(organModel, OBJECT_TYPE_TREMULANT),
    m_IsOdfDefined(false),
    m_TremulantType(SYNTH),
    m_PeriodMs(0),
    m_StartTimeMs(0),
    m_StopTimeMs(0),
    m_AmpModDepthPcnt(0),
    m_PitchModDepthCents(0.0f),
    m_PitchModPhaseDegrees(0),
    m_LfoAmpDepthAdjustDb(LFO_AMP_DEPTH_ADJUST_DB_DEFAULT),
    m_LfoPitchDepthAdjustPcnt(LFO_PITCH_DEPTH_ADJUST_PCNT_DEFAULT) {
  r_OrganModel.RegisterCacheObject(this);
}

// Defined here because GOSoundTremulantProvider is incomplete in the header
GOTremulant::~GOTremulant() { r_OrganModel.UnRegisterCacheObject(this); }

void GOTremulant::Initialize() {}

void GOTremulant::UpdateHash(GOHash &hash) const {
  hash.Update((int)m_TremulantType);
  /* The compression of an Lfo section follows the global setting, so toggling
   * it must invalidate the organ cache, as for GOSoundingPipe. Synth and Wave
   * have no cached data, so the setting must not reset their cache */
  if (m_TremulantType == LFO)
    hash.Update((int)r_OrganModel.GetConfig().LosslessCompression());
  if (o_provider)
    o_provider->UpdateHash(hash);
}

void GOTremulant::LoadData(const GOFileStore &fileStore, GOMemoryPool &pool) {
  /* Synth needs nothing: the wave form is rendered by the sound engine because
   * it depends on the sample rate; a Wave tremulant has no provider */
  if (m_TremulantType == LFO && o_provider)
    o_provider->LoadData(
      fileStore, pool, r_OrganModel.GetConfig().LosslessCompression());
}

bool GOTremulant::LoadCache(GOMemoryPool &pool, GOCache &cache) {
  return o_provider ? o_provider->LoadCache(pool, cache) : true;
}

bool GOTremulant::SaveCache(GOCacheWriter &cache) const {
  return o_provider ? o_provider->SaveCache(cache) : true;
}

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
 * Applies the depth to the deviation, in place: multiplies every item by it.
 * A zero depth needs no special case: the channel is not requested, so its
 * deviation is empty, and it stays empty.
 */
static void apply_depth(float depth, std::vector<float> &ioDeviations) {
  std::transform(
    ioDeviations.begin(),
    ioDeviations.end(),
    ioDeviations.begin(),
    [depth](float value) { return value * depth; });
}

/**
 * Advances the deviation cyclically: every item becomes the item that is
 * nShiftFrames ahead, that is the deviation is shifted to the left. A later
 * feature of the deviation thus comes nShiftFrames frames earlier: it leads.
 * @param nShiftFrames less than the length of the deviation (any if it is
 *   empty)
 */
static void advance_shape(
  unsigned nShiftFrames, std::vector<float> &ioDeviations) {
  if (nShiftFrames > 0 && !ioDeviations.empty()) {
    assert(nShiftFrames < ioDeviations.size());
    std::rotate(
      ioDeviations.begin(),
      ioDeviations.begin() + nShiftFrames,
      ioDeviations.end());
  }
}

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

bool GOTremulant::ApplySynthParamChanges(const SynthParamChanges &changes) {
  const bool isSynth = m_TremulantType == SYNTH;
  bool isChanged = false;

  assert(isSynth);
  /* With correct use the type is always Synth here: this check never fires, it
   * only keeps the call harmless in a release build, where the assert above is
   * compiled out */
  if (isSynth) {
    isChanged = assign_param(changes.periodMs, PERIOD_MS_RANGE, m_PeriodMs);

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
    isChanged
      = assign_param(changes.stopTimeMs, STOP_TIME_MS_RANGE, m_StopTimeMs)
      || isChanged;
    if (isChanged)
      SetOrganModified();
  }
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
  m_TremulantType = SYNTH;
  o_provider = std::make_unique<GOSoundTremulantProviderSynth>();
  LoadFromCmb(cfg);
}

void GOTremulant::Load(GOConfigReader &cfg, const wxString &group) {
  m_IsOdfDefined = true;
  SetGroupAndPrefix(group, wxEmptyString);
  m_TremulantType = (TremulantType)cfg.ReadEnum(
    ODFSetting, group, wxT("TremulantType"), TREMULANT_TYPES, false, SYNTH);
  if (m_TremulantType != WAVE) {
    // Shared by Synth and Lfo; a Wave tremulant reads none of them
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
  }
  if (m_TremulantType == SYNTH) {
    o_provider = std::make_unique<GOSoundTremulantProviderSynth>();
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
  } else if (m_TremulantType == LFO) {
    // ReadFileName() does not reject an empty value, so it is checked here
    const wxString fileName
      = cfg.ReadFileName(ODFSetting, group, WX_LFO_WAVE_FILE, true);

    if (fileName.IsEmpty())
      throw wxString::Format(
        _("Key '%s' must not be empty at section '%s'"),
        WX_LFO_WAVE_FILE,
        group);

    // The file and the channels go to the provider only
    const unsigned pitchChannelN = (unsigned)cfg.ReadInteger(
      ODFSetting,
      group,
      WX_LFO_PITCH_CHANNEL,
      LFO_CHANNEL_N_RANGE.m_min,
      LFO_CHANNEL_N_RANGE.m_max,
      false,
      LFO_PITCH_CHANNEL_N_DEFAULT);
    const unsigned ampChannelN = (unsigned)cfg.ReadInteger(
      ODFSetting,
      group,
      WX_LFO_AMP_CHANNEL,
      LFO_CHANNEL_N_RANGE.m_min,
      LFO_CHANNEL_N_RANGE.m_max,
      false,
      LFO_AMP_CHANNEL_N_DEFAULT);

    if (!pitchChannelN && !ampChannelN)
      throw wxString::Format(
        _("Section '%s' has neither %s nor %s"),
        group,
        WX_LFO_PITCH_CHANNEL,
        WX_LFO_AMP_CHANNEL);

    m_LfoAmpDepthAdjustDb = (float)cfg.ReadFloat(
      ODFSetting,
      group,
      WX_LFO_AMP_DEPTH_ADJUST_DB,
      LFO_AMP_DEPTH_ADJUST_DB_RANGE.m_min,
      LFO_AMP_DEPTH_ADJUST_DB_RANGE.m_max,
      false,
      LFO_AMP_DEPTH_ADJUST_DB_DEFAULT);
    m_LfoPitchDepthAdjustPcnt = (float)cfg.ReadFloat(
      ODFSetting,
      group,
      WX_LFO_PITCH_DEPTH_ADJUST_PCNT,
      LFO_PITCH_DEPTH_ADJUST_PCNT_RANGE.m_min,
      LFO_PITCH_DEPTH_ADJUST_PCNT_RANGE.m_max,
      false,
      LFO_PITCH_DEPTH_ADJUST_PCNT_DEFAULT);
    o_provider = std::make_unique<GOSoundTremulantProviderLfo>(
      fileName, pitchChannelN, ampChannelN);
  }
  // Wave reads nothing and creates no provider
  GODrawstop::Load(cfg, group);
}

const GOSoundTremulantProviderLfo &GOTremulant::GetLfoProvider() const {
  assert(m_TremulantType == LFO);
  assert(o_provider);
  return static_cast<const GOSoundTremulantProviderLfo &>(*o_provider);
}

const wxString &GOTremulant::GetLfoFileName() const {
  return GetLfoProvider().GetFileName();
}

unsigned GOTremulant::GetLfoPitchChannelN() const {
  return GetLfoProvider().GetPitchChannelN();
}

unsigned GOTremulant::GetLfoAmpChannelN() const {
  return GetLfoProvider().GetAmpChannelN();
}

void GOTremulant::SetupIsToStoreInCmb() {
  GODrawstop::SetupIsToStoreInCmb();
  m_IsToStoreInDivisional
    = m_IsToStoreInDivisional && r_OrganModel.DivisionalsStoreTremulants();
}

GOTremulant::Description GOTremulant::Render(unsigned sampleRate) const {
  // A Wave tremulant has no provider and is not rendered
  assert(m_TremulantType != WAVE);
  assert(o_provider);

  // The loop is rounded to the nearest frame: it should be as close as
  // possible to the requested period
  const unsigned nPeriodFrames
    = GOSoundConversions::msToFramesRounded((unsigned)m_PeriodMs, sampleRate);
  const unsigned nStartFrames = GOSoundConversions::msToFramesRounded(
    (unsigned)m_StartTimeMs, sampleRate);
  const unsigned nStopFrames
    = GOSoundConversions::msToFramesRounded((unsigned)m_StopTimeMs, sampleRate);

  assert(nPeriodFrames > 0);

  /* How the deviations are modified: the depths as multipliers and the pitch
   * phase */
  const bool isSynth = m_TremulantType == SYNTH;
  const bool isLfo = m_TremulantType == LFO;
  /* Synth: percent. Lfo: dB relative to the file's amplitude channel; the
   * ODF range -60..+20 dB keeps the factor above 0 */
  const float ampFactor = isSynth
    ? m_AmpModDepthPcnt / 100.0f
    : (isLfo ? GOSoundConversions::gainToVolume(m_LfoAmpDepthAdjustDb) : 0.0f);
  /* Synth: linear in frequency, the pitch goes down by exactly
   * m_PitchModDepthCents; a zero depth is set to an exact 0, whatever the
   * rounding of centsToRatio(). Lfo: percent of the file's pitch channel */
  const float pitchFactor = isSynth
    ? (
      m_PitchModDepthCents != 0.0f
        ? 1.0f - (float)GOSoundConversions::centsToRatio(-m_PitchModDepthCents)
        : 0.0f)
    : (isLfo ? m_LfoPitchDepthAdjustPcnt / 100.0f : 0.0f);

  assert(ampFactor >= 0.0f);
  assert(pitchFactor >= 0.0f);

  // A zero factor means no modulation: that deviation is not requested
  GOSoundTremulantProvider::Shapes shapes = o_provider->RenderOnePeriod(
    nPeriodFrames, ampFactor > 0.0f, pitchFactor > 0.0f);

  // The provider must honour its contract: every deviation is empty or one
  // period
  assert(
    shapes.ampDeviation.empty() || shapes.ampDeviation.size() == nPeriodFrames);
  assert(
    shapes.pitchDeviation.empty()
    || shapes.pitchDeviation.size() == nPeriodFrames);

  /* A zero depth has not requested the channel, so its deviation is empty and
   * the multiplication leaves it empty */
  apply_depth(ampFactor, shapes.ampDeviation);
  apply_depth(pitchFactor, shapes.pitchDeviation);

  if (isSynth) {
    /* The phase is in -180 .. 359: a negative value becomes its 0 .. 359
     * equivalent by adding 360 */
    assert(m_PitchModPhaseDegrees >= -180 && m_PitchModPhaseDegrees <= 359);

    const int phase = m_PitchModPhaseDegrees;
    const unsigned phaseDegrees = (unsigned)(phase < 0 ? phase + 360 : phase);
    /* The phase as a shift in frames: a positive phase makes the pitch lead.
     * It is rounded to the nearest frame, which is at most half a frame
     * (180 degrees / nPeriodFrames) off; the modulo keeps it below the period
     */
    const unsigned nPitchShiftFrames
      = (unsigned)(((uint64_t)phaseDegrees * nPeriodFrames + 180) / 360)
      % nPeriodFrames;

    advance_shape(nPitchShiftFrames, shapes.pitchDeviation);
  }

  return {
    .nPeriodFrames = nPeriodFrames,
    .nStartFrames = nStartFrames,
    .nStopFrames = nStopFrames,
    .ampDeviation = std::move(shapes.ampDeviation),
    .pitchDeviation = std::move(shapes.pitchDeviation)};
}

void GOTremulant::OnDrawstopStateChanged(bool on) {
  if (m_TremulantType == WAVE)
    r_OrganModel.UpdateTremulant(this);
}

void GOTremulant::StartPlayback() {
  GODrawstop::StartPlayback();

  if (m_TremulantType == WAVE)
    r_OrganModel.UpdateTremulant(this);
}

GOTremulant::TremulantType GOTremulant::GetTremulantType() const {
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
