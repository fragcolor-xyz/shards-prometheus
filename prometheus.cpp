/* SPDX-License-Identifier: BSD 3-Clause "New" or "Revised" License */
/* Copyright © 2019 Giovanni Petrantoni */

#include <shards/dllshard.hpp>

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
#include "prometheus/gauge.h"
#include "prometheus/histogram.h"
#include "prometheus/registry.h"
#include "shards/shards.hpp"

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

  std::unordered_map<std::string, std::reference_wrapper<
                                      prometheus::Family<prometheus::Gauge>>>
      gauges;

  std::unordered_map<
      std::string,
      std::reference_wrapper<prometheus::Family<prometheus::Histogram>>>
      histograms;

  std::string endpoint{"127.0.0.1:9090"};
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
    auto msg = "Opening prometheus exposer on " + endpoint;
    shards::Core::log(toSWL(msg));
    exposer.emplace(endpoint);
    registry = std::make_shared<prometheus::Registry>();
    self = Core::referenceVariable(context, "Prometheus.Exposer"_swl);
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

struct Base {
  SeqVar _buckets;

  static SHTypesInfo inputTypes() { return CoreInfo::FloatType; }
  static SHTypesInfo outputTypes() { return CoreInfo::FloatType; }

  static inline Parameters Params{
      {"Name",
       "The name of the counter to increment."_optional,
       {CoreInfo::StringType}},
      {"Label",
       "The label of the value to increment."_optional,
       {CoreInfo::StringType}},
      {"Value",
       "The name of the value to increment."_optional,
       {CoreInfo::StringType}},
      {"Buckets",
       "The buckets to use for the histogram."_optional,
       {CoreInfo::FloatSeqType}}};

  static SHParametersInfo parameters() { return Params; }

  static SHExposedTypesInfo requiredVariables() {
    return {&Exposer::ExposerInfo, 1, 0};
  }

  std::string _name;
  std::string _label;
  std::string _value;
  SHVar *expo{nullptr};

  void setParam(int index, SHVar val) {
    switch (index) {
    case 0:
      _name = std::string(val.payload.stringValue, val.payload.stringLen);
      break;
    case 1:
      _label = std::string(val.payload.stringValue, val.payload.stringLen);
      break;
    case 2:
      _value = std::string(val.payload.stringValue, val.payload.stringLen);
      break;
    case 3:
      _buckets = *static_cast<SeqVar *>(&val);
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
    case 3:
      return _buckets;
    default:
      return Var{};
    }
  }

  void warmup(SHContext *context) {
    expo = Core::referenceVariable(context, "Prometheus.Exposer"_swl);

    if (expo->valueType != SHType::Object ||
        expo->payload.objectVendorId != 'frag' ||
        expo->payload.objectTypeId != 'prom')
      throw WarmupError{"Prometheus.Exposer is not an exposer"};
  }

  void cleanup() {
    if (expo) {
      Core::releaseVariable(expo);
      expo = nullptr;
    }
  }
};

struct Increment : Base {
  std::optional<std::reference_wrapper<prometheus::Counter>> _counter;

  void warmup(SHContext *context) {
    Base::warmup(context);

    Exposer *e = reinterpret_cast<Exposer *>(expo->payload.objectValue);

    if (e->counters.count(_name) == 0) {
      auto &counter = prometheus::BuildCounter().Name(_name).Help("").Register(
          *e->registry);
      e->counters.emplace(_name, counter);
      if (_label.empty())
        _counter = counter.Add({});
      else
        _counter = counter.Add({{{_label, _value}}});
    } else {
      auto &counter = e->counters.at(_name);
      if (_label.empty())
        _counter = counter.get().Add({});
      else
        _counter = counter.get().Add({{{_label, _value}}});
    }
  }

  void cleanup() {
    Base::cleanup();

    _counter.reset();
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    // won't work if negative so throw in that case to correct users
    if (input.payload.floatValue < 0)
      throw ActivationError("Prometheus Increment should be a positive number");
    _counter->get().Increment(input.payload.floatValue);
    return input;
  }
};

struct Gauge : Base {
  std::optional<std::reference_wrapper<prometheus::Gauge>> _gauge;

  void warmup(SHContext *context) {
    Base::warmup(context);

    Exposer *e = reinterpret_cast<Exposer *>(expo->payload.objectValue);

    if (e->counters.count(_name) == 0) {
      auto &gauge =
          prometheus::BuildGauge().Name(_name).Help("").Register(*e->registry);
      e->gauges.emplace(_name, gauge);
      if (_label.empty())
        _gauge = gauge.Add({});
      else
        _gauge = gauge.Add({{{_label, _value}}});
    } else {
      auto &gauge = e->gauges.at(_name);
      if (_label.empty())
        _gauge = gauge.get().Add({});
      else
        _gauge = gauge.get().Add({{{_label, _value}}});
    }
  }

  void cleanup() {
    Base::cleanup();

    _gauge.reset();
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    _gauge->get().Set(input.payload.floatValue);
    return input;
  }
};

#include <prometheus/histogram.h>

struct Histogram : Base {
  std::optional<std::reference_wrapper<prometheus::Histogram>> _histogram;

  void warmup(SHContext *context) {
    Base::warmup(context);

    Exposer *e = reinterpret_cast<Exposer *>(expo->payload.objectValue);

    std::vector<double> buckets;
    for (auto &bucket : _buckets) {
      shassert(bucket.valueType == SHType::Float &&
               "Histogram buckets must be floats");
      buckets.push_back(bucket.payload.floatValue);
    }

    if (e->histograms.count(_name) == 0) {
      auto &histogram =
          prometheus::BuildHistogram().Name(_name).Register(*e->registry);
      e->histograms.emplace(_name, histogram);
      if (_label.empty())
        _histogram = std::optional(
            std::ref(histogram.Add({}, prometheus::Histogram::BucketBoundaries{
                                           buckets.begin(), buckets.end()})));
      else
        _histogram = std::optional(std::ref(histogram.Add(
            {{_label, _value}}, prometheus::Histogram::BucketBoundaries{
                                    buckets.begin(), buckets.end()})));
    } else {
      auto &histogram = e->histograms.at(_name);
      if (_label.empty())
        _histogram = std::optional(std::ref(
            histogram.get().Add({}, prometheus::Histogram::BucketBoundaries{
                                        buckets.begin(), buckets.end()})));
      else
        _histogram = std::optional(std::ref(histogram.get().Add(
            {{_label, _value}}, prometheus::Histogram::BucketBoundaries{
                                    buckets.begin(), buckets.end()})));
    }
  }

  void cleanup() {
    Base::cleanup();

    _histogram.reset();
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    _histogram->get().Observe(input.payload.floatValue);
    return input;
  }
};
} // namespace Prometheus
namespace shards {
void registerExternalShards() {
  REGISTER_SHARD("Prometheus.Exposer", Prometheus::Exposer);
  REGISTER_SHARD("Prometheus.Increment", Prometheus::Increment);
  REGISTER_SHARD("Prometheus.Gauge", Prometheus::Gauge);
  REGISTER_SHARD("Prometheus.Histogram", Prometheus::Histogram);
}
} // namespace shards