/*
 * Copyright 2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOTestGUILayoutEngine.h"

#include <format>
#include <vector>

#include "gui/panels/GOGUILayoutEngine.h"

#include "GOTestScope.h"

const std::string GOTestGUILayoutEngine::TEST_NAME = "GOTestGUILayoutEngine";

void GOTestGUILayoutEngine::TestCellOffset() {
  const std::vector<unsigned> columnWidths = {52, 80, 52};
  const std::vector<unsigned> rowHeights = {63, 40};
  const auto getColumnWidth = [&](unsigned colI) { return columnWidths[colI]; };
  const auto getRowHeight = [&](unsigned rowI) { return rowHeights[rowI]; };

  GOAssert(
    GOGUILayoutEngine::computeCellOffset(0, getColumnWidth, 0) == 0,
    "The offset before the first cell should be 0");
  GOAssert(
    GOGUILayoutEngine::computeCellOffset(1, getColumnWidth, 0) == 52,
    "The offset of the second column should be the width of the first");
  GOAssert(
    GOGUILayoutEngine::computeCellOffset(2, getColumnWidth, 0) == 132,
    "The offset of the third column should be 52 + 80");
  GOAssert(
    GOGUILayoutEngine::computeCellOffset(3, getColumnWidth, 0) == 184,
    "The total width of the three columns should be 184");
  GOAssert(
    GOGUILayoutEngine::computeCellOffset(1, getRowHeight, 12) == 75,
    "The offset of the second row should be 63 + the gap 12");
  GOAssert(
    GOGUILayoutEngine::computeCellOffset(2, getRowHeight, 12) == 127,
    "The total height of the two rows with the gap after each should be "
    "63 + 12 + 40 + 12");
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
  GO_RUN_TEST(TestCellOffset())
  GO_RUN_TEST(TestCentredPosition())
  GO_RUN_TEST(TestEnclosureAreaX())
}
