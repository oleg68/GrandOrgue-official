/*
 * Copyright 2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTORGANSETTINGSTREMULANTSTAB_H
#define GOTESTORGANSETTINGSTREMULANTSTAB_H

#include <string>

#include "gui/dialogs/organ-settings/GOOrganSettingsTremulantsTab.h"

#include "GOTest.h"

/**
 * Tests the logic of GOOrganSettingsTremulantsTab that does not depend on the
 * widgets: parsing of the edits and applying the changes to the tremulants. It
 * is a friend of the tab and needs no tab object and no display.
 */
class GOTestOrganSettingsTremulantsTab : public GOTest {
private:
  using Tab = GOOrganSettingsTremulantsTab;

  static const std::string TEST_NAME;

  /** The valid texts are parsed, the absent ones are left absent. */
  void TestParseValid();

  /** The borders of the ranges are valid, just outside them are not. */
  void TestParseRangeBorders();

  /** Not a number, a fraction in an integer edit and NaN are reported. */
  void TestParseProblems();

  /** Only the first invalid edit, top to bottom, is reported. */
  void TestParseReportsFirstError();

  /** applyChanges writes only the present values to each selected tremulant
   * and calls the hook once with the really changed ones. */
  void TestApplyChanges();

  /** resetToDefault resets the selected tremulants only and calls the hook
   * once with the really changed ones. */
  void TestResetToDefault();

public:
  std::string GetName() override { return TEST_NAME; }
  void run() override;
};

#endif /* GOTESTORGANSETTINGSTREMULANTSTAB_H */
