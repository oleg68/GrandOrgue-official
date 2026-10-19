/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundTremulantProviderSynth.h"

#include <cmath>
#include <numbers>
#include <numeric>
#include <vector>

#include "sound/providers/GOSoundTremulantProviderSynth.h"

#include "GOTestScope.h"

const std::string GOTestSoundTremulantProviderSynth::TEST_NAME
  = "GOTestSoundTremulantProviderSynth";

static constexpr unsigned N_PERIOD_FRAMES = 64;
static constexpr float TOLERANCE = 1e-5f;

static bool are_close(float a, float b) {
  return std::fabs(a - b) <= TOLERANCE * std::max(1.0f, std::fabs(b));
}

/**
 * @return the sine of the frame of a period with one sine period
 */
static float sine_at(unsigned frameI, unsigned nPeriodFrames) {
  return (float)std::sin(2.0 * std::numbers::pi * frameI / nPeriodFrames);
}

void GOTestSoundTremulantProviderSynth::TestPeriodLength() {
  const GOSoundTremulantProviderSynth synth;
  const GOSoundTremulantProvider &provider = synth;

  for (unsigned nPeriodFrames : {1u, 2u, 7u, 64u, 1001u}) {
    std::vector<float> amp;
    std::vector<float> pitch;

    provider.RenderOnePeriod(nPeriodFrames, amp, pitch);
    GOAssert(amp.size() == nPeriodFrames, "wrong amplitude length");
    GOAssert(pitch.size() == nPeriodFrames, "wrong pitch length");
  }
}

void GOTestSoundTremulantProviderSynth::TestAmplitudeShape() {
  const GOSoundTremulantProviderSynth synth;
  const GOSoundTremulantProvider &provider = synth;
  std::vector<float> amp;
  std::vector<float> pitch;

  provider.RenderOnePeriod(N_PERIOD_FRAMES, amp, pitch);
  GOAssert(amp[0] == 0.0f, "the period must start at 0");
  GOAssert(are_close(amp[N_PERIOD_FRAMES / 4], 1.0f), "wrong maximum");
  GOAssert(are_close(amp[3 * N_PERIOD_FRAMES / 4], -1.0f), "wrong minimum");

  const double sum = std::accumulate(amp.begin(), amp.end(), 0.0);

  GOAssert(std::fabs(sum / N_PERIOD_FRAMES) < 1e-5, "the mean must be 0");
  for (unsigned frameI = 0; frameI < N_PERIOD_FRAMES; ++frameI) {
    GOAssert(
      are_close(amp[frameI], sine_at(frameI, N_PERIOD_FRAMES)),
      "the amplitude must be a unit sine");
    GOAssert(
      amp[frameI] == pitch[frameI],
      "the pitch shape must equal the amplitude shape");
  }
}

void GOTestSoundTremulantProviderSynth::TestZeroMean() {
  const GOSoundTremulantProviderSynth synth;
  const GOSoundTremulantProvider &provider = synth;

  for (unsigned nPeriodFrames : {2u, 7u, 64u, 1001u}) {
    std::vector<float> amp;
    std::vector<float> pitch;

    provider.RenderOnePeriod(nPeriodFrames, amp, pitch);
    const double ampSum = std::accumulate(amp.begin(), amp.end(), 0.0);
    const double pitchSum = std::accumulate(pitch.begin(), pitch.end(), 0.0);

    GOAssert(
      std::fabs(ampSum / nPeriodFrames) < 1e-5,
      "the mean amplitude shape must be 0");
    GOAssert(
      std::fabs(pitchSum / nPeriodFrames) < 1e-5,
      "the mean pitch shape must be 0");
  }
}

void GOTestSoundTremulantProviderSynth::TestOutputReuse() {
  const GOSoundTremulantProviderSynth synth;
  const GOSoundTremulantProvider &provider = synth;
  std::vector<float> amp(5000, 7.0f);
  std::vector<float> pitch(5000, 7.0f);

  provider.RenderOnePeriod(N_PERIOD_FRAMES, amp, pitch);
  GOAssert(amp.size() == N_PERIOD_FRAMES, "old longer amplitude must be cut");
  GOAssert(pitch.size() == N_PERIOD_FRAMES, "old longer pitch must be cut");
  GOAssert(amp[0] == 0.0f, "old amplitude values must be overwritten");
  GOAssert(pitch[0] == 0.0f, "old pitch values must be overwritten");
}

void GOTestSoundTremulantProviderSynth::run() {
  GO_RUN_TEST(TestPeriodLength())
  GO_RUN_TEST(TestAmplitudeShape())
  GO_RUN_TEST(TestZeroMean())
  GO_RUN_TEST(TestOutputReuse())
}
