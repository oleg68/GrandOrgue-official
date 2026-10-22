/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOGUICOUPLERMANUALSANDVOLUMEPANEL_H
#define GOGUICOUPLERMANUALSANDVOLUMEPANEL_H

#include "GOGUILayoutEngine.h"
#include "GOGUIPanelCreator.h"

class GOGuiOrgan;
class GOGUIPanel;
class GOOrganController;

class GOGUICouplerManualsAndVolumePanel : public GOGUIPanelCreator {
private:
  /**
   * The layout engine of the panel. The area of the enclosures consists of the
   * rows, from the top: the enclosures, then the embedded tremulant buttons,
   * with ROW_GAP between the rows. The columns are as wide as the wider of an
   * enclosure and a drawstop button; the master volume is in the column 1 (it
   * has no button), the enclosure of the windchest number N and its button
   * are in the column N + 1.
   * The rows and the columns are the DispRow/DispCol of the buttons (the
   * y_pos/x_pos arguments of GOGUIButton::Init()): the panel has no drawstop
   * jambs, so GetDrawstopBlitPosition() is used for this.
   * The panel metrics (GOGUISetterDisplayMetrics) must provide the room for
   * the rows in the screen height and for the columns in the screen width, so
   * they are created together. If the screen is too narrow, the outer columns
   * are cut off.
   */
  class LayoutEngine : public GOGUILayoutEngine {
  public:
    /** The rows of the area of the enclosures, from the top */
    enum Row {
      ENCLOSURE_ROW,
      TREMULANT_BUTTON_ROW,
      LAST_ROW = TREMULANT_BUTTON_ROW
    };

  private:
    /** @return the width of a column */
    unsigned GetColumnWidth();

    /** @return the height of a row of the area of the enclosures, without the
     *  gaps
     *  @param rowI the row index from 0 to LAST_ROW */
    unsigned GetRowHeight(int rowI);

    /** @return the distance from the top of the area of the enclosures to the
     *  top of a row: the heights of the rows above it and the gaps
     *  @param rowI the row index from 0 to LAST_ROW */
    unsigned GetRowOffset(int rowI);

    /** @return the Y of the top edge of a row. Valid after Update()
     *  @param rowI the row index from 0 to LAST_ROW */
    int GetRowY(int rowI) { return GetEnclosureY() + (int)GetRowOffset(rowI); }

    /** @return the sum of the heights of all the rows and of the gaps between
     *  them */
    unsigned GetEnclosureAreaHeight() override {
      return GetRowOffset(LAST_ROW) + GetRowHeight(LAST_ROW);
    }

    /** @return the total width of all the columns */
    unsigned GetEnclosureAreaWidth() override {
      return GetColumnWidth() * m_Enclosures.size();
    }

    /**
     * @return the X of the left edge of an item centred in a column
     * @param colN the column number from 1
     * @param width the width of the item
     */
    int GetCentredInColumnX(unsigned colN, unsigned width);

  public:
    LayoutEngine(GOGUIDisplayMetrics &metrics) : GOGUILayoutEngine(metrics) {}

    /** @return the X of the enclosure: centred in its column */
    int GetEnclosureX(const GOGUIEnclosure *pEnclosure) override;

    /**
     * Called by GOGUIButton::Layout(). Places a button at the top of its row,
     * centred in its column.
     * @param rowI the row index from 0 (a Row)
     * @param colN the column number from 1
     * @param outX the X of the left edge of the button
     * @param outY the Y of the top edge of the button
     */
    void GetDrawstopBlitPosition(
      int rowI, int colN, int &outX, int &outY) override;
  };

  GOGuiOrgan &r_GuiOrgan;
  GOOrganController *p_OrganController;

  GOGUIPanel *CreatePanel(GOConfigReader &cfg);

public:
  GOGUICouplerManualsAndVolumePanel(GOGuiOrgan &guiOrgan);
  ~GOGUICouplerManualsAndVolumePanel();

  void CreatePanels(GOConfigReader &cfg);
};

#endif
