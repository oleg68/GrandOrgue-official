/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOSOUNDTESTSIGNALS_H
#define GOSOUNDTESTSIGNALS_H

#include <vector>

/**
 * Generators and measurements of test signals shared by the sound tests. It is
 * a collection of static helpers, not a test: it is not named GOTest... because
 * it is not run by GOTestExe.
 */
class GOSoundTestSignals {
public:
  /**
   * @param nFrames the length of the result
   * @param amplitudeCents the amplitude of the sine
   * @param periodFrames the period of the sine, in frames
   * @return nFrames of amplitudeCents * sin(2 * pi * frameI / periodFrames)
   */
  static std::vector<float> makeSineCents(
    unsigned nFrames, float amplitudeCents, float periodFrames);
};

#endif /* GOSOUNDTESTSIGNALS_H */
