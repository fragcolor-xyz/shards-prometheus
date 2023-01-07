/* SPDX-License-Identifier: BSD 3-Clause "New" or "Revised" License */
/* Copyright © 2019 Giovanni Petrantoni */

#include <dllshard.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "prometheus/client_metric.h"
#include "prometheus/counter.h"
#include "prometheus/exposer.h"
#include "prometheus/family.h"
#include "prometheus/registry.h"

using namespace shards;

namespace Prometheus {
struct Exposer {
  static SHTypesInfo inputTypes() { return CoreInfo::AnyType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyType; }

  std::optional<prometheus::Exposer> exposer;
  std::shared_ptr<prometheus::Registry> registry;
  std::unordered_map<std::string, std::reference_wrapper<
                                      prometheus::Family<prometheus::Counter>>>
      counters;
  std::string endpoint{"127.0.0.1:8080"};
  SHVar *self{nullptr};

  static inline Parameters Params{
      {"Endpoint",
       "The URL prometheus will use to pull data from."_optional,
       {CoreInfo::StringType}}};

  static SHParametersInfo parameters() { return Params; }

  void setParam(int index, SHVar value) {
    endpoint = value.payload.stringValue;
  }

  SHVar getParam(int index) { return Var{endpoint}; }

  static inline Type ExposerType{
      {SHType::Object, {.object = {'frag', 'prom'}}}};
  static inline SHExposedTypeInfo ExposerInfo{
      "Prometheus.Exposer", "The current active prometheus exposer"_optional,
      ExposerType};
  static SHExposedTypesInfo exposedVariables() { return {&ExposerInfo, 1, 0}; }

  void warmup(SHContext *context) {
    exposer.emplace(endpoint);
    registry = std::make_shared<prometheus::Registry>();
    self = Core::referenceVariable(context, "Prometheus.Exposer");
    self->valueType = SHType::Object;
    self->payload.objectValue = this;
    self->payload.objectVendorId = 'frag';
    self->payload.objectTypeId = 'prom';
    exposer->RegisterCollectable(registry);
  }

  void cleanup() {
    exposer.reset();
    registry.reset();
    if (self) {
      Core::releaseVariable(self);
      self = nullptr;
    }
  }

  SHVar activate(SHContext *context, const SHVar &input) { return input; }
};

struct Increment {
  static SHTypesInfo inputTypes() { return CoreInfo::AnyType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyType; }

  static inline Parameters Params{
      {"Name",
       "The name of the counter to increment."_optional,
       {CoreInfo::StringType}},
      {"Label",
       "The label of the value to increment."_optional,
       {CoreInfo::StringType}},
      {"Value",
       "The name of the value to increment."_optional,
       {CoreInfo::StringType}}};

  static SHParametersInfo parameters() { return Params; }

  static SHExposedTypesInfo requiredVariables() {
    return {&Exposer::ExposerInfo, 1, 0};
  }

  std::string _name;
  std::string _label;
  std::string _value;
  SHVar *expo{nullptr};
  std::optional<std::reference_wrapper<prometheus::Counter>> _counter;

  void setParam(int index, SHVar val) {
    switch (index) {
    case 0:
      _name = val.payload.stringValue;
      break;
    case 1:
      _label = val.payload.stringValue;
      break;
    case 2:
      _value = val.payload.stringValue;
      break;
    default:
      break;
    }
  }

  SHVar getParam(int index) {
    switch (index) {
    case 0:
      return Var{_name};
    case 1:
      return Var{_label};
    case 2:
      return Var{_value};
    default:
      return Var{};
    }
  }

  void warmup(SHContext *context) {
    expo = Core::referenceVariable(context, "Prometheus.Exposer");

    if (expo->valueType != SHType::Object ||
        expo->payload.objectVendorId != 'frag' ||
        expo->payload.objectTypeId != 'prom')
      throw WarmupError{"Prometheus.Exposer is not an exposer"};

    Exposer *e = reinterpret_cast<Exposer *>(expo->payload.objectValue);

    if (e->counters.count(_name) == 0) {
      auto &counter = prometheus::BuildCounter().Name(_name).Help("").Register(
          *e->registry);
      e->counters.emplace(_name, counter);
      _counter = counter.Add({{_label, _value}});
    } else {
      auto &counter = e->counters.at(_name);
      _counter = counter.get().Add({{_label, _value}});
    }
  }

  void cleanup() {
    if (expo) {
      Core::releaseVariable(expo);
      expo = nullptr;
    }
    _counter.reset();
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    _counter->get().Increment();
    return input;
  }
};
} // namespace Prometheus
namespace shards {
void registerShards() {
  REGISTER_SHARD("Prometheus.Exposer", Prometheus::Exposer);
  REGISTER_SHARD("Prometheus.Increment", Prometheus::Increment);
}
} // namespace shards