/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDORGANENGINE_H
#define GOSOUNDORGANENGINE_H

#include <atomic>
#include <memory>
#include <vector>

#include "scheduler/GOSoundScheduler.h"
#include "threading/GOMutex.h"

#include "GOSoundOrganInterface.h"
#include "GOSoundResample.h"
#include "GOSoundReverb.h"
#include "GOSoundSampler.h"
#include "GOSoundSamplerPool.h"

class GOConfig;
class GOMemoryPool;
class GOOrganModel;
class GOSoundBufferMutable;
class GOSoundGroupTask;
class GOSoundOutputTask;
class GOSoundProvider;
class GOSoundRecorder;
class GOSoundReleaseTask;
class GOSoundTask;
class GOSoundThread;
class GOSoundTouchTask;
class GOSoundTremulantTask;
class GOSoundWindchestTask;
class GOWindchest;

/**
 * This class represents a sound engine of one loaded organ. It reflects some
 * GrandOrgue-wide objects (AudioGroup) and some objects of it's model
 * (WindChests, Tremulants).
 *
 * Lifecycle (steps 3–6 are repeatable for restart with new parameters):
 *
 *   1. Constructor: GOSoundOrganEngine(organModel, memoryPool)
 *   2. Configuration: SetFromConfig(config) or manual setters
 *   3. BuildAndStart(audioOutputConfigs, nSamplesPerBuffer, sampleRate,
 *      recorder) — creates tasks and starts the engine
 *   4. SetUsed(true) — connect to the audio system
 *      ... GetAudioOutput() is called from the audio thread ...
 *   5. SetUsed(false) — disconnect from the audio system
 *   6. StopAndDestroy() — stops the engine and destroys tasks
 */
class GOSoundOrganEngine : public GOSoundOrganInterface {
public:
  /*
   * Nested type
   */

  /**
   * @brief Configuration of one audio output.
   *
   * scaleGains[ch][groupI*2+ch] = 0.0f — direct output of group groupI
   * to channel ch. Other values = GOAudioDeviceConfig::MUTE_VOLUME (-121.0f).
   * Values are in dB (converted to linear gain in BuildTasks).
   */
  struct AudioOutputConfig {
    unsigned channels;
    std::vector<std::vector<float>> scaleGains;
  };

  /*
   * Factories
   */

  /**
   * @brief Creates output configurations from GOConfig.
   * @param config       Application configuration.
   * @param nAudioGroups Number of audio groups.
   * @return Vector of output configurations for each audio device.
   */
  static std::vector<AudioOutputConfig> createAudioOutputConfigs(
    GOConfig &config, unsigned nAudioGroups);

  /**
   * @brief Creates one stereo output for nAudioGroups groups.
   *
   * For each group i: scaleGains[LEFT][i*2] = 0.0f,
   * scaleGains[RIGHT][i*2+1] = 0.0f.
   * Other elements: scaleGains[LEFT][i*2+1] = MUTE_VOLUME,
   * scaleGains[RIGHT][i*2] = MUTE_VOLUME.
   * @param nAudioGroups Number of audio groups (default 1).
   * @return Vector containing one stereo AudioOutputConfig.
   */
  static std::vector<AudioOutputConfig> createDefaultOutputConfigs(
    unsigned nAudioGroups = 1);

private:
  static constexpr int DETACHED_RELEASE_TASK_ID = 0;

  /*
   * Constructor constants and objects living the whole lifetime of the instance
   */

  GOOrganModel &r_OrganModel;
  GOMemoryPool &r_MemoryPool;
  /** Resampler used by all samplers; not modified after construction. */
  GOSoundResample m_resample;
  /** Owning release task; created in constructor, reused across BuildAndStart
   * cycles. */
  std::unique_ptr<GOSoundReleaseTask> mp_ReleaseTask;
  /** Owning touch task; created in constructor, reused across BuildAndStart
   * cycles. */
  std::unique_ptr<GOSoundTouchTask> mp_TouchTask;

  /*
   * Lifecycle state
   */

  /** Engine lifecycle state. */
  enum class LifecycleState {
    IDLE,    ///< initial state or after StopAndDestroy
    BUILT,   ///< tasks built, engine not started
             ///< (transitional: between BuildTasks and StartEngine,
             ///<  between StopEngine and DestroyTasks)
    WORKING, ///< engine running, not connected to audio system
    USED,    ///< engine running and connected to audio system
  };

