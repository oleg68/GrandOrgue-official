/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOPARAMRANGE_H
#define GOPARAMRANGE_H

/**
 * The allowed range of a parameter, both ends included.
 * @tparam T the type of the parameter: int or float
 */
template <typename T> struct GOParamRange {
  T m_Min;
  T m_Max;

  bool Contains(T v) const { return v >= m_Min && v <= m_Max; }
};

#endif /* GOPARAMRANGE_H */
