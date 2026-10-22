/*
 * Copyright 2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOTESTORGANCONTROLLER_H
#define GOTESTORGANCONTROLLER_H

#include <set>
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
  /**
   * The objects registered in the registries of the controller. The pointers
   * are compared as addresses only, they are never dereferenced.
   */
  struct RegistrySets {
    std::set<const void *> cacheObjects;
    std::set<const void *> referencingObjects;
    std::set<const void *> combinationButtonSets;
    std::set<const void *> midiObjects;
    std::set<const void *> midiEventHandlers;
    std::set<const void *> lifecycleListeners;
    std::set<const void *> saveableObjects;

    bool operator==(const RegistrySets &) const = default;

    /** @return true if every registry has the same number of objects */
    bool HasSameSizes(const RegistrySets &other) const;

    /** @return the numbers of the objects in every registry as a text */
    std::string ToString() const;
  };

  /**
   * Collects the objects of the registries. Freezes them for reading and
   * unfreezes them again, so that the controller is left unfrozen.
   */
  RegistrySets GetRegistrySets();

  /**
   * Checks that the model of the cleared controller is empty: no windchests,
   * enclosures, ranks, children of the root pipe config node and general
   * template elements.
   */
  void CheckClearedCoreData();

  /**
   * Loads the core data of an ODF with LoadOrganCoreData(), bypassing Load().
   * @param odfPath the path of the ODF file
   * @throws wxString if the loading fails
   */
  void LoadCoreData(const wxString &odfPath);

  /**
   * Saves the core data with SaveOrganCoreData(), writes the test.cmb file
   * and checks the saved groups.
   * @return the plain text of the saved .cmb
   */
  std::string SaveCoreData();

  /** Checks that the minimal ODF is loaded as expected. */
  void CheckLoadedCoreData();

  /*
   * Loading, saving, clearing and repeated loading of the organ core data.
   */
  void TestOrganCoreData();

  /*
   * The On indicator button does not send its MIDI events in the ordinary
   * lifecycle calls (issue #1762): the controller sends them last by itself.
   * The MIDI order itself is not tested: a recording MIDI send proxy needs a
   * live GOMidiSystem or a configured GOMidiRecorder, which GOTestExe lacks.
   */
  void TestOnStateLifecycleSending();

  /*
   * The setter exists before the first loading.
   */
  void TestGetSetterBeforeLoad();

  /*
   * ClearOrganCoreData() without a loading is a safe no-op.
   */
  void TestClearIdempotency();

  /*
   * After a failed loading ClearOrganCoreData() leaves the controller as new.
   */
  void TestLoadAfterFailedLoad();

  /*
   * After a loading failed late (in LoadCmbButtons(), after the internal
   * enclosures and the metronome windchest were added)
   * ClearOrganCoreData() leaves the controller as new.
   */
  void TestLoadAfterLateFailedLoad();

  /*
   * Checks the internal tremulants in the combinations: the generals store
   * and restore them according to the setter mode, the divisionals do not
   * contain them. Loads and clears the organ core data itself. It changes the
   * states of the internal tremulants.
   */
  void TestInternalTremulantsInCombinations();

  /*
   * Checks that the sound engine built from the loaded organ has a tremulant
   * task for every tremulant of the model, the internal ones included. Loads
   * and clears the organ core data itself.
   */
  void TestEngineTremulantTasks();
};

#endif /* GOTESTORGANCONTROLLER_H */