  std::atomic<LifecycleState> m_LifecycleState;

  /*
   * Configuration parameters
   * TODO: m_gain/m_volume naming is inconsistent with convention: m_volume
   *       stores dB and m_gain stores a linear coefficient; rename them and
   *       their accessors (GetGain, GetVolume, SetVolume) accordingly.
   */

  float m_gain;
  GOSoundResample::InterpolationType m_InterpolationType;
  bool m_IsDownmix;
  bool m_IsPolyphonyLimiting;
  bool m_IsRandomizeSpeaking;
  bool m_IsReleaseAlignmentEnabled;
  bool m_IsScaledReleases;
  unsigned m_NAudioGroups;
  unsigned m_NAuxThreads;
  unsigned m_NReleaseRepeats;
  unsigned m_PolyphonySoftLimit;
  GOSoundReverb::ReverbConfig m_ReverbConfig;
  int m_volume;

  /*
   * Startup parameters (set in BuildAndStart)
   */

  unsigned m_NSamplesPerBuffer;
  unsigned m_SampleRate;

  /*
   * Playback counters and state
   */

  uint64_t m_CurrentTime;
  GOSoundSamplerPool m_SamplerPool;
  std::atomic_uint m_UsedPolyphony;
  std::vector<double> m_MeterInfo;

  /*
   * Tasks (filled in BuildTasks, cleared in DestroyTasks)
   */

  /** [B1] Audio group mix buffers.
   * Referenced by: m_AudioOutputTasks [B2], mp_MixerOutputTask [B3],
   * mp_ReleaseTask. */
  ptr_vector<GOSoundGroupTask> m_AudioGroupTasks;
  /** [B2] Device output tasks, one per audio device.
   * References: m_AudioGroupTasks [B1]. */
  ptr_vector<GOSoundOutputTask> m_AudioOutputTasks;
  /** [B3] Internal stereo mix task (downmix mode only, otherwise null).
   * References: m_AudioGroupTasks [B1]. Used as recorder input in [B4]. */
  std::unique_ptr<GOSoundOutputTask> mp_MixerOutputTask;
  /** Non-owning pointer to the recorder; set in BuildTasks [B4]. */
  GOSoundRecorder *p_AudioRecorder;
  /** [B7] Tremulant tasks, one per organ tremulant.
   * Referenced by: m_WindchestTasks [B8]. */
  ptr_vector<GOSoundTremulantTask> m_TremulantTasks;
  /** [B8] Windchest tasks, one per organ windchest plus one for detached
   * releases. References: m_TremulantTasks [B7] (via Init [B9]). */
  ptr_vector<GOSoundWindchestTask> m_WindchestTasks;

  /*
   * Scheduler and threads
   */

  /** [B9] Scheduler; all tasks added here, including mp_ReleaseTask and
   * mp_TouchTask from constructor constants. */
  GOSoundScheduler m_scheduler;
  /** [B10] Worker threads; created in BuildThreads, destroyed in
   * DestroyThreads. */
  ptr_vector<GOSoundThread> mp_threads;

  /*
   * Private lifecycle functions
   */

  /** Resets playback counters; called from StartEngine. */
  void ResetCounters();
  /** Creates auxiliary threads [B10]; called from BuildTasks. */
  void BuildThreads(unsigned nThreads);
  /** Destroys auxiliary threads [B10]; called from DestroyTasks. */
  void DestroyThreads();
  /** Builds all tasks and adds them to the scheduler;
   * called from BuildAndStart. */
  void BuildTasks(
    const std::vector<AudioOutputConfig> &audioOutputConfigs,
    unsigned nSamplesPerBuffer,
    unsigned sampleRate,
    GOSoundRecorder &recorder);
  /** Starts the engine; called from BuildAndStart. */
  void StartEngine();
  /** Stops the engine; called from StopAndDestroy. */
  void StopEngine();
  /** Destroys all tasks; called from StopAndDestroy. */
  void DestroyTasks();

  /*
   * Other private functions
   */

  unsigned MsToSamples(unsigned ms) const { return m_SampleRate * ms / 1000; }

  unsigned SamplesDiffToMs(uint64_t fromSamples, uint64_t toSamples) const;

