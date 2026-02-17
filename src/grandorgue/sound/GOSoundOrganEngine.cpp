/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundOrganEngine.h"

#include <algorithm>

#include "config/GOAudioDeviceConfig.h"
#include "config/GOConfig.h"
#include "model/GOOrganModel.h"
#include "model/GOPipe.h"
#include "model/GOWindchest.h"
#include "sound/buffer/GOSoundBufferMutable.h"
#include "sound/scheduler/GOSoundGroupTask.h"
#include "sound/scheduler/GOSoundOutputTask.h"
#include "sound/scheduler/GOSoundReleaseTask.h"
#include "sound/scheduler/GOSoundThread.h"
#include "sound/scheduler/GOSoundTouchTask.h"
#include "sound/scheduler/GOSoundTremulantTask.h"
#include "sound/scheduler/GOSoundWindchestTask.h"

#include "GOEvent.h"
#include "GOSoundProvider.h"
#include "GOSoundRecorder.h"
#include "GOSoundReleaseAlignTable.h"
#include "GOSoundSampler.h"

/*
 * Factories
 */

std::vector<GOSoundOrganEngine::AudioOutputConfig> GOSoundOrganEngine::
  createAudioOutputConfigs(GOConfig &config, unsigned nAudioGroups) {
  std::vector<GOAudioDeviceConfig> &audioDeviceConf
    = config.GetAudioDeviceConfig();
  const unsigned nAudioDevices = audioDeviceConf.size();
  std::vector<AudioOutputConfig> engineConf(nAudioDevices);

  for (unsigned deviceI = 0; deviceI < nAudioDevices; deviceI++) {
    const GOAudioDeviceConfig &deviceConfig = audioDeviceConf[deviceI];
    const auto &deviceOutputs = deviceConfig.GetChannelOututs();
    AudioOutputConfig &engineDeviceConf = engineConf[deviceI];

    engineDeviceConf.channels = deviceConfig.GetChannels();
    engineDeviceConf.scaleGains.resize(engineDeviceConf.channels);
    for (unsigned j = 0; j < engineDeviceConf.channels; j++) {
      std::vector<float> &sf = engineDeviceConf.scaleGains[j];

      sf.resize(nAudioGroups * 2);
      std::fill(sf.begin(), sf.end(), GOAudioDeviceConfig::MUTE_VOLUME);

      if (j >= deviceOutputs.size())
        continue;

      const auto &channelOutputs = deviceOutputs[j];

      for (unsigned k = 0; k < channelOutputs.size(); k++) {
        const auto &groupOutput = channelOutputs[k];
        int id = config.GetStrictAudioGroupId(groupOutput.GetName());

        if (id >= 0) {
          sf[id * 2] = groupOutput.GetLeft();
          sf[id * 2 + 1] = groupOutput.GetRight();
        }
      }
    }
  }
  return engineConf;
}

std::vector<GOSoundOrganEngine::AudioOutputConfig> GOSoundOrganEngine::
  createDefaultOutputConfigs(unsigned nAudioGroups) {
  AudioOutputConfig conf;

  conf.channels = 2;
  conf.scaleGains.resize(2);
  for (unsigned ch = 0; ch < 2; ch++) {
    conf.scaleGains[ch].resize(
      nAudioGroups * 2, GOAudioDeviceConfig::MUTE_VOLUME);
    for (unsigned groupI = 0; groupI < nAudioGroups; groupI++)
      conf.scaleGains[ch][groupI * 2 + ch] = 0.0f;
  }
  return {conf};
}

/*
 * Constructor
 */

