/*
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTTREMULANTLFO_H
#define GOTESTTREMULANTLFO_H

#include <string>

#include <wx/string.h>

#include "GOTest.h"

class GOTremulant;

/**
 * Exercises the Lfo type of GOTremulant: reading its ODF keys, the keys that
 * each type reads, the getters of the Tremulants tab, the factors of Render()
 * and the data cache. The LFO wav files are written to a temporary directory.
 */
class GOTestTremulantLfo : public GOTest {
private:
  static const std::string TEST_NAME;

  /**
   * It is a class member because GOTremulant::UpdateHash() is private and this
   * class is its friend.
   * @param tremulant the tremulant to hash
   * @return the string of the hash that the tremulant contributes
   */
  static wxString hashOf(const GOTremulant &tremulant);

  /** The type Lfo and its defaults are loaded from the ODF. */
  void TestLoadDefaults();

  /** Each type reads only its own keys. */
  void TestOnlyOwnKeysAreRead();

  /** The invalid keys and the missing Period are rejected at load. */
  void TestRejectedKeys();

  /** The depth adjustments scale the rendered shapes. */
  void TestRenderFactors();

  /** An Lfo tremulant whose data is not loaded (never loaded, or its file is
   * missing) renders without modulation and does not crash. */
  void TestRenderWithoutLoadedData();

  /** A tremulant restored from the cache renders the same shapes. */
  void TestCacheRoundTrip();

  /** The compression setting changes the hash of an Lfo tremulant only. */
  void TestHashFollowsCompression();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTTREMULANTLFO_H */