  /* samplerTaskId:
     -1 .. -n Tremulants
     0 (DETACHED_RELEASE_TASK_ID) detached release
     1 .. n Windchests
  */
  inline static unsigned isWindchestTask(int taskId) { return taskId >= 0; }

  inline static unsigned windchestTaskToIndex(int taskId) {
    return (unsigned)taskId;
  }

  inline static unsigned tremulantTaskToIndex(int taskId) {
    return -taskId - 1;
  }

  void StartSampler(GOSoundSampler *sampler);

  GOSoundSampler *CreateTaskSample(
    const GOSoundProvider *soundProvider,
    int samplerTaskId,
    unsigned audioGroup,
    unsigned velocity,
    unsigned delay,
    uint64_t prevEventTime,
    bool isRelease,
    uint64_t *pStartTimeSamples);
  void CreateReleaseSampler(GOSoundSampler *sampler);

  /**
   * Creates a new sampler with decay of current loop.
   * Switch this sampler to the new attack.
   * It is used when a wave tremulant is switched on or off.
   * @param pSampler current playing sampler for switching to a new attack
   */
  void SwitchToAnotherAttack(GOSoundSampler *pSampler);
  float GetRandomFactor();

public:
  /*
   * Constructor / destructor
   */

  GOSoundOrganEngine(GOOrganModel &organModel, GOMemoryPool &memoryPool);
  // Declared explicitly so its definition can be placed in .cpp, where the
  // complete types required by the unique_ptr members are available.
  ~GOSoundOrganEngine();

  /*
   * Configuration parameters — getters and setters
   */

  /** Returns the gain computed from volume. */
  float GetGain() const { return m_gain; }

  unsigned GetHardPolyphony() const { return m_SamplerPool.GetUsageLimit(); }
  void SetHardPolyphony(unsigned polyphony);

  GOSoundResample::InterpolationType GetInterpolationType() const {
    return m_InterpolationType;
  }
  void SetInterpolationType(unsigned type) {
    m_InterpolationType = (GOSoundResample::InterpolationType)type;
  }

  bool IsDownmix() const { return m_IsDownmix; }
  void SetDownmix(bool isDownmix) { m_IsDownmix = isDownmix; }

  unsigned GetNAudioGroups() const { return m_NAudioGroups; }
  void SetNAudioGroups(unsigned nAudioGroups) {
    m_NAudioGroups = nAudioGroups < 1 ? 1 : nAudioGroups;
  }

  unsigned GetNAuxThreads() const { return m_NAuxThreads; }
  void SetNAuxThreads(unsigned nAuxThreads) { m_NAuxThreads = nAuxThreads; }

  unsigned GetNReleaseRepeats() const { return m_NReleaseRepeats; }
  void SetNReleaseRepeats(unsigned nReleaseRepeats) {
    m_NReleaseRepeats = nReleaseRepeats < 1 ? 1 : nReleaseRepeats;
  }

  bool IsPolyphonyLimiting() const { return m_IsPolyphonyLimiting; }
  void SetPolyphonyLimiting(bool isLimiting) {
    m_IsPolyphonyLimiting = isLimiting;
  }

  bool IsRandomizeSpeaking() const { return m_IsRandomizeSpeaking; }
  void SetRandomizeSpeaking(bool isRandomize) {
    m_IsRandomizeSpeaking = isRandomize;
  }

  bool IsReleaseAlignmentEnabled() const { return m_IsReleaseAlignmentEnabled; }
  void SetReleaseAlignmentEnabled(bool isEnabled) {
    m_IsReleaseAlignmentEnabled = isEnabled;
  }

  const GOSoundReverb::ReverbConfig &GetReverbConfig() const {
    return m_ReverbConfig;
  }
  void SetReverbConfig(const GOSoundReverb::ReverbConfig &config) {
    m_ReverbConfig = config;
  }

  bool IsScaledReleases() const { return m_IsScaledReleases; }
  void SetScaledReleases(bool isScaleRelease) {
    m_IsScaledReleases = isScaleRelease;
  }

  int GetVolume() const { return m_volume; }
  /** Sets the volume and updates the derived gain value (m_gain). */
  void SetVolume(int volume);