GOSoundOrganEngine::GOSoundOrganEngine(
  GOOrganModel &organModel, GOMemoryPool &memoryPool)
  : r_OrganModel(organModel),
    r_MemoryPool(memoryPool),
    mp_TouchTask(std::make_unique<GOSoundTouchTask>(r_MemoryPool)),
    m_LifecycleState(LifecycleState::IDLE),
    m_InterpolationType(GOSoundResample::GO_LINEAR_INTERPOLATION),
    m_IsDownmix(false),
    m_IsPolyphonyLimiting(true),
    m_IsRandomizeSpeaking(true),
    m_IsReleaseAlignmentEnabled(true),
    m_IsScaledReleases(true),
    m_NAudioGroups(1),
    m_NAuxThreads(0),
    m_NReleaseRepeats(1),
    m_PolyphonySoftLimit(0),
    m_ReverbConfig(GOSoundReverb::CONFIG_REVERB_DISABLED),
    m_NSamplesPerBuffer(1),
    m_SampleRate(0),
    m_CurrentTime(1),
    m_UsedPolyphony(0),
    m_MeterInfo(1),
    p_AudioRecorder(nullptr) {
  m_SamplerPool.SetUsageLimit(2048);
  m_PolyphonySoftLimit = (m_SamplerPool.GetUsageLimit() * 3) / 4;
  // mp_ReleaseTask references m_AudioGroupTasks, which is declared after
  // mp_ReleaseTask; initializing it in the initializer list would use
  // m_AudioGroupTasks before it is constructed.
  mp_ReleaseTask
    = std::make_unique<GOSoundReleaseTask>(*this, m_AudioGroupTasks);
  SetVolume(-15);
}

// Cannot be defined in .h: mp_ReleaseTask and mp_TouchTask are unique_ptr to
// forward-declared types; their destructors require complete types only
// available in .cpp.
GOSoundOrganEngine::~GOSoundOrganEngine() {}

/*
 * Configuration setters
 */

void GOSoundOrganEngine::SetVolume(int volume) {
  m_volume = volume;
  m_gain = powf(10.0f, m_volume * 0.05f);
}

void GOSoundOrganEngine::SetHardPolyphony(unsigned polyphony) {
  m_SamplerPool.SetUsageLimit(polyphony);
  m_PolyphonySoftLimit = (m_SamplerPool.GetUsageLimit() * 3) / 4;
}

void GOSoundOrganEngine::SetFromConfig(GOConfig &config) {
  SetHardPolyphony(config.PolyphonyLimit());
  SetInterpolationType(config.m_InterpolationType());
  SetDownmix(config.RecordDownmix());
  SetPolyphonyLimiting(config.ManagePolyphony());
  SetRandomizeSpeaking(config.RandomizeSpeaking());
  SetScaledReleases(config.ScaleRelease());
  SetNAudioGroups(config.GetAudioGroups().size());
  SetNAuxThreads(config.Concurrency());
  SetNReleaseRepeats(config.ReleaseConcurrency());
  SetReverbConfig(GOSoundReverb::createReverbConfig(config));
}

/*
 * Lifecycle setter
 */

void GOSoundOrganEngine::SetUsed(bool isUsed) {
  const auto state = m_LifecycleState.load();

  assert(state >= LifecycleState::WORKING && state <= LifecycleState::USED);
  m_LifecycleState.store(
    isUsed ? LifecycleState::USED : LifecycleState::WORKING);
}

/*
 * Lifecycle functions
 */

void GOSoundOrganEngine::ResetCounters() {
  m_UsedPolyphony.store(0);
  m_SamplerPool.ReturnAll();
  m_CurrentTime = 1;
  m_scheduler.Reset();
}

void GOSoundOrganEngine::BuildThreads(unsigned nThreads) {
  for (unsigned threadI = 0; threadI < nThreads; threadI++)
    mp_threads.push_back(new GOSoundThread(&m_scheduler));
  for (GOSoundThread *pThread : mp_threads)
    pThread->Run();
}

void GOSoundOrganEngine::DestroyThreads() {
  for (GOSoundThread *pThread : mp_threads)
    pThread->Delete();
  mp_threads.resize(0);
}

