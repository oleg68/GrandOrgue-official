/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiEventPattern.h"

#include <algorithm>
#include <math.h>

#include "yaml/go-wx-yaml.h"

#include "midi/GOMidiMap.h"

bool GOMidiEventPattern::operator==(const GOMidiEventPattern &other) const {
  return deviceId == other.deviceId && channel == other.channel
    && key == other.key && low_value == other.low_value
    && high_value == other.high_value && useNoteOff == other.useNoteOff;
}

static const char *C_DEVICE = "device";

void GOMidiEventPattern::DeviceIdToYaml(
  YAML::Node &eventNode, const GOMidiMap &map) const {
  if (deviceId)
    eventNode[C_DEVICE] = map.GetDeviceLogicalNameById(deviceId);
}

void GOMidiEventPattern::DeviceIdFromYaml(
  const YAML::Node &eventNode, GOMidiMap &map) {
  YAML::Node deviceNode = eventNode[C_DEVICE];

  deviceId = deviceNode.IsDefined() && deviceNode.IsScalar()
    ? map.GetDeviceIdByLogicalName(deviceNode.as<wxString>())
    : 0;
}

int GOMidiEventPattern::convertValueBetweenRanges(
  int srcValue, int srcLow, int srcHigh, int dstLow, int dstHigh) {
  const int dstAbsLow = std::min(dstLow, dstHigh);
  const int dstAbsHigh = std::max(dstLow, dstHigh);
  int dstValue = srcValue - srcLow;

  if (srcHigh != srcLow)
    dstValue = dstLow
      + round(dstValue * (float)(dstHigh - dstLow) / (srcHigh - srcLow));
  if (dstValue < dstAbsLow)
    dstValue = dstAbsLow;
  if (dstValue > dstAbsHigh)
    dstValue = dstAbsHigh;
  return dstValue;
}
