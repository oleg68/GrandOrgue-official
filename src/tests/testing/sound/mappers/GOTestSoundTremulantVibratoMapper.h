/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTSOUNDTREMULANTVIBRATOMAPPER_H
#define GOTESTSOUNDTREMULANTVIBRATOMAPPER_H

#include <string>

#include "GOTest.h"

/**
 * Exercises GOSoundTremulantVibratoMapper: the pitch rate chunk of the
 * tremulant task reaches the vibrato processor in every round, with no
 * last-value cache. Friended by GOSoundVibratoProcessor so that the cases can
 * stomp and read its m_PitchRateChunk directly: this tells "the mapper passed
 * the chunk" apart from "the processor happened to hold it already".
 */
class GOTestSoundTremulantVibratoMapper : public GOTest {
private:
  static const std::string TEST_NAME;

  /** An engaged task with pitch modulation: the processor gets exactly the
   * chunk of the task after the call, whatever it held before. */
  void TestPassesChunk();

  /** A task without pitch modulation: the processor gets the chunk with no
   * rates, whatever it held before. */
  void TestPassesBypassChunk();

  /** Two rounds in a row: after each call the processor holds that round's
   * chunk, so nothing is cached between the rounds. */
  void TestFollowsRounds();

  /** Without pitch modulation a signal goes through the processor unchanged
   * bit for bit; with it the pitch of the output follows the recorded rates of
   * the chunks. */
  void TestEndToEnd();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTSOUNDTREMULANTVIBRATOMAPPER_H */