void GOSoundOrganEngine::BuildTasks(
  const std::vector<AudioOutputConfig> &audioOutputConfigs,
  unsigned nSamplesPerBuffer,
  unsigned sampleRate,
  GOSoundRecorder &recorder) {
  assert(m_LifecycleState.load() == LifecycleState::IDLE);
  assert(!audioOutputConfigs.empty());

  m_NSamplesPerBuffer = nSamplesPerBuffer;
  m_SampleRate = sampleRate;

  // [B1] Audio group mix buffers
  assert(m_AudioGroupTasks.size() == 0);

  // audioGroupOutputs is used in [B2] and [B3] to connect group tasks as
  // inputs to output tasks
  std::vector<GOSoundBufferTaskBase *> audioGroupOutputs;

  for (unsigned groupI = 0; groupI < m_NAudioGroups; groupI++) {
    GOSoundGroupTask *pGroupTask
      = new GOSoundGroupTask(*this, m_NSamplesPerBuffer);

    m_AudioGroupTasks.push_back(pGroupTask);
    audioGroupOutputs.push_back(pGroupTask);
  }

  // [B2] Create output tasks and connect them to audioGroupOutputs ([B1])
  assert(m_AudioOutputTasks.size() == 0);

  unsigned nTotalChannels = 0;

  for (const AudioOutputConfig &deviceConf : audioOutputConfigs) {
    assert(deviceConf.channels > 0);
    assert(deviceConf.scaleGains.size() == deviceConf.channels);

    const unsigned nDeviceChannels = deviceConf.channels;
    // Device outputs: channel-major layout [chI * nAudioGroups * 2 + groupI*2 +
    // lr]. Values are converted from dB (scaleGains) to linear
    // (outputScaleFactors).
    std::vector<float> outputScaleFactors(
      m_NAudioGroups * nDeviceChannels * 2, 0.0f);

    for (unsigned chI = 0; chI < nDeviceChannels; chI++) {
      const std::vector<float> &chScaleGains = deviceConf.scaleGains[chI];
      const unsigned nChScaleGains = chScaleGains.size();

      assert(nChScaleGains == m_NAudioGroups * 2);
      for (unsigned k = 0; k < nChScaleGains; k++) {
        const float dbGain = chScaleGains[k];

        outputScaleFactors[chI * m_NAudioGroups * 2 + k]
          = (dbGain >= -120.0f && dbGain < 40.0f) ? powf(10.0f, dbGain * 0.05f)
                                                  : 0.0f;
      }
    }
    GOSoundOutputTask *pOutputTask = new GOSoundOutputTask(
      nDeviceChannels, outputScaleFactors, m_NSamplesPerBuffer);

    pOutputTask->SetOutputs(audioGroupOutputs);
    m_AudioOutputTasks.push_back(pOutputTask);
    nTotalChannels += nDeviceChannels;
  }

  // [B3] Create mixer output task and connect it to audioGroupOutputs ([B1])
  // (downmix mode only)
  //
  // Internal mix: group-major layout [groupI * 4 + ch * 2 + lr]
  //   group i, ch 0, left  → index i*4   = 1.0f
  //   group i, ch 1, right → index i*4+3 = 1.0f
  assert(!mp_MixerOutputTask);

  if (m_IsDownmix) {
    std::vector<float> mixScaleFactors(m_NAudioGroups * 4, 0.0f);

    for (unsigned groupI = 0; groupI < m_NAudioGroups; groupI++) {
      mixScaleFactors[groupI * 4] = 1.0f;
      mixScaleFactors[groupI * 4 + 3] = 1.0f;
    }
    mp_MixerOutputTask = std::make_unique<GOSoundOutputTask>(
      2, mixScaleFactors, m_NSamplesPerBuffer);
    mp_MixerOutputTask->SetOutputs(audioGroupOutputs);
  }

  // [B4] Set up recorder
  p_AudioRecorder = &recorder;
  {
    std::vector<GOSoundBufferTaskBase *> recorderOutputs;

    if (mp_MixerOutputTask)
      recorderOutputs.push_back(mp_MixerOutputTask.get());
    else
      for (GOSoundOutputTask *pOutputTask : m_AudioOutputTasks)
        recorderOutputs.push_back(pOutputTask);
    p_AudioRecorder->SetOutputs(recorderOutputs, m_NSamplesPerBuffer);
  }

  // [B5] Set up reverb on output tasks
  for (GOSoundOutputTask *pOutputTask : m_AudioOutputTasks)
    pOutputTask->SetupReverb(m_ReverbConfig, m_NSamplesPerBuffer, m_SampleRate);
  if (mp_MixerOutputTask)
    mp_MixerOutputTask->SetupReverb(
      m_ReverbConfig, m_NSamplesPerBuffer, m_SampleRate);

  // [B6] Tremulant tasks
  assert(m_TremulantTasks.size() == 0);

  const unsigned nTremulants = r_OrganModel.GetTremulantCount();

  for (unsigned tremI = 0; tremI < nTremulants; tremI++)
    m_TremulantTasks.push_back(
      new GOSoundTremulantTask(*this, m_NSamplesPerBuffer));

  // [B7] Windchest tasks: slot 0 for detached releases, slots 1..N for
  // windchests
  assert(m_WindchestTasks.size() == 0);
  m_WindchestTasks.push_back(new GOSoundWindchestTask(*this, nullptr));

  const unsigned nWindchests = r_OrganModel.GetWindchestCount();

  for (unsigned windI = 0; windI < nWindchests; windI++)
    m_WindchestTasks.push_back(
      new GOSoundWindchestTask(*this, r_OrganModel.GetWindchest(windI)));

  // [B8] Init windchest tasks with tremulant tasks
  for (GOSoundWindchestTask *pWindchestTask : m_WindchestTasks)
    pWindchestTask->Init(m_TremulantTasks);

  // [B9] Add all tasks to scheduler
  m_scheduler.SetRepeatCount(m_NReleaseRepeats);
  for (GOSoundTremulantTask *pTremulantTask : m_TremulantTasks)
    m_scheduler.Add(pTremulantTask);
  for (GOSoundWindchestTask *pWindchestTask : m_WindchestTasks)
    m_scheduler.Add(pWindchestTask);
  for (GOSoundGroupTask *pGroupTask : m_AudioGroupTasks)
    m_scheduler.Add(pGroupTask);
  for (GOSoundOutputTask *pOutputTask : m_AudioOutputTasks)
    m_scheduler.Add(pOutputTask);
  if (mp_MixerOutputTask)
    m_scheduler.Add(mp_MixerOutputTask.get());
  m_scheduler.Add(p_AudioRecorder);
  m_scheduler.Add(mp_ReleaseTask.get());
  m_scheduler.Add(mp_TouchTask.get());

  // [B10] Build threads
  BuildThreads(m_NAuxThreads);

  m_MeterInfo.resize(nTotalChannels + 1);
  m_LifecycleState.store(LifecycleState::BUILT);
}

