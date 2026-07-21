/*
* prometheus-cpp-lite — header-only C++ library for exposing Prometheus metrics
* https://github.com/biaks/prometheus-cpp-lite
*
* Copyright (c) 2026 Yan Kryukov ianiskr@gmail.com
* Licensed under the MIT License
*/

#pragma once

#include "prometheus/core.h"

namespace prometheus {

  // =============================================================================
  // gauge_t — Prometheus gauge metric
  //
  // A gauge is a metric that represents a single numerical value that can
  // arbitrarily go up and down (e.g. temperature, memory usage, active requests).
  // Supports two ownership modes controlled by MetricValue:
  //
  //   gauge_t<int64_t>   — owning:    holds its own std::atomic<int64_t>.
  //   gauge_t<int64_t&>  — reference: binds to the atomic of an owning gauge.
  //
  // The reference form enables zero-copy metric handles (see modify_t<>).
  // =============================================================================

  /// @brief Prometheus gauge metric — a value that can go up and down.
  ///
  /// @tparam MetricValue Value type. Use a plain type (e.g. `int64_t`) for an
  ///         owning gauge, or a reference type (e.g. `int64_t&`) for a
  ///         zero-copy reference handle.
  template <typename MetricValue = int64_t>
  class gauge_t : public Metric {

  public:
    using storage_type = typename atomic_storage<MetricValue>::storage_type;
    using value_type   = typename atomic_storage<MetricValue>::value_type;
    using Family       = CustomFamily<gauge_t<value_type> >; ///< Legacy alias for backward compatibility.

  private:
    storage_type value;
    value_type   snapshot_value { 0 };

    /// Grant access to internals so the reference form can bind to the owning form.
    friend gauge_t<value_type&>;

  public:
    using Value = value_type;

    // --- Owning constructor (gauge_t<value_type>) -------------------------------

    /// @brief Constructs an owning gauge initialized to zero.
    /// @param labels Per-metric dimensional labels (copied and owned).
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    explicit gauge_t(const labels_t& labels)
      : Metric(labels), value(0) {}

    // --- SimpleAPI: easy to use from the user's side, non-trivial internally.
    // --- Reference constructors (gauge_t<value_type&>) --------------------------

    /// @brief Constructs a reference gauge that binds to an existing owning gauge.
    /// @param other Owning gauge whose atomic value and labels are referenced.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    gauge_t(gauge_t<value_type>& other)
      // gauge_t<value_type>& -> gauge_t<value_type&>
      : Metric(other.labels_ptr), value(other.value), snapshot_value(other.snapshot_value) {}

    /// @brief Constructs a reference gauge by adding an owning gauge to the given family.
    /// The metric value type compatibility with the family is checked at runtime.
    /// @param family Family to add the owning gauge to.
    /// @param labels Per-metric dimensional labels.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    gauge_t(family_t& family, const labels_t& labels = {})
      // family_t::Add<>() -> Family::Add<>() -> gauge_t<value_type>& -> gauge_t<value_type&>
      : gauge_t(family.Add<gauge_t<value_type> >(labels)) {}

    /// @brief Constructs a reference gauge by adding an owning gauge to the given family.
    /// The metric value type compatibility with the family is enforced at compile time.
    /// @param family Family to add the owning gauge to.
    /// @param labels Per-metric dimensional labels.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    gauge_t(custom_family_t<gauge_t<value_type> >& family, const labels_t& labels = {})
      // custom_family_t<>::Add() -> CustomFamily<>::Add() -> gauge_t<value_type>& -> gauge_t<value_type&>
      : gauge_t(family.Add(labels)) {}

    /// @brief Constructs a reference gauge, creating both family and metric in the given registry.
    /// @param registry Registry to register the family in.
    /// @param name     Metric family name.
    /// @param help     Help/description string.
    /// @param labels   Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    gauge_t(Registry& registry, const std::string& name, const std::string& help, const labels_t& labels = {})
      // registry::Add() -> Family::Add<gauge_t<value_type>>() -> gauge_t<value_type>& -> gauge_t<value_type&>
      : gauge_t(registry.Add(name, help).Add<gauge_t<value_type> >(labels)) {}

    /// @brief Constructs a reference gauge, creating both family and metric in the given registry.
    /// @param registry Shared pointer to Registry to register the family in.
    /// @param name     Metric family name.
    /// @param help     Help/description string.
    /// @param labels   Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    gauge_t(std::shared_ptr<registry_t>& registry, const std::string& name, const std::string& help, const labels_t& labels = {})
      // registry::Add() -> Family::Add<gauge_t<value_type>>() -> gauge_t<value_type>& -> gauge_t<value_type&>
      : gauge_t(registry->Add(name, help).Add<gauge_t<value_type> >(labels)) {}

