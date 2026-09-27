/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestSoundResample.h"

#include <vector>

#include "sound/dsp-kernels/GOSoundResample.h"

const std::string GOTestSoundResample::TEST_NAME = "GOTestSoundResample";

static void fill_with_ramp(std::vector<float> &data) {
  for (unsigned n = data.size(), i = 0; i < n; i++)
    data[i] = (float)i;
}

template <class ResamplerT>
static void assert_constant_rate_equivalent(
  GOTestSoundResample &test,
  const GOSoundResample &resampler,
  ResamplerT typedResampler,
  const std::string &caseName) {
  static constexpr unsigned N_OUT_FRAMES = 16;
  std::vector<float> src(64);
  std::vector<float> outConstant(N_OUT_FRAMES);
  std::vector<float> outVariable(N_OUT_FRAMES);

  fill_with_ramp(src);

  GOSoundResample::ResamplingPosition posConstant;
  GOSoundResample::ResamplingPosition posVariable;

  posConstant.Init(1.37f);
  posVariable.Init(1.37f);

  GOSoundResample::PtrFrameVector<float, float, 1> fvConstant(src.data());
  GOSoundResample::PtrFrameVector<float, float, 1> fvVariable(src.data());
  std::vector<unsigned> increments(
    N_OUT_FRAMES, posVariable.GetFractionIncrement());

  typedResampler.template ResampleBlock<
    GOSoundResample::PtrFrameVector<float, float, 1>,
    1>(posConstant, fvConstant, outConstant.data(), N_OUT_FRAMES);
  typedResampler.template ResampleBlockVariableRate<
    GOSoundResample::PtrFrameVector<float, float, 1>,
    1>(
    posVariable,
    fvVariable,
    increments.data(),
    outVariable.data(),
    N_OUT_FRAMES);

  for (unsigned frameI = 0; frameI < N_OUT_FRAMES; frameI++)
    test.GOAssert(
      outConstant[frameI] == outVariable[frameI],
      caseName + ": output item " + std::to_string(frameI) + " must match");
  test.GOAssert(
    posConstant.GetIndex() == posVariable.GetIndex(),
    caseName + ": final index must match");
  test.GOAssert(
    posConstant.GetFraction() == posVariable.GetFraction(),
    caseName + ": final fraction must match");
}

void GOTestSoundResample::TestConstantRateEquivalence() {
  GOSoundResample resampler;

  assert_constant_rate_equivalent(
    *this, resampler, GOSoundResample::LinearResampler(resampler), "LINEAR");
  assert_constant_rate_equivalent(
    *this,
    resampler,
    GOSoundResample::PolyphaseResampler(resampler),
    "POLYPHASE");
}

void GOTestSoundResample::TestVaryingRateCorrectness() {
  // Hand-picked increments, in 1/UPSAMPLE_FACTOR units.
  static constexpr unsigned UPSAMPLE_FACTOR = GOSoundResample::UPSAMPLE_FACTOR;
  const unsigned increments[] = {
    UPSAMPLE_FACTOR / 2,
    UPSAMPLE_FACTOR,
    UPSAMPLE_FACTOR * 3 / 2,
    UPSAMPLE_FACTOR / 4};
  static constexpr unsigned N_OUT_FRAMES = 4;

  std::vector<float> src(8);

  fill_with_ramp(src);

  // Independently derived trajectory (index, fraction) -> Inc(unsigned)'s
  // documented formula, and LINEAR_INTERPOLATION's coefficients are
  // (1 - fraction/UPSAMPLE_FACTOR, fraction/UPSAMPLE_FACTOR):
  //   S0 = (index=0, fraction=0)
  //   frame0: coefs=(1, 0), reads src[0],src[1] -> 1*0 + 0*1 = 0.0
  //     Inc(4096): fraction=4096, index+=0 -> S1=(0, 4096)
  //   frame1: coefs=(0.5, 0.5), reads src[0],src[1] -> 0.5*0+0.5*1 = 0.5
  //     Inc(8192): fraction=12288&8191=4096, index+=1 -> S2=(1, 4096)
  //   frame2: coefs=(0.5, 0.5), reads src[1],src[2] -> 0.5*1+0.5*2 = 1.5
  //     Inc(12288): fraction=16384&8191=0, index+=2 -> S3=(3, 0)
  //   frame3: coefs=(1, 0), reads src[3],src[4] -> 1*3+0*4 = 3.0
  //     Inc(2048): fraction=2048, index+=0 -> S4=(3, 2048)
  const float expectedOutput[N_OUT_FRAMES] = {0.0f, 0.5f, 1.5f, 3.0f};
  constexpr unsigned expectedFinalIndex = 3;
  constexpr unsigned expectedFinalFraction = 2048;

  GOSoundResample resampler;
  GOSoundResample::LinearResampler linearResampler(resampler);
  GOSoundResample::ResamplingPosition pos;
  std::vector<float> out(N_OUT_FRAMES);

  pos.Init(1.0f);

  GOSoundResample::PtrFrameVector<float, float, 1> fv(src.data());

  linearResampler.ResampleBlockVariableRate<
    GOSoundResample::PtrFrameVector<float, float, 1>,
    1>(pos, fv, increments, out.data(), N_OUT_FRAMES);

  for (unsigned frameI = 0; frameI < N_OUT_FRAMES; frameI++)
    GOAssert(
      out[frameI] == expectedOutput[frameI],
      "frame " + std::to_string(frameI)
        + ": output must match hand-computed value");
  GOAssert(pos.GetIndex() == expectedFinalIndex, "final index must match");
  GOAssert(
    pos.GetFraction() == expectedFinalFraction, "final fraction must match");
}

