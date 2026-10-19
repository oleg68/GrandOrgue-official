/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundTremulantWaveForm.h"

#include <cmath>
#include <numbers>
#include <vector>

#include "model/GOTremulant.h"
#include "sound/buffer/GOSoundTypedBufferLoopBase.h"
#include "sound/dsp-kernels/GOSoundResample.h"
#include "sound/tasks/GOSoundTremulantWaveForm.h"

#include "GOTestScope.h"

const std::string GOTestSoundTremulantWaveForm::TEST_NAME
  = "GOTestSoundTremulantWaveForm";

using LoopPosition = GOSoundTypedBufferLoopBase::LoopPosition;

static constexpr unsigned N_CHUNK = 4;
static constexpr float TOLERANCE = 1e-4f;
static constexpr unsigned UNIT = GOSoundTremulantWaveForm::UNIT_AMP;

static bool are_close(float a, float b) {
  return std::fabs(a - b) <= TOLERANCE;
}

/**
 * Makes a description by hand
 */
static GOTremulant::Description make_desc(
  const std::vector<float> &amp,
  const std::vector<float> &pitchCents,
  unsigned nStartFrames,
  unsigned nStopFrames) {
  GOTremulant::Description desc;

  desc.amp = amp;
  desc.pitchCents = pitchCents;
  desc.nStartFrames = nStartFrames;
  desc.nStopFrames = nStopFrames;
  return desc;
}

/**
 * @return the unit value of the loop at the position
 */
static unsigned unit_at(
  const GOSoundTremulantWaveForm &waveForm, const LoopPosition &pos) {
  return *waveForm.GetAmpUnits().GetChunk(pos);
}

void GOTestSoundTremulantWaveForm::TestLoopLength() {
  for (unsigned nLoopFrames : {1u, 7u, 8u, 1001u}) {
    const GOSoundTremulantWaveForm waveForm(
      make_desc(std::vector<float>(nLoopFrames, 1.0f), {}, 0, 0), N_CHUNK);

    GOAssert(waveForm.GetNLoopFrames() == nLoopFrames, "wrong loop length");
    GOAssert(
      waveForm.GetAmpUnits().GetNChunkFrames() == N_CHUNK,
      "wrong chunk length");
    GOAssert(waveForm.GetAmpUnits().IsValid(), "the loop must be valid");
  }
}

void GOTestSoundTremulantWaveForm::TestAmplitudeConversion() {
  const std::vector<float> amps{1.0f, 0.5f, 1.25f, 0.0f};

  for (float amp : amps) {
    const GOSoundTremulantWaveForm waveForm(
      make_desc(std::vector<float>(8, amp), {}, 0, 0), N_CHUNK);
    LoopPosition pos(8);

    for (unsigned frameI = 0; frameI < 8; ++frameI, pos.Advance(1))
      GOAssert(
        unit_at(waveForm, pos) == (unsigned)std::lround(amp * UNIT),
        "wrong amplitude units");
  }
}

void GOTestSoundTremulantWaveForm::TestAmplitudeClamping() {
  const GOSoundTremulantWaveForm waveForm(
    make_desc({-0.5f, 10.0f, 1.0f, 1.0f}, {}, 0, 0), N_CHUNK);
  LoopPosition pos(4);

  GOAssert(unit_at(waveForm, pos) == 0, "a negative amplitude must clamp to 0");
  pos.Advance(1);
  GOAssert(
    unit_at(waveForm, pos) == UINT16_MAX,
    "a too large amplitude must clamp to UINT16_MAX");
}

void GOTestSoundTremulantWaveForm::TestAmplitudeTail() {
  const GOSoundTremulantWaveForm waveForm(
    make_desc({0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f}, {}, 0, 0), N_CHUNK);
  const GOSoundTypedBufferLoopMonoManaged<uint16_t> &ampUnits
    = waveForm.GetAmpUnits();
  LoopPosition pos(6);

  // The chunk from the last frames continues with the loop start
  pos.Advance(5);

  const uint16_t *pChunk = ampUnits.GetChunk(pos);

  GOAssert(pChunk[0] == std::lround(0.6f * UNIT), "wrong chunk start");
  GOAssert(pChunk[1] == std::lround(0.1f * UNIT), "the tail must wrap");
  GOAssert(pChunk[2] == std::lround(0.2f * UNIT), "the tail must wrap");
  GOAssert(pChunk[3] == std::lround(0.3f * UNIT), "the tail must wrap");

  // A loop shorter than the chunk repeats cyclically
  const GOSoundTremulantWaveForm shortForm(
    make_desc({0.25f, 0.75f}, {}, 0, 0), N_CHUNK);
  LoopPosition shortPos(2);
  const uint16_t *pShort = shortForm.GetAmpUnits().GetChunk(shortPos);

  for (unsigned frameI = 0; frameI < N_CHUNK; ++frameI)
    GOAssert(
      pShort[frameI] == std::lround((frameI % 2 ? 0.75f : 0.25f) * UNIT),
      "a short loop must repeat cyclically");
}

void GOTestSoundTremulantWaveForm::TestRamps() {
  const GOSoundTremulantWaveForm waveForm(
    make_desc(std::vector<float>(8, 1.0f), {}, 40, 28), N_CHUNK);
  const auto &startRamp = waveForm.GetRamp(true);
  const auto &stopRamp = waveForm.GetRamp(false);

  GOAssert(startRamp.IsRising(), "the start ramp must rise");
  GOAssert(startRamp.GetNRampFrames() == 40, "wrong start ramp length");
  GOAssert(startRamp.GetGainAt(0) == 0.0f, "the start ramp must begin at 0");
  GOAssert(startRamp.GetGainAt(40) == 1.0f, "the start ramp must end at 1");
  GOAssert(!stopRamp.IsRising(), "the stop ramp must fall");
  GOAssert(stopRamp.GetNRampFrames() == 28, "wrong stop ramp length");
  GOAssert(stopRamp.GetGainAt(0) == 1.0f, "the stop ramp must begin at 1");
  GOAssert(stopRamp.GetGainAt(28) == 0.0f, "the stop ramp must end at 0");
}

