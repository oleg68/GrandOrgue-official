/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2025 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "go-wx-yaml.h"

#include <wx/string.h>

namespace YAML {

Node convert<wxString>::encode(const wxString &rhs) {
  Node node;

  if (!rhs.IsEmpty())
    node = rhs.utf8_str().data();
  return node;
}

bool convert<wxString>::decode(const Node &node, wxString &rhs) {
  const bool isNull = !node.IsDefined() || node.IsNull();
  bool isValid = isNull || node.IsScalar();

  if (isValid) {
    if (isNull)
      rhs = wxEmptyString;
    else
      rhs = wxString::FromUTF8(node.as<std::string>().c_str());
  }
  return isValid;
}

} // namespace YAML

YAML::Node get_from_map_or_null(const YAML::Node &container, const char *key) {
  YAML::Node resultNode;

  if (container.IsDefined() && container.IsMap()) {
    YAML::Node valueNode = container[key];

    if (valueNode.IsDefined())
      resultNode = valueNode;
  }
  return resultNode;
}

void put_to_map_if_not_null(
  YAML::Node &container, const char *key, const YAML::Node &value) {
  if (!value.IsNull())
    container[key] = value;
}

const char *const NAME = "name";

void put_to_map_with_name(
  YAML::Node &container,
  const char *key,
  const wxString &nameValue,
  const char *valueLabel,
  const YAML::Node &value) {
  if (!value.IsNull()) {
    YAML::Node childNode = container[key];

    childNode[NAME] = nameValue;
    childNode[valueLabel] = value;
  }
}

static void put_to_map_by_path(
  YAML::Node &container,
  std::vector<wxString>::const_iterator &current,
  const std::vector<wxString>::const_iterator &end,
  const wxString &lastKey,
  const YAML::Node &node) {
  if (current != end) {
    const char *pKey = current->utf8_str().data();
    YAML::Node child = container[pKey];
    const bool isNew = !child.IsDefined();

    if (isNew)
      container[pKey] = child;
    current++;
    put_to_map_by_path(child, current, end, lastKey, node);
  } else
    put_to_map_if_not_null(container, lastKey, node);
}

void put_to_map_by_path_if_not_null(
  YAML::Node &rootNode,
  const std::vector<wxString> &path,
  const wxString &lastKey,
  const YAML::Node &node) {
  if (!node.IsNull()) {
    std::vector<wxString>::const_iterator current = path.cbegin();

    put_to_map_by_path(rootNode, current, path.cend(), lastKey, node);
  }
}

static YAML::Node get_from_map_by_path(
  const YAML::Node &container,
  std::vector<wxString>::const_iterator &current,
  const std::vector<wxString>::const_iterator &end,
  const wxString &lastKey) {
  YAML::Node res;

  if (current != end) {
    YAML::Node child = get_from_map_or_null(container, *current);

    current++;
    res = get_from_map_by_path(child, current, end, lastKey);
  } else
    res = get_from_map_or_null(container, lastKey);
  return res;
}

YAML::Node get_from_map_by_path_or_null(
  const YAML::Node &rootNode,
  const std::vector<wxString> &path,
  const wxString &lastKey) {
  std::vector<wxString>::const_iterator current = path.cbegin();

  return get_from_map_by_path(rootNode, current, path.cend(), lastKey);
}

const YAML::Node &operator>>(const YAML::Node &yamlNode, unsigned &value) {
  if (yamlNode.IsDefined() && yamlNode.IsScalar())
    value = yamlNode.as<unsigned>();
  return yamlNode;
}

const YAML::Node &operator>>(const YAML::Node &yamlNode, int &value) {
  if (yamlNode.IsDefined() && yamlNode.IsScalar())
    value = yamlNode.as<int>();
  return yamlNode;
}

const YAML::Node &operator>>(const YAML::Node &yamlNode, bool &value) {
  if (yamlNode.IsDefined() && yamlNode.IsScalar())
    value = yamlNode.as<bool>();
  return yamlNode;
}