void GOTestSoundResample::TestRingNonWrappingReads() {
  static constexpr uint8_t N_CHANNELS = 2;
  static constexpr unsigned N_LOGICAL_FRAMES = 10;
  static constexpr unsigned N_PHYSICAL_FRAMES = N_LOGICAL_FRAMES + 4;

  std::vector<float> planar(N_CHANNELS * N_PHYSICAL_FRAMES, -1.0f);
  std::vector<float> interleaved(N_CHANNELS * N_LOGICAL_FRAMES);

  for (uint8_t ch = 0; ch < N_CHANNELS; ch++)
    for (unsigned frameI = 0; frameI < N_LOGICAL_FRAMES; frameI++) {
      const float value = ch * 1000.0f + frameI;

      planar[ch * N_PHYSICAL_FRAMES + frameI] = value;
      interleaved[frameI * N_CHANNELS + ch] = value;
    }

  GOSoundResample::RingPlanarFrameVector<float, float, N_CHANNELS> ring(
    planar.data(), N_LOGICAL_FRAMES, N_PHYSICAL_FRAMES);
  GOSoundResample::PtrFrameVector<float, float, N_CHANNELS> ref(
    interleaved.data());
  const unsigned testIndices[] = {0, 1, 5, 6};
  static constexpr unsigned N_READS = 3;

  for (uint8_t ch = 0; ch < N_CHANNELS; ch++)
    for (unsigned index : testIndices) {
      ring.Seek(index, ch);
      ref.Seek(index, ch);
      for (unsigned readI = 0; readI < N_READS; readI++)
        GOAssert(
          ring.NextItem() == ref.NextItem(),
          "index=" + std::to_string(index) + " ch=" + std::to_string(ch)
            + " read=" + std::to_string(readI) + ": must match PtrFrameVector");
    }
}

void GOTestSoundResample::TestRingMirroredSeamReads() {
  static constexpr uint8_t N_CHANNELS = 2;
  static constexpr unsigned N_LOGICAL_FRAMES = 12;
  static constexpr unsigned N_POINTS = GOSoundResample::POLYPHASE_POINTS;
  static constexpr unsigned N_PHYSICAL_FRAMES = N_LOGICAL_FRAMES + N_POINTS;

  std::vector<float> planar(N_CHANNELS * N_PHYSICAL_FRAMES);

  for (uint8_t ch = 0; ch < N_CHANNELS; ch++) {
    for (unsigned frameI = 0; frameI < N_LOGICAL_FRAMES; frameI++)
      planar[ch * N_PHYSICAL_FRAMES + frameI] = ch * 100.0f + frameI;
    // Mirror the first N_POINTS logical frames (wrapped) into the tail, as
    // the documented contract requires the ring owner to do.
    for (unsigned pointI = 0; pointI < N_POINTS; pointI++)
      planar[ch * N_PHYSICAL_FRAMES + N_LOGICAL_FRAMES + pointI]
        = ch * 100.0f + (pointI % N_LOGICAL_FRAMES);
  }

  GOSoundResample::RingPlanarFrameVector<float, float, N_CHANNELS> ring(
    planar.data(), N_LOGICAL_FRAMES, N_PHYSICAL_FRAMES);
  // Starting indices whose N_POINTS-wide read crosses the seam (the seam
  // itself is at index == N_LOGICAL_FRAMES; Seek() only accepts indices
  // below that, so the highest start tested is N_LOGICAL_FRAMES - 1, whose
  // read runs entirely past the seam except for its first item).
  const unsigned startIndices[] = {
    N_LOGICAL_FRAMES - 7,
    N_LOGICAL_FRAMES - 6,
    N_LOGICAL_FRAMES - 5,
    N_LOGICAL_FRAMES - 4,
    N_LOGICAL_FRAMES - 3,
    N_LOGICAL_FRAMES - 2,
    N_LOGICAL_FRAMES - 1};

  for (uint8_t ch = 0; ch < N_CHANNELS; ch++)
    for (unsigned startIndex : startIndices) {
      ring.Seek(startIndex, ch);
      for (unsigned pointI = 0; pointI < N_POINTS; pointI++) {
        const float expected
          = ch * 100.0f + ((startIndex + pointI) % N_LOGICAL_FRAMES);

        GOAssert(
          ring.NextItem() == expected,
          "ch=" + std::to_string(ch) + " start=" + std::to_string(startIndex)
            + " step=" + std::to_string(pointI) + ": must read wrapped value");
      }
    }
}