void GOSoundOrganEngine::DestroyTasks() {
  assert(m_LifecycleState.load() == LifecycleState::BUILT);

  // [B9] Clear scheduler (tasks are removed but not yet destroyed)
  m_scheduler.Clear();

  // [B8] skipped: windchest-tremulant connections are non-owning.

  // [B7] Destroy windchest tasks (sampler queues already cleared in StopEngine)
  m_WindchestTasks.clear();

  // [B6] Destroy tremulant tasks
  m_TremulantTasks.clear();

  // [B5] skipped: reverb state is owned by GOSoundOutputTask, freed below.

  // [B4] Disconnect recorder
  p_AudioRecorder = nullptr;

  // [B3] Destroy mixer output task
  mp_MixerOutputTask.reset();

  // [B2] Destroy device output tasks
  m_AudioOutputTasks.clear();

  // [B1] Destroy audio group tasks
  m_AudioGroupTasks.clear();

  m_MeterInfo.resize(1);

  // Resume scheduler (now empty) so threads can receive the exit signal;
  // GOSoundThread::Delete() alone cannot unblock a thread waiting for work.
  // [B9]
  m_scheduler.ResumeGivingWork();

  // [B10] Destroy threads
  DestroyThreads();

  m_LifecycleState.store(LifecycleState::IDLE);
}

void GOSoundOrganEngine::StartEngine() {
  assert(m_LifecycleState.load() == LifecycleState::BUILT);
  ResetCounters();
  m_scheduler.ResumeGivingWork();
  m_LifecycleState.store(LifecycleState::WORKING);
}

void GOSoundOrganEngine::StopEngine() {
  assert(m_LifecycleState.load() == LifecycleState::WORKING);
  m_scheduler.PauseGivingWork();
  WaitForThreadsIdle();
  // Clear all active samplers from group task queues to stop sound output
  for (GOSoundGroupTask *pGroupTask : m_AudioGroupTasks)
    pGroupTask->WaitAndClear();
  m_LifecycleState.store(LifecycleState::BUILT);
}

void GOSoundOrganEngine::BuildAndStart(
  const std::vector<AudioOutputConfig> &audioOutputConfigs,
  unsigned nSamplesPerBuffer,
  unsigned sampleRate,
  GOSoundRecorder &recorder) {
  BuildTasks(audioOutputConfigs, nSamplesPerBuffer, sampleRate, recorder);
  StartEngine();
}

void GOSoundOrganEngine::StopAndDestroy() {
  StopEngine();
  DestroyTasks();
}

/*
 * Other functions
 */

