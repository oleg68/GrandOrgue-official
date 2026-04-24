/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDIMAP_H
#define GOMIDIMAP_H

#include <cstdint>

#include <wx/string.h>

#include "GONameMap.h"

class GOMidiMap {
private:
  GONameMap m_DeviceMap;
  GONameMap m_RecorderElementMap;

  static uint_fast16_t getIdByName(const GONameMap &map, const wxString &name) {
    return map.GetIdByName(name.utf8_str().data());
  }

  static uint_fast16_t ensureName(GONameMap &map, const wxString &name) {
    return map.EnsureNameExists(name.utf8_str().data());
  }

  template <typename AddingFun>
  static uint_fast16_t ensureName(
    GONameMap &map, const wxString &name, AddingFun addingFun) {
    return map.EnsureNameExists(name.utf8_str().data(), addingFun);
  }

  static wxString getNameById(const GONameMap &map, uint_fast16_t id) {
    return wxString::FromUTF8(
      map.GetNameById(static_cast<GONameMap::IdType>(id)).c_str());
  }

public:
  uint_fast16_t GetDeviceIdByLogicalName(const wxString &name) const {
    return getIdByName(m_DeviceMap, name);
  }

  uint_fast16_t EnsureLogicalName(const wxString &name) {
    return ensureName(m_DeviceMap, name);
  }

  template <typename AddingFun>
  uint_fast16_t EnsureLogicalName(const wxString &name, AddingFun addingFun) {
    return ensureName(m_DeviceMap, name, addingFun);
  }

  wxString GetDeviceLogicalNameById(uint_fast16_t id) const {
    return getNameById(m_DeviceMap, id);
  }

  // Recoder element ids start with 0
  uint_fast16_t EnsureRecorderElementName(const wxString &str) {
    return ensureName(m_RecorderElementMap, str) - 1;
  }

  wxString GetRecorderElementNameById(uint_fast16_t id) const {
    return getNameById(m_RecorderElementMap, id + 1);
  }
};

#endif