    /// @brief Constructs a reference gauge using the global registry.
    /// @param name   Metric family name.
    /// @param help   Help/description string.
    /// @param labels Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    gauge_t(const std::string& name, const std::string& help, const labels_t& labels = {})
      // global_registry::Add() -> Family::Add<gauge_t<value_type>>() -> gauge_t<value_type>& -> gauge_t<value_type&>
      : gauge_t(global_registry.Add(name, help).Add<gauge_t<value_type>>(labels)) {}

    // --- Conversion: owning → reference -----------------------------------------

    /// @brief Implicit conversion from an owning gauge to a reference gauge.
    /// @return A reference gauge bound to this owning gauge.
    //template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    //operator gauge_t<value_type&>() {
    //  return gauge_t<value_type&>(*this);
    //}

    // --- Non-copyable (owning form only) ----------------------------------------

    /// @brief Owning gauges are non-copyable to prevent accidental duplication.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    gauge_t(const gauge_t&) = delete;

    /// @brief Owning gauges are non-copy-assignable.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    gauge_t& operator=(const gauge_t&) = delete;

    // --- Public API (shared by both owning and reference forms) -----------------

    /// @brief Increments the gauge by one.
    void Increment() { ++value; }

    /// @brief Increments the gauge by the given value.
    /// @param val Amount to add (may be negative).
    void Increment(const value_type& val) { value += val; }

    /// @brief Decrements the gauge by one.
    void Decrement() { --value; }

    /// @brief Decrements the gauge by the given value.
    /// @param val Amount to subtract (may be negative).
    void Decrement(const value_type& val) { value -= val; }

    /// @brief Sets the gauge to the specified value.
    /// @param val New value.
    void Set(const value_type& val) { value.store(val); }

    /// @brief Sets the gauge to the current Unix timestamp (seconds since epoch).
    void SetToCurrentTime() { value.store(static_cast<value_type>(std::time(nullptr))); }

    /// @brief Returns the current gauge value.
    /// @return Atomically loaded gauge value.
    value_type Get() const { return value.load(); }

    /// @brief Pre-increment operator (increments by one).
    /// @return Reference to this gauge.
    gauge_t& operator++()                    { ++value;    return *this; }

    /// @brief Post-increment operator (increments by one, returns reference — not previous value).
    /// @return Reference to this gauge.
    gauge_t& operator++(int)                 { ++value;    return *this; }

    /// @brief Pre-decrement operator (decrements by one).
    /// @return Reference to this gauge.
    gauge_t& operator--()                    { --value;    return *this; }

    /// @brief Post-decrement operator (decrements by one, returns reference — not previous value).
    /// @return Reference to this gauge.
    gauge_t& operator--(int)                 { --value;    return *this; }

    /// @brief Compound addition operator.
    /// @param v Value to add.
    /// @return Reference to this gauge.
    gauge_t& operator+=(const value_type& v) { value += v; return *this; }

    /// @brief Compound subtraction operator.
    /// @param v Value to subtract.
    /// @return Reference to this gauge.
    gauge_t& operator-=(const value_type& v) { value -= v; return *this; }

    /// @brief Sets the gauge to the specified value.
    /// @param val New value.
    gauge_t& operator=(const value_type& v) { value.store(v); return *this; }

    // --- Metric interface overrides ---------------------------------------------

    /// @brief Returns the Prometheus type name for this metric.
    /// @return "gauge".
    const char* type_name() const override { return "gauge"; }

    /// @brief Freezes the current value into a snapshot for consistent serialization.
    void collect() override { snapshot_value = value.load(); }

    /// @brief Writes this gauge's data line in the Prometheus text exposition format.
    /// @param out         Output stream.
    /// @param family_name Metric family name (line prefix).
    /// @param base_labels Constant labels from the owning family.
    void serialize(std::ostream& out, const std::string& family_name,
                   const labels_t& base_labels) const override {
      TextSerializer::WriteLine(out, family_name, base_labels, this->get_labels(), snapshot_value);
    }
  };

  // =============================================================================
  // Convenience aliases
  // =============================================================================

  /// @brief Generic gauge alias with configurable value type.
  template <typename T = int64_t>
  using Gauge = gauge_t<T>;

  /// @brief Fluent builder alias for the default gauge type (legacy from prometheus-cpp, should be double).
  using BuildGauge = Builder<gauge_t<double>>;

  /// @brief Zero-copy reference handle to a default gauge (SimpleAPI).
  using gauge_metric_t = typename modify_t<gauge_t<>>::metric_ref;

  /// @brief Typed family alias for default gauges (SimpleAPI).
  using gauge_family_t = custom_family_t<gauge_t<>>;

} // namespace prometheus