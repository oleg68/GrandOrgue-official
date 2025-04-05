/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOKEYCONVERT_H
#define GOKEYCONVERT_H

#include <wx/string.h>

#include <config/GOConfigEnum.h>

class GOKeyConvert {
public:
  using Shortcut = GOConfigEnum::Entry;

  static const GOConfigEnum SHORTCUTS;

  static const std::vector<Shortcut> &getShortcuts() {
    return SHORTCUTS.m_entries;
  };

  static int wXKtoVK(int what);
};

#endif
