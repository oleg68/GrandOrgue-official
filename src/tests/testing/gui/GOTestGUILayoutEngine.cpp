/*
 * Copyright 2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestGUILayoutEngine.h"

#include <format>

#include "gui/panels/GOGUILayoutEngine.h"

#include "GOTestScope.h"

const std::string GOTestGUILayoutEngine::TEST_NAME = "GOTestGUILayoutEngine";

void GOTestGUILayoutEngine::TestGridBlitPosition() {
  // The sizes of PanelMetrics: the master column 80, the others 52; the
  // enclosure row 61, the row of the pistons 32; ROW_GAP 12
  const auto getColumnWidth
    = [](unsigned colN) { return colN == 1 ? 80u : 52u; };
  const auto getRowHeight = [](unsigned rowN) { return rowN == 1 ? 61u : 32u; };
  const int areaX = 200;
  const int areaY = 50;
  int enclosureX = 0;
  int enclosureY = 0;
  int pistonX = 0;
  int pistonY = 0;
  int labelX = 0;
  int labelY = 0;

  GOGUILayoutEngine::computeGridBlitPosition(
    areaX,
    areaY,
    getColumnWidth,
    getRowHeight,
    12,
    1,
    2,
    46,
    61,
    enclosureX,
    enclosureY);
  GOGUILayoutEngine::computeGridBlitPosition(
    areaX,
    areaY,
    getColumnWidth,
    getRowHeight,
    12,
    2,
    2,
    32,
    32,
    pistonX,
    pistonY);
  GOGUILayoutEngine::computeGridBlitPosition(
    areaX,
    areaY,
    getColumnWidth,
    getRowHeight,
    12,
    2,
    1,
    80,
    25,
    labelX,
    labelY);

  GOAssert(
    pistonX + 16 == enclosureX + 23,
    std::format(
      "The centre of the piston ({}) should equal the centre of the "
      "enclosure ({}) in the same column",
      pistonX + 16,
      enclosureX + 23));
  GOAssert(
    pistonY == areaY + 61 + 12,
    std::format(
      "The piston Y should be {}, got: {}", areaY + 61 + 12, pistonY));
  GOAssert(
    labelX == areaX,
    std::format("The label X should be {}, got: {}", areaX, labelX));
  GOAssert(
    labelY == areaY + 61 + 12 + 4,
    std::format(
      "The label should be centred in the row of 32 (16 - 12 = 4): Y {}, "
      "got: {}",
      areaY + 61 + 12 + 4,
      labelY));
}

void GOTestGUILayoutEngine::TestCentredPosition() {
  GOAssert(
    GOGUILayoutEngine::computeCentredPosition(100, 52, 52) == 100,
    "An item that fills its cell should be at the start of the cell");
  GOAssert(
    GOGUILayoutEngine::computeCentredPosition(100, 52, 32) == 110,
    "A 32 px item in a 52 px cell should be shifted by 10");
  GOAssert(
    GOGUILayoutEngine::computeCentredPosition(100, 80, 52) == 114,
    "A 52 px item in an 80 px cell should be shifted by 14");
  GOAssert(
    GOGUILayoutEngine::computeCentredPosition(100, 80, 80) == 100,
    "An 80 px label in an 80 px cell should be at the start of the cell");
  GOAssert(
    GOGUILayoutEngine::computeCentredPosition(100, 40, 63) == 89,
    "An item that is higher than its cell should be shifted up");
}

void GOTestGUILayoutEngine::TestEnclosureAreaX() {
  GOAssert(
    GOGUILayoutEngine::computeEnclosureAreaX(600, 156) == 225,
    "The area should be centred on the screen with the shift of 6");

  const int narrowX = GOGUILayoutEngine::computeEnclosureAreaX(100, 156);

  GOAssert(
    narrowX < 0,
    std::format(
      "The area that is wider than the screen should start at a negative X, "
      "got: {}",
      narrowX));
}

void GOTestGUILayoutEngine::run() {
  GO_RUN_TEST(TestGridBlitPosition())
  GO_RUN_TEST(TestCentredPosition())
  GO_RUN_TEST(TestEnclosureAreaX())
}