const std::vector<double> &GOSoundOrganEngine::GetMeterInfo() {
  m_MeterInfo[0] = m_UsedPolyphony.load() / (double)GetHardPolyphony();
  m_UsedPolyphony.store(0);

  for (unsigned i = 1; i < m_MeterInfo.size(); i++)
    m_MeterInfo[i] = 0;

  unsigned nr = 1;

  for (GOSoundOutputTask *pOutputTask : m_AudioOutputTasks) {
    const std::vector<float> &info = pOutputTask->GetMeterInfo();
    const unsigned nInfo = info.size();

    for (unsigned infoI = 0; infoI < nInfo; infoI++)
      m_MeterInfo[nr++] = info[infoI];
    pOutputTask->ResetMeterInfo();
  }
  return m_MeterInfo;
}

void GOSoundOrganEngine::WakeupThreads() {
  for (GOSoundThread *pThread : mp_threads)
    pThread->Wakeup();
}

void GOSoundOrganEngine::WaitForThreadsIdle() {
  for (GOSoundThread *pThread : mp_threads)
    pThread->WaitForIdle();
}

void GOSoundOrganEngine::GetAudioOutput(
  unsigned outputIndex, bool isLast, GOSoundBufferMutable &outBuffer) {
  if (IsWorking()) {
    GOSoundOutputTask &outTask = *m_AudioOutputTasks[outputIndex];

    outTask.Finish(isLast);
    outBuffer.CopyFrom(outTask);
  } else
    outBuffer.FillWithSilence();
}

void GOSoundOrganEngine::NextPeriod() {
  m_scheduler.Exec();

  m_CurrentTime += m_NSamplesPerBuffer;
  unsigned used_samplers = m_SamplerPool.UsedSamplerCount();
  if (used_samplers > m_UsedPolyphony.load())
    m_UsedPolyphony.store(used_samplers);

  m_scheduler.Reset();
}

float GOSoundOrganEngine::GetRandomFactor() {
  if (m_IsRandomizeSpeaking) {
    const double factor = (pow(2, 1.0 / 1200.0) - 1) / (RAND_MAX / 2);
    int num = rand() - RAND_MAX / 2;
    return 1 + num * factor;
  }
  return 1;
}

void GOSoundOrganEngine::PassSampler(GOSoundSampler *sampler) {
  int taskId = sampler->m_SamplerTaskId;

  if (isWindchestTask(taskId))
    m_AudioGroupTasks[sampler->m_AudioGroupId]->Add(sampler);
  else
    m_TremulantTasks[tremulantTaskToIndex(taskId)]->Add(sampler);
}

void GOSoundOrganEngine::StartSampler(GOSoundSampler *sampler) {
  int taskId = sampler->m_SamplerTaskId;

  sampler->stop = 0;
  sampler->new_attack = 0;
  sampler->p_WindchestTask = isWindchestTask(taskId)
    ? m_WindchestTasks[windchestTaskToIndex(taskId)]
    : nullptr;
  PassSampler(sampler);
}

unsigned GOSoundOrganEngine::SamplesDiffToMs(
  uint64_t fromSamples, uint64_t toSamples) const {
  return (unsigned)std::min(
    (toSamples - fromSamples) * 1000 / m_SampleRate, (uint64_t)UINT_MAX);
}

GOSoundSampler *GOSoundOrganEngine::CreateTaskSample(
  const GOSoundProvider *pSoundProvider,
  int samplerTaskId,
  unsigned audioGroup,
  unsigned velocity,
  unsigned delay,
  uint64_t prevEventTime,
  bool isRelease,
  uint64_t *pStartTimeSamples) {
  unsigned delay_samples = (delay * m_SampleRate) / (1000);
  uint64_t start_time = m_CurrentTime + delay_samples;
  unsigned eventIntervalMs = SamplesDiffToMs(prevEventTime, start_time);

  GOSoundSampler *sampler = nullptr;
  const GOSoundAudioSection *section = isRelease
    ? pSoundProvider->GetRelease(BOOL3_DEFAULT, eventIntervalMs)
    : pSoundProvider->GetAttack(velocity, eventIntervalMs);

  if (pStartTimeSamples) {
    *pStartTimeSamples = start_time;
  }
  if (section && section->GetChannels()) {
    sampler = m_SamplerPool.GetSampler();
    if (sampler) {
      sampler->p_SoundProvider = pSoundProvider;
      sampler->m_WaveTremulantStateFor = section->GetWaveTremulantStateFor();
      sampler->velocity = velocity;
      sampler->stream.InitStream(
        &m_resample,
        section,
        m_InterpolationType,
        GetRandomFactor() * pSoundProvider->GetTuning() / (float)m_SampleRate);

      const float playback_gain
        = pSoundProvider->GetGain() * section->GetNormGain();

      sampler->fader.Setup(
        playback_gain, pSoundProvider->GetVelocityVolume(velocity));
      sampler->delay = delay_samples;
      sampler->time = start_time;
      sampler->toneBalanceFilterState.Init(
        sampler->p_SoundProvider->GetToneBalance()->GetFilter());
      sampler->is_release = isRelease;
      sampler->m_SamplerTaskId = samplerTaskId;
      sampler->m_AudioGroupId = audioGroup;
      StartSampler(sampler);
    }
  }
  return sampler;
}

