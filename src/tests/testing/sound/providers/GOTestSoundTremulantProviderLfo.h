/*
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTSOUNDTREMULANTPROVIDERLFO_H
#define GOTESTSOUNDTREMULANTPROVIDERLFO_H

#include <string>

#include "GOMemoryPool.h"
#include "GOTest.h"

/**
 * Exercises GOSoundTremulantProviderLfo: the loop geometry, the stretching of
 * the loop to the period, the channel mapping, the storage format, the
 * compression and the cache. The wav files are written to a temporary
 * directory by GOTestWaveFileBuilder.
 */
class GOTestSoundTremulantProviderLfo : public GOTest {
private:
  static const std::string TEST_NAME;

  /** The pool that holds the sections of the loaded providers */
  GOMemoryPool m_pool;

  /** The loop length is the length of the longest loop of the file. */
  void TestLoopGeometry();

  /**
   * The loop is stretched to the period; at the native rate the output is the
   * source advanced by the 3 frames of the polyphase kernel.
   */
  void TestStretch();

  /**
   * The loop is compressed to a shorter period: for the whole and the
   * fractional rates every frame must be the source shape at its position.
   */
  void TestCompress();

  /**
   * The last frame of a long period is the source at its exact position, with
   * an error of at most 1/8 of a source frame, so the repeated period has no
   * seam: a constant rounded increment would miss it by tenths of a source
   * frame or more.
   */
  void TestPeriodEnd();

  /** Channel 1 is the pitch and channel 2 the amplitude, and the opposite. */
  void TestChannelMapping();

  /** A shape that the file does not have (channel 0) stays empty. */
  void TestMissingShape();

  /** A shape that is not requested stays empty. */
  void TestRequestedChannels();

  /** 8-, 16-, 24-bit PCM and float: 0.5 and -1.0 keep their values. */
  void TestFullScale();

  /** The average of the amplitude channel is not removed. */
  void TestDcKept();

  /** A period that is too long for the loop gives empty shapes. */
  void TestTooLongPeriod();

  /** A file without a loop, a missing channel and a missing file. */
  void TestErrors();

  /** The section keeps the channels and the depth of the file. */
  void TestStorageFormat();

  /** A compressed section renders the same shapes as an uncompressed one. */
  void TestCompression();

  /** A provider restored from the cache renders the same shapes. */
  void TestCache();

  /** A cache of another version is rejected and leaves the provider empty. */
  void TestCacheRejectsWrongVersion();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTSOUNDTREMULANTPROVIDERLFO_H */