void GOTestSoundResample::TestRingNormalizePosition() {
  static constexpr unsigned UPSAMPLE_FACTOR = GOSoundResample::UPSAMPLE_FACTOR;
  static constexpr unsigned N_POINTS = GOSoundResample::POLYPHASE_POINTS;

  // Growth of 1 index per Inc() call (fractionIncrement == UPSAMPLE_FACTOR),
  // small ring, enough steps to wrap several times. The ring's data buffer
  // itself is never read here - only NormalizePosition() is exercised. The
  // physical size still follows the class's real margin contract (logical
  // size + N_POINTS), like every other test's ring, even though nothing
  // here reads into that margin.
  {
    static constexpr unsigned N_LOGICAL_FRAMES = 5;
    static constexpr unsigned N_PHYSICAL_FRAMES = N_LOGICAL_FRAMES + N_POINTS;
    static constexpr unsigned N_STEPS = 17;

    std::vector<float> planar(N_PHYSICAL_FRAMES);
    GOSoundResample::RingPlanarFrameVector<float, float, 1> ring(
      planar.data(), N_LOGICAL_FRAMES, N_PHYSICAL_FRAMES);
    GOSoundResample::ResamplingPosition pos;

    pos.Init(1.0f);
    for (unsigned step = 1; step <= N_STEPS; step++) {
      pos.Inc(UPSAMPLE_FACTOR);
      ring.NormalizePosition(pos);
      GOAssert(
        pos.GetIndex() == step % N_LOGICAL_FRAMES,
        "growth=1 step " + std::to_string(step)
          + ": index must match (unbounded index) % nLogicalFrames");
    }
  }

  // Growth of 2 indices per Inc() call, a different ring size - confirms
  // the single-subtract correction also holds for a larger per-call step.
  {
    static constexpr unsigned N_LOGICAL_FRAMES = 7;
    static constexpr unsigned N_PHYSICAL_FRAMES = N_LOGICAL_FRAMES + N_POINTS;
    static constexpr unsigned N_STEPS = 20;

    std::vector<float> planar(N_PHYSICAL_FRAMES);
    GOSoundResample::RingPlanarFrameVector<float, float, 1> ring(
      planar.data(), N_LOGICAL_FRAMES, N_PHYSICAL_FRAMES);
    GOSoundResample::ResamplingPosition pos;

    pos.Init(1.0f);
    for (unsigned step = 1; step <= N_STEPS; step++) {
      pos.Inc(UPSAMPLE_FACTOR * 2);
      ring.NormalizePosition(pos);
      GOAssert(
        pos.GetIndex() == (2 * step) % N_LOGICAL_FRAMES,
        "growth=2 step " + std::to_string(step)
          + ": index must match (unbounded index) % nLogicalFrames");
    }
  }
}

