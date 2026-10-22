/*
 * Copyright 2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTORGANCONTROLLER_H
#define GOTESTORGANCONTROLLER_H

#include <string>

#include "GOTest.h"

class GOTestOrganController : public GOCommonControllerTest {

private:
  std::string name = "GOTestOrganController";

public:
  GOTestOrganController() { name = "GOTestOrganController"; }
  virtual ~GOTestOrganController();
  virtual void run();
  std::string GetName();

private:
  /*
   * Loading, saving and clearing of the organ core data.
   */
  void TestOrganCoreData();

  /*
   * Checks the internal tremulants in the combinations: the generals store
   * and restore them according to the setter mode, the divisionals do not
   * contain them. Called by TestOrganCoreData() while the organ core data is
   * loaded (a controller cannot be loaded again after it has been cleared).
   * It changes the states of the internal tremulants.
   */
  void CheckInternalTremulantsInCombinations();
};

#endif /* GOTESTORGANCONTROLLER_H */