void GOSoundOrganEngine::SwitchToAnotherAttack(GOSoundSampler *pSampler) {
  const GOSoundProvider *pProvider = pSampler->p_SoundProvider;

  if (pProvider && !pSampler->is_release) {
    const GOSoundAudioSection *section
      = pProvider->GetAttack(pSampler->velocity, 1000);

    if (section) {
      GOSoundSampler *new_sampler = m_SamplerPool.GetSampler();

      if (new_sampler != NULL) {
        float gain_target = pProvider->GetGain() * section->GetNormGain();
        unsigned crossFadeSamples
          = MsToSamples(pProvider->GetAttackSwitchCrossfadeLength());

        // copy old sampler to the new one
        *new_sampler = *pSampler;

        // start decay in the new sampler
        new_sampler->is_release = true;
        new_sampler->time = m_CurrentTime;
        new_sampler->fader.StartDecreasingVolume(crossFadeSamples);

        // start new section stream in the old sampler
        pSampler->m_WaveTremulantStateFor = section->GetWaveTremulantStateFor();
        pSampler->stream.InitAlignedStream(
          section, m_InterpolationType, &new_sampler->stream);
        pSampler->p_SoundProvider = pProvider;
        pSampler->time = m_CurrentTime + 1;

        pSampler->fader.Setup(
          gain_target,
          new_sampler->fader.GetVelocityVolume(),
          crossFadeSamples);
        pSampler->is_release = false;

        new_sampler->toneBalanceFilterState.Init(
          new_sampler->p_SoundProvider->GetToneBalance()->GetFilter());

        StartSampler(new_sampler);
      }
    }
  }
}

