/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOGUIBUTTON_H
#define GOGUIBUTTON_H

#include <wx/colour.h>

#include "primitives/GOBitmap.h"
#include "primitives/GOFont.h"

#include "GOGUIMidiControl.h"

class GOButtonControl;

class GOGUIButton : public GOGUIMidiControl {
protected:
  bool m_IsPiston;
  bool m_DispKeyLabelOnLeft;
  GOButtonControl *p_ButtonControl;
  wxRect m_MouseRect;
  unsigned m_Radius;
  GOBitmap m_OnBitmap;
  GOBitmap m_OffBitmap;
  unsigned m_FontSize;
  wxString m_FontName;
  GOFont m_Font;
  wxColor m_TextColor;
  wxString m_DispLabelText;
  wxRect m_TextRect;
  unsigned m_TextWidth;
  unsigned m_DispCol;
  unsigned m_DispRow;
  unsigned m_TileOffsetX;
  unsigned m_TileOffsetY;

public:
  GOGUIButton(
    GOGUIPanel *panel, GOButtonControl *control, bool is_piston = false);

  /**
   * Initialises a button that is not defined in the ODF.
   * @param cfg the config reader
   * @param group the .cmb group of the button
   * @param x_pos the column of the button in the layout of the panel
   * @param y_pos the row of the button in the layout of the panel
   * @param image the number of the image, 0 for the default one
   * @param dispLabelText the label drawn on the button; if empty, the name of
   *   the button control is used. (Load() reads it from the ODF
   *   DispLabelText)
   */
  void Init(
    GOConfigReader &cfg,
    wxString group,
    unsigned x_pos,
    unsigned y_pos,
    unsigned image = 0,
    const wxString &dispLabelText = wxEmptyString);
  void Load(GOConfigReader &cfg, wxString group);
  void Layout();

  bool HandleMousePress(int x, int y, bool right, GOGUIMouseState &state);
  void PrepareDraw(double scale, GOBitmap *background);
  void Draw(GODC &dc);
};

#endif