void GOTestSoundTremulantWaveForm::TestZeroRamps() {
  const GOSoundTremulantWaveForm waveForm(
    make_desc(std::vector<float>(8, 1.0f), {}, 0, 0), N_CHUNK);
  const auto &startRamp = waveForm.GetRamp(true);
  const auto &stopRamp = waveForm.GetRamp(false);

  GOAssert(startRamp.GetNRampFrames() == 0, "the start ramp must be instant");
  GOAssert(startRamp.GetGainAt(0) == 1.0f, "an instant start ramp is 1");
  GOAssert(startRamp.IsRising(), "an instant start ramp must keep its sense");
  GOAssert(stopRamp.GetNRampFrames() == 0, "the stop ramp must be instant");
  GOAssert(stopRamp.GetGainAt(0) == 0.0f, "an instant stop ramp is 0");
  GOAssert(!stopRamp.IsRising(), "an instant stop ramp must keep its sense");
}

void GOTestSoundTremulantWaveForm::TestGetAmplitudeAtDepth() {
  for (float amp : {1.2f, 0.8f}) {
    const GOSoundTremulantWaveForm waveForm(
      make_desc(std::vector<float>(8, amp), {}, 0, 0), N_CHUNK);
    const LoopPosition pos(8);
    // the value after the conversion to units
    const float loopValue = (float)std::lround(amp * UNIT) / UNIT;

    GOAssert(
      are_close(waveForm.GetAmplitudeAt(pos, 1.0f), loopValue),
      "depth 1 must give the loop value");
    GOAssert(
      waveForm.GetAmplitudeAt(pos, 0.0f) == 1.0f, "depth 0 must give 1.0");
    GOAssert(
      are_close(
        waveForm.GetAmplitudeAt(pos, 0.5f), 1.0f + 0.5f * (loopValue - 1.0f)),
      "depth 0.5 must give the middle");
  }
}

void GOTestSoundTremulantWaveForm::TestGetAmplitudeAtPositions() {
  const std::vector<float> profile{
    1.0f, 1.1f, 1.2f, 1.1f, 1.0f, 0.9f, 0.8f, 0.9f};
  const GOSoundTremulantWaveForm waveForm(
    make_desc(profile, {}, 0, 0), N_CHUNK);
  LoopPosition pos(8);

  for (unsigned frameI = 0; frameI < 8; ++frameI, pos.Advance(1))
    GOAssert(
      are_close(waveForm.GetAmplitudeAt(pos, 1.0f), profile[frameI]),
      "wrong amplitude at a position");
}

void GOTestSoundTremulantWaveForm::TestPitch() {
  constexpr unsigned N_LOOP = 64;
  std::vector<float> pitchCents(N_LOOP);

  for (unsigned frameI = 0; frameI < N_LOOP; ++frameI)
    pitchCents[frameI]
      = 50.0f * (float)std::sin(2.0 * std::numbers::pi * frameI / N_LOOP);

  const GOSoundTremulantWaveForm waveForm(
    make_desc(std::vector<float>(N_LOOP, 1.0f), pitchCents, 0, 0), N_CHUNK);
  const GOSoundVibratoPitchRateCurve &curve = waveForm.GetPitchRateCurve();
  LoopPosition pos(N_LOOP);
  unsigned long sumUnits = 0;

  GOAssert(waveForm.HasPitch(), "the form must have pitch");
  GOAssert(curve.GetNLoopFrames() == N_LOOP, "wrong curve length");
  GOAssert(curve.GetNChunkFrames() == N_CHUNK, "wrong curve chunk length");
  for (unsigned frameI = 0; frameI < N_LOOP; ++frameI, pos.Advance(1)) {
    // one position reads both loops
    GOAssert(waveForm.GetAmplitudeAt(pos, 1.0f) == 1.0f, "wrong amplitude");
    sumUnits += *curve.GetChunk(pos);
  }
  GOAssert(
    sumUnits == (unsigned long)N_LOOP * GOSoundResample::UPSAMPLE_FACTOR,
    "the curve must be normalised");
}

void GOTestSoundTremulantWaveForm::TestNoPitch() {
  const GOSoundTremulantWaveForm waveForm(
    make_desc(std::vector<float>(8, 1.1f), {}, 0, 0), N_CHUNK);

  GOAssert(!waveForm.HasPitch(), "the form must have no pitch");
  GOAssert(
    !waveForm.GetPitchRateCurve().IsValid(), "the curve must not be built");
  GOAssert(waveForm.GetAmpUnits().IsValid(), "the amplitude must be built");
}

void GOTestSoundTremulantWaveForm::run() {
  GO_RUN_TEST(TestLoopLength())
  GO_RUN_TEST(TestAmplitudeConversion())
  GO_RUN_TEST(TestAmplitudeClamping())
  GO_RUN_TEST(TestAmplitudeTail())
  GO_RUN_TEST(TestRamps())
  GO_RUN_TEST(TestZeroRamps())
  GO_RUN_TEST(TestGetAmplitudeAtDepth())
  GO_RUN_TEST(TestGetAmplitudeAtPositions())
  GO_RUN_TEST(TestPitch())
  GO_RUN_TEST(TestNoPitch())
}