void GOSoundOrganEngine::CreateReleaseSampler(GOSoundSampler *handle) {
  if (!handle->p_SoundProvider)
    return;

  /* The beloow code creates a new sampler to playback the release, the
   * following code takes the active sampler for this pipe (which will be
   * in either the attack or loop section) and sets the fadeout property
   * which will decay this portion of the pipe. The sampler will
   * automatically be placed back in the pool when the fade restores to
   * zero. */
  const GOSoundProvider *this_pipe = handle->p_SoundProvider;
  const GOSoundAudioSection *release_section = this_pipe->GetRelease(
    handle->m_WaveTremulantStateFor,
    SamplesDiffToMs(handle->time, m_CurrentTime));
  unsigned crossFadeSamples = MsToSamples(
    release_section ? release_section->GetReleaseCrossfadeLength()
                    : this_pipe->GetAttackSwitchCrossfadeLength());

  handle->fader.StartDecreasingVolume(crossFadeSamples);
  handle->is_release = true;

  int taskId = handle->m_SamplerTaskId;
  float vol = isWindchestTask(taskId)
    ? m_WindchestTasks[windchestTaskToIndex(taskId)]->GetWindchestVolume()
    : 1.0f;

  // FIXME: this is wrong... the intention is to not create a release for a
  // sample being played back with zero amplitude but this is a comparison
  // against a double. We should test against a minimum level.
  if (vol && release_section) {
    GOSoundSampler *new_sampler = m_SamplerPool.GetSampler();
    if (new_sampler != NULL) {
      new_sampler->p_SoundProvider = this_pipe;
      new_sampler->time = m_CurrentTime + 1;
      new_sampler->m_WaveTremulantStateFor
        = release_section->GetWaveTremulantStateFor();

      unsigned gain_decay_length = 0;
      float gain_target = this_pipe->GetGain() * release_section->GetNormGain();
      const bool not_a_tremulant = isWindchestTask(handle->m_SamplerTaskId);

      if (not_a_tremulant) {
        /* Because this sampler is about to be moved to a detached
         * windchest, we must apply the gain of the existing windchest
         * to the gain target for this fader - otherwise the playback
         * volume on the detached chest will not match the volume on
         * the existing chest. */
        gain_target *= vol;
        if (m_IsScaledReleases) {
          /* Note: "time" is in milliseconds. */
          int time = ((m_CurrentTime - handle->time) * 1000) / m_SampleRate;
          /* TODO: below code should be replaced by a more accurate model of the
           * attack to get a better estimate of the amplitude when playing very
           * short notes estimating attack duration from pipe midi pitch */
          unsigned midikey_frequency = this_pipe->GetMidiKeyNumber();
          /* if MidiKeyNumber is not within an organ 64 feet to 1 foot pipes, we
           * assume average pipe (MIDI = 60)*/
          if (midikey_frequency > 133 || midikey_frequency == 0)
            midikey_frequency = 60;
          /* attack duration is assumed 50 ms above MIDI 96, 800 ms below MIDI
           * 24 and linear in between */
          float attack_duration = 50.0f;
          if (midikey_frequency < 96) {
            if (midikey_frequency < 24)
              attack_duration = 500.0f;
            else
              attack_duration
                = 500.0f + ((24.0f - (float)midikey_frequency) * 6.25f);
          }
          /* calculate gain (gain_target) to apply to tail amplitude in function
           * of when the note is released during the attack */
          if (time < (int)attack_duration) {
            float attack_index = (float)time / attack_duration;
            float gain_delta
              = (0.2f + (0.8f * (2.0f * attack_index - (attack_index * attack_index))));
            gain_target *= gain_delta;
          }
          /* calculate the volume decay to be applied to the release to take
           * into account the fact reverb is not completely formed during
           * staccato time to full reverb is estimated in function of release
           * length for an organ with a release length of 5 seconds or more,
           * time_to_full_reverb is around 350 ms for an organ with a release
           * length of 1 second or less, time_to_full_reverb is around 100 ms
           * time_to_full_reverb is linear in between */
          int time_to_full_reverb = ((60 * release_section->GetLength())
                                     / release_section->GetSampleRate())
            + 40;
          if (time_to_full_reverb > 350)
            time_to_full_reverb = 350;
          if (time_to_full_reverb < 100)
            time_to_full_reverb = 100;
          if (time < time_to_full_reverb) {
            /* in function of note duration, fading happens between:
             * 200 ms and 6 s for release with little reverberation e.g. short
             * release
             * 700 ms and 6 s for release with large reverberation e.g. long
             * release */
            gain_decay_length
              = time_to_full_reverb + 6000 * time / time_to_full_reverb;
          }
        }
      }

      const unsigned releaseLength = this_pipe->GetReleaseTail();

      new_sampler->fader.Setup(
        gain_target, handle->fader.GetVelocityVolume(), crossFadeSamples);

      if (
        releaseLength > 0
        && (releaseLength < gain_decay_length || gain_decay_length == 0))
        gain_decay_length = releaseLength;

      if (gain_decay_length > 0)
        new_sampler->fader.StartDecreasingVolume(
          MsToSamples(gain_decay_length));

      if (
        m_IsReleaseAlignmentEnabled
        && release_section->SupportsStreamAlignment()) {
        new_sampler->stream.InitAlignedStream(
          release_section, m_InterpolationType, &handle->stream);
      } else {
        new_sampler->stream.InitStream(
          &m_resample,
          release_section,
          m_InterpolationType,
          this_pipe->GetTuning() / (float)m_SampleRate);
      }
      new_sampler->is_release = true;

      new_sampler->m_SamplerTaskId = not_a_tremulant
        ? /* detached releases are enabled and the pipe was on a regular
           * windchest. Play the release on the detached windchest */
        DETACHED_RELEASE_TASK_ID
        /* detached releases are disabled (or this isn't really a pipe)
         * so put the release on the same windchest as the pipe (which
         * means it will still be affected by tremulants - yuck). */
        : handle->m_SamplerTaskId;
      new_sampler->m_AudioGroupId = handle->m_AudioGroupId;
      new_sampler->toneBalanceFilterState.Init(
        new_sampler->p_SoundProvider->GetToneBalance()->GetFilter());
      StartSampler(new_sampler);
      handle->time = m_CurrentTime;
    }
  }
}

