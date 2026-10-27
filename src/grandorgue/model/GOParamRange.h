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

  /**
   * @tparam U the type of the checked value; a double is compared as is, so a
   *   NaN is never contained
   * @param v the value to check
   * @return whether the value is within the range
   */
  template <typename U> bool Contains(U v) const {
    return v >= m_Min && v <= m_Max;
  }
};

#endif /* GOPARAMRANGE_H */
