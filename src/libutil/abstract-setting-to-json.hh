#pragma once
///@file

#include <nlohmann/json.hpp>
#include "config.hh"
#include "json-utils.hh"
// Required for instances of to_json and from_json for ExperimentalFeature
#include "experimental-features-json.hh"

namespace nix {
template<typename T>
std::map<std::string, nlohmann::json> BaseSetting<T>::toJSONObject() const
{
    auto obj = AbstractSetting::toJSONObject();
    obj.emplace("value", value);
    obj.emplace("defaultValue", defaultValue);
    obj.emplace("documentDefault", documentDefault);
    return obj;
}
}