void GOTestSoundResample::TestRingResamplerIntegration() {
  static constexpr uint8_t N_CHANNELS = 2;
  static constexpr unsigned N_LOGICAL_FRAMES = 12;
  static constexpr unsigned N_POINTS = GOSoundResample::POLYPHASE_POINTS;
  static constexpr unsigned N_PHYSICAL_FRAMES = N_LOGICAL_FRAMES + N_POINTS;
  static constexpr unsigned N_OUT_FRAMES = 10;
  static constexpr unsigned N_OUT_ITEMS = N_OUT_FRAMES * N_CHANNELS;
  static constexpr unsigned FLAT_SIZE = 64;
  static constexpr unsigned START_INDEX = 8;
  static constexpr unsigned UPSAMPLE_FACTOR = GOSoundResample::UPSAMPLE_FACTOR;

  // A per-channel signal that repeats every N_LOGICAL_FRAMES frames - the
  // ring's planar (with mirrored tail) and the flat reference's "manually
  // unwrapped" layout are two different representations of the same
  // logical values. The per-channel offset means a channel-stride bug
  // (e.g. reading channel 1 from channel 0's own tail mirror) shows up as
  // a value mismatch, not a coincidentally-equal one.
  auto logicalValue = [](uint8_t ch, unsigned frameI) {
    return (float)(ch * 100 + frameI % N_LOGICAL_FRAMES);
  };

  std::vector<float> planar(N_CHANNELS * N_PHYSICAL_FRAMES);

  for (uint8_t ch = 0; ch < N_CHANNELS; ch++) {
    for (unsigned frameI = 0; frameI < N_LOGICAL_FRAMES; frameI++)
      planar[ch * N_PHYSICAL_FRAMES + frameI] = logicalValue(ch, frameI);
    for (unsigned pointI = 0; pointI < N_POINTS; pointI++)
      planar[ch * N_PHYSICAL_FRAMES + N_LOGICAL_FRAMES + pointI]
        = logicalValue(ch, pointI);
  }

  std::vector<float> flat(FLAT_SIZE * N_CHANNELS);

  for (unsigned frameI = 0; frameI < FLAT_SIZE; frameI++)
    for (uint8_t ch = 0; ch < N_CHANNELS; ch++)
      flat[frameI * N_CHANNELS + ch] = logicalValue(ch, frameI);

  // Increments varying around 1x - large enough on average that the
  // resampling position crosses the ring's seam and wraps at least once
  // over N_OUT_FRAMES, starting close to the seam (START_INDEX).
  const unsigned increments[N_OUT_FRAMES] = {
    UPSAMPLE_FACTOR,
    UPSAMPLE_FACTOR * 3 / 2,
    UPSAMPLE_FACTOR / 2,
    UPSAMPLE_FACTOR,
    UPSAMPLE_FACTOR * 5 / 4,
    UPSAMPLE_FACTOR,
    UPSAMPLE_FACTOR * 3 / 4,
    UPSAMPLE_FACTOR * 3 / 2,
    UPSAMPLE_FACTOR,
    UPSAMPLE_FACTOR};

  GOSoundResample::RingPlanarFrameVector<float, float, N_CHANNELS> fvRing(
    planar.data(), N_LOGICAL_FRAMES, N_PHYSICAL_FRAMES);
  GOSoundResample::PtrFrameVector<float, float, N_CHANNELS> fvFlat(flat.data());
  GOSoundResample::ResamplingPosition posRing;
  GOSoundResample::ResamplingPosition posFlat;

  posRing.Init(1.0f, START_INDEX);
  posFlat.Init(1.0f, START_INDEX);

  GOSoundResample resampler;
  GOSoundResample::PolyphaseResampler polyphaseResampler(resampler);
  std::vector<float> outRing(N_OUT_ITEMS);
  std::vector<float> outFlat(N_OUT_ITEMS);

  polyphaseResampler.ResampleBlockVariableRate<
    GOSoundResample::RingPlanarFrameVector<float, float, N_CHANNELS>,
    N_CHANNELS>(posRing, fvRing, increments, outRing.data(), N_OUT_FRAMES);
  polyphaseResampler.ResampleBlockVariableRate<
    GOSoundResample::PtrFrameVector<float, float, N_CHANNELS>,
    N_CHANNELS>(posFlat, fvFlat, increments, outFlat.data(), N_OUT_FRAMES);

  GOAssert(
    posFlat.GetIndex() >= N_LOGICAL_FRAMES,
    "test setup must actually cross the ring's seam at least once");
  for (unsigned itemI = 0; itemI < N_OUT_ITEMS; itemI++)
    GOAssert(
      outRing[itemI] == outFlat[itemI],
      "item " + std::to_string(itemI)
        + ": RingPlanarFrameVector output must match PtrFrameVector output "
          "over the equivalent unwrapped source");
  GOAssert(
    posRing.GetIndex() == posFlat.GetIndex() % N_LOGICAL_FRAMES,
    "final ring index must match the unwrapped index modulo "
    "nLogicalFrames");
}

void GOTestSoundResample::run() {
  TestConstantRateEquivalence();
  TestVaryingRateCorrectness();
  TestRingNonWrappingReads();
  TestRingMirroredSeamReads();
  TestRingNormalizePosition();
  TestRingResamplerIntegration();
}
