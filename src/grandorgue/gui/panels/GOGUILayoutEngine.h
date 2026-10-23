/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOGUILAYOUTENGINE_H
#define GOGUILAYOUTENGINE_H

#include <functional>
#include <vector>

class GOGUIDisplayMetrics;
class GOGUIEnclosure;
class GOGUIManual;

class GOGUILayoutEngine {
public:
  /** The grids of the layout: a cell is addressed by a row and a column of a
   * grid */
  enum GridType { GRID_DRAWSTOP, GRID_PUSHBUTTON, GRID_ENCLOSURE };

  typedef struct {
    unsigned width;
    unsigned height;
    int y;
    int x;
    int keys_y;
    int piston_y;
  } MANUAL_RENDER_INFO;

protected:
  GOGUIDisplayMetrics &r_metrics;
  std::vector<MANUAL_RENDER_INFO> m_ManualRenderInfo;
  std::vector<GOGUIEnclosure *> m_Enclosures;
  std::vector<GOGUIManual *> m_Manuals;

  /* values computed on screen update */
  int m_HackY;
  int m_EnclosureY;
  int m_CenterY;
  int m_CenterWidth;

private:
  /**
   * The total width of nCols columns of the drawstop grid starting from
   * firstColN, as the metrics give it by the index of the column
   * @param firstColN the first column from 1
   */
  unsigned GetDrawstopColumnsWidth(unsigned firstColN, unsigned nCols);

  /**
   * The total height of nRows rows of the drawstop grid starting from
   * firstRowN. The row numbers are as in GetDrawstopBlitPosition
   */
  unsigned GetDrawstopRowsHeight(int firstRowN, unsigned nRows);

  /** The same for the pushbutton grid */
  unsigned GetButtonColumnsWidth(unsigned firstColN, unsigned nCols);
  unsigned GetButtonRowsHeight(int firstRowN, unsigned nRows);

  /** The same for the enclosure grid */
  unsigned GetEnclosureColumnsWidth(unsigned firstColN, unsigned nCols);

  /**
   * The same for the enclosure grid, but with ROW_GAP after each row
   * @param firstRowN the first row from 1
   */
  unsigned GetEnclosureRowsHeight(int firstRowN, unsigned nRows);

protected:
  /**
   * @return the width of the area of the enclosures: the sum of the widths of
   *   its columns, one per registered enclosure
   */
  unsigned GetEnclosureAreaWidth();

  /**
   * @return the height that Update() reserves for the area of the enclosures:
   *   the sum of the heights of its rows and ROW_GAP between them
   */
  unsigned GetEnclosureAreaHeight();

public:
  GOGUILayoutEngine(GOGUIDisplayMetrics &metrics);
  virtual ~GOGUILayoutEngine();

  /**
   * @return the offset of the cell number nCells from the start of a line of
   *   cells: the sum of the sizes of the first nCells cells plus gap after each
   *   of them
   * @param nCells the number of the cells before the wanted position
   * @param getCellSize the size of the cell by its index from 0
   * @param gap the gap after each cell: 0 for the columns, ROW_GAP for the rows
   */
  static unsigned computeCellOffset(
    unsigned nCells,
    const std::function<unsigned(unsigned)> &getCellSize,
    unsigned gap);

  /**
   * @return the left (top) coordinate of an item centred in a cell
   * @param cellStart the left (top) coordinate of the cell
   * @param cellSize the width (height) of the cell
   * @param itemSize the width (height) of the item
   */
  static int computeCentredPosition(
    int cellStart, unsigned cellSize, unsigned itemSize);

  /**
   * @return the X of the left edge of the area of the enclosure columns:
   *   centred on the screen, shifted by 6 pixels
   */
  static int computeEnclosureAreaX(unsigned screenWidth, unsigned areaWidth);

  /**
   * The position of an item in a cell of a grid: calls the method of the grid.
   * Buttons and labels call only this method.
   * @param gridType the grid of the cell
   * @param rowN the row of the cell (the meaning depends on the grid)
   * @param colN the column of the cell from 1
   * @param width the width of the item; used to centre it in an enclosure
   *   cell, ignored by the other grids, which place only items of their
   *   standard cell size
   * @param height the height of the item; used as the width
   * @param outX the X of the left edge of the item
   * @param outY the Y of the top edge of the item
   */
  void GetBlitPosition(
    GridType gridType,
    int rowN,
    int colN,
    unsigned width,
    unsigned height,
    int &outX,
    int &outY);

  /**
   * The position of a cell of the drawstop grid (the jambs). The size of the
   * item is not passed, so the method gives the corner of the cell plus the
   * fixed offsets, which are right only for an item of the standard size of a
   * drawstop (the cell GetDrawstopWidth() x GetDrawstopHeight()); an item of
   * another size is not centred and not aligned. To place an item of any size
   * use GetBlitPosition().
   * @param drawstopRow the row: from 1, or from 100 for the extra rows above
   *   the manuals
   * @param drawstopCol the column from 1
   * @param blitX the X of the left edge of the cell
   * @param blitY the Y of the top edge of the cell
   */
  virtual void GetDrawstopBlitPosition(
    const int drawstopRow, const int drawstopCol, int &blitX, int &blitY);

  /**
   * The same for the grid of the pushbuttons (the pistons): valid only for an
   * item of the standard size of a button cell (GetButtonWidth() x
   * GetButtonHeight()).
   * @param buttonRow the row: the number of the manual, 99 for the pedal, from
   *   100 for the extra rows
   * @param buttonCol the column from 1
   * @param blitX the X of the left edge of the cell
   * @param blitY the Y of the top edge of the cell
   */
  virtual void GetPushbuttonBlitPosition(
    const int buttonRow, const int buttonCol, int &blitX, int &blitY);

  /**
   * The third grid method, next to GetDrawstopBlitPosition and
   * GetPushbuttonBlitPosition. The grid of the enclosure area: the rows from
   * the top (1 is the row of the enclosures), the columns from 1; the sizes of
   * the cells are those of the metrics. The area is centred on the screen, the
   * item is centred in its cell. Valid after Update().
   * @param rowN the row from 1
   * @param colN the column from 1
   * @param width the width of the item
   * @param height the height of the item
   * @param outX the X of the left edge of the item
   * @param outY the Y of the top edge of the item
   */
  void GetEnclosureBlitPosition(
    int rowN, int colN, unsigned width, unsigned height, int &outX, int &outY);

  virtual int GetEnclosureY();
  virtual int GetEnclosureX(const GOGUIEnclosure *enclosure);

  virtual int GetJambLeftRightWidth();
  virtual unsigned GetJambLeftRightHeight();
  virtual int GetJambLeftRightY();
  virtual int GetJambLeftX();
  virtual int GetJambRightX();
  virtual int GetJambTopDrawstop();
  virtual int GetJambTopPiston();
  virtual unsigned GetJambTopHeight();
  virtual unsigned GetJambTopWidth();
  virtual int GetJambTopX();
  virtual int GetJambTopY();
  virtual unsigned GetPistonTopHeight();
  virtual unsigned GetPistonWidth();
  virtual int GetPistonX();
  virtual int GetCenterWidth();
  virtual int GetCenterY();
  virtual int GetCenterX();
  virtual int GetHackY();

  virtual const MANUAL_RENDER_INFO &GetManualRenderInfo(
    const unsigned manual_nb) const;

  void RegisterEnclosure(GOGUIEnclosure *enclosure);
  void RegisterManual(GOGUIManual *manual);
  unsigned GetManualNumber();

  virtual void Update();
};

#endif