  /**
   * @brief Reads the parameters above from GOConfig and stores them via
   * setters.
   *
   * Audio startup parameters (audioOutputConfigs, nSamplesPerBuffer,
   * sampleRate) are not part of the config and are passed directly to
   * BuildAndStart.
   * @param config Application configuration.
   */
  void SetFromConfig(GOConfig &config);

  /*
   * Startup parameters — getters (values come through BuildAndStart)
   */

  unsigned GetSampleRate() const override { return m_SampleRate; }
  /** Returns the buffer size in samples (set during BuildAndStart). */
  unsigned GetNSamplesPerBuffer() const { return m_NSamplesPerBuffer; }

  /*
   * State getters
   */

  // In samples
  uint64_t GetTime() const { return m_CurrentTime; }
  const std::vector<double> &GetMeterInfo();

  /*
   * Lifecycle state
   */

  /** Returns true if the engine is in the initial state (before BuildAndStart
   * or after StopAndDestroy). */
  bool IsIdle() const {
    return m_LifecycleState.load() == LifecycleState::IDLE;
  }
  /** Returns true between BuildAndStart() and StopAndDestroy(), including
   * while connected to the audio system (USED state). */
  bool IsWorking() const {
    return m_LifecycleState.load() >= LifecycleState::WORKING;
  }
  /** Returns true if the engine is connected to the audio system (USED
   * state). */
  bool IsUsed() const {
    return m_LifecycleState.load() >= LifecycleState::USED;
  }
  /** Switches between WORKING and USED; called from GOSoundSystem. */
  void SetUsed(bool isUsed);

  /*
   * Public lifecycle functions
   */

  /**
   * @brief Creates tasks and starts the engine.
   *
   * Call after SetFromConfig() or manual setters.
   * After return the engine is ready for GOSoundSystem::ConnectToEngine.
   * @param audioOutputConfigs  Output configuration; must not be empty.
   * @param nSamplesPerBuffer   Buffer size in samples (from audio system).
   * @param sampleRate          Sample rate in Hz (from audio system).
   * @param recorder            Recorder (non-owning).
   */
  void BuildAndStart(
    const std::vector<AudioOutputConfig> &audioOutputConfigs,
    unsigned nSamplesPerBuffer,
    unsigned sampleRate,
    GOSoundRecorder &recorder);

  /**
   * @brief Stops the engine and destroys tasks.
   *
   * Call after GOSoundSystem::DisconnectFromEngine().
   * assert: state must be WORKING (not USED) — engine must be disconnected
   * from the audio system.
   */
  void StopAndDestroy();

  /*
   * Public organ interface functions (from GOSoundOrganInterface)
   */

  GOSoundSampler *StartPipeSample(
    const GOSoundProvider *pipeProvider,
    unsigned windchestN,
    unsigned audioGroup,
    unsigned velocity,
    unsigned delay,
    uint64_t prevEventTime,
    bool isRelease = false,
    uint64_t *pStartTimeSamples = nullptr) override {
    return CreateTaskSample(
      pipeProvider,
      windchestN,
      audioGroup,
      velocity,
      delay,
      prevEventTime,
      isRelease,
      pStartTimeSamples);
  }

  inline GOSoundSampler *StartTremulantSample(
    const GOSoundProvider *tremProvider,
    unsigned tremulantN,
    uint64_t prevEventTime) override {
    return CreateTaskSample(
      tremProvider, -tremulantN, 0, 0x7f, 0, prevEventTime, false, nullptr);
  }

  uint64_t StopSample(
    const GOSoundProvider *pipe, GOSoundSampler *handle) override;
  void SwitchSample(
    const GOSoundProvider *pipe, GOSoundSampler *handle) override;
  void UpdateVelocity(
    const GOSoundProvider *pipe,
    GOSoundSampler *handle,
    unsigned velocity) override;

  /*
   * Other public functions
   */

  void GetAudioOutput(
    unsigned outputIndex, bool isLast, GOSoundBufferMutable &outBuffer);
  void NextPeriod();

  void WakeupThreads();
  void WaitForThreadsIdle();

  bool ProcessSampler(
    float *buffer, GOSoundSampler *sampler, unsigned n_frames, float volume);
  void ProcessRelease(GOSoundSampler *sampler);
  void PassSampler(GOSoundSampler *sampler);
  void ReturnSampler(GOSoundSampler *sampler);
};

#endif /* GOSOUNDORGANENGINE_H */