bool GOSoundOrganEngine::ProcessSampler(
  float *output_buffer,
  GOSoundSampler *sampler,
  unsigned n_frames,
  float volume) {
  float temp[n_frames * 2];
  const bool process_sampler = (sampler->time <= m_CurrentTime);

  if (process_sampler) {
    if (sampler->is_release &&
        ((m_IsPolyphonyLimiting &&
          m_SamplerPool.UsedSamplerCount() >= m_PolyphonySoftLimit &&
          m_CurrentTime - sampler->time > 172 * 16) ||
         sampler->drop_counter > 1))
      sampler->fader.StartDecreasingVolume(MsToSamples(370));

    /* The decoded sampler frame will contain values containing
     * sampler->pipe_section->sample_bits worth of significant bits.
     * It is the responsibility of the fade engine to bring these bits
     * back into a sensible state. This is achieved during setup of the
     * fade parameters. The gain target should be:
     *
     *     playback gain * (2 ^ -sampler->pipe_section->sample_bits)
     */
    if (!sampler->stream.ReadBlock(temp, n_frames))
      sampler->p_SoundProvider = NULL;

    sampler->fader.Process(n_frames, temp, volume);
    if (sampler->toneBalanceFilterState.IsToApply())
      sampler->toneBalanceFilterState.ProcessBuffer(n_frames, temp);

    /* Add these samples to the current output buffer shifting
     * right by the necessary amount to bring the sample gain back
     * to unity (this value is computed in GOPipe.cpp)
     */
    for (unsigned i = 0; i < n_frames * 2; i++)
      output_buffer[i] += temp[i];

    if (
      (sampler->stop && sampler->stop <= m_CurrentTime)
      || (sampler->new_attack && sampler->new_attack <= m_CurrentTime)) {
      mp_ReleaseTask->Add(sampler);
      return false;
    }
  }

  if (
    !sampler->p_SoundProvider
    || (sampler->fader.IsSilent() && process_sampler)) {
    ReturnSampler(sampler);
    return false;
  } else
    return true;
}

void GOSoundOrganEngine::ProcessRelease(GOSoundSampler *sampler) {
  if (sampler->stop) {
    CreateReleaseSampler(sampler);
    sampler->stop = 0;
  } else if (sampler->new_attack) {
    SwitchToAnotherAttack(sampler);
    sampler->new_attack = 0;
  }
  PassSampler(sampler);
}

void GOSoundOrganEngine::ReturnSampler(GOSoundSampler *sampler) {
  m_SamplerPool.ReturnSampler(sampler);
}

uint64_t GOSoundOrganEngine::StopSample(
  const GOSoundProvider *pipe, GOSoundSampler *handle) {
  assert(handle);
  assert(pipe);

  // The following condition could arise if a one-shot sample is played,
  // decays away (and hence the sampler is discarded back into the pool), and
  // then the user releases a key. If the sampler had already been reused
  // with another pipe, that sample would erroneously be told to decay.
  if (pipe != handle->p_SoundProvider)
    return 0;

  handle->stop = m_CurrentTime + handle->delay;
  return handle->stop;
}

void GOSoundOrganEngine::SwitchSample(
  const GOSoundProvider *pipe, GOSoundSampler *handle) {
  assert(handle);
  assert(pipe);

  // The following condition could arise if a one-shot sample is played,
  // decays away (and hence the sampler is discarded back into the pool), and
  // then the user releases a key. If the sampler had already been reused
  // with another pipe, that sample would erroneously be told to decay.
  if (pipe != handle->p_SoundProvider)
    return;

  handle->new_attack = m_CurrentTime + handle->delay;
}

void GOSoundOrganEngine::UpdateVelocity(
  const GOSoundProvider *pipe, GOSoundSampler *handle, unsigned velocity) {
  assert(handle);
  assert(pipe);

  if (handle->p_SoundProvider == pipe) {
    // we've just checked that handle is still playing the same pipe
    // may be handle was switched to another pipe between checking and
    // SetVelocityVolume but we don't want to lock it because this functionality
    // is not so important Concurrent update possible, as it just update a float
    handle->velocity = velocity;
    handle->fader.SetVelocityVolume(pipe->GetVelocityVolume(velocity));
  }
}
