
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
  // counter_t — Prometheus counter metric
  //
  // A counter is a cumulative metric that can only increase (or be reset to zero
  // on process restart).  Supports two ownership modes controlled by MetricValue:
  //
  //   counter_t<uint64_t>   — owning:    holds its own std::atomic<uint64_t>.
  //   counter_t<uint64_t&>  — reference: binds to the atomic of an owning counter.
  //
  // The reference form enables zero-copy metric handles (see make_ref<>).
  // =============================================================================

  /// @brief Prometheus counter metric — monotonically increasing value.
  ///
  /// @tparam MetricValue Value type. Use a plain type (e.g. `uint64_t`) for an
  ///         owning counter, or a reference type (e.g. `uint64_t&`) for a
  ///         zero-copy reference handle.
  template <typename MetricValue = uint64_t>
  class counter_t : public Metric {

  public:
    using storage_type = typename atomic_storage<MetricValue>::storage_type;
    using value_type   = typename atomic_storage<MetricValue>::value_type;
    using Family       = CustomFamily<counter_t<value_type> >; ///< Legacy alias for backward compatibility.

  private:
    storage_type value;
    value_type   snapshot_value { 0 };

    friend counter_t<value_type&>; ///< Grant access to internals so the reference form can bind to the owning form.

  public:

    // --- Owning constructor (counter_t<value_type>) -----------------------------

    /// @brief Constructs an owning counter initialized to zero.
    /// @param labels Per-metric dimensional labels (copied and owned).
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    explicit counter_t(const labels_t& labels)
      : Metric(labels), value(0) {}

    // --- SimpleAPI: easy to use from the user's side, non-trivial internally.
    // --- Reference constructors (counter_t<value_type&>) ------------------------

    /// @brief Constructs a reference counter that binds to an existing owning counter.
    /// @param other Owning counter whose atomic value and labels are referenced.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    counter_t(counter_t<value_type>& other)
      // counter_t<value_type>& -> counter_t<value_type&>
      : Metric(other.labels_ptr), value(other.value), snapshot_value(other.snapshot_value) {}

    /// @brief Constructs a reference counter by adding an owning counter to the given family.
    /// The metric value type compatibility with the family is checked at runtime.
    /// @param family Family to add the owning counter to.
    /// @param labels Per-metric dimensional labels.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    counter_t(family_t& family, const labels_t& labels = {})
      // family_t::Add<>() -> Family::Add<>() -> counter_t<value_type>& -> counter_t<value_type&>
      : counter_t(family.Add<counter_t<value_type> >(labels)) {}

    /// @brief Constructs a reference counter by adding an owning counter to the given family.
    /// The metric value type compatibility with the family is enforced at compile time.
    /// @param family Family to add the owning counter to.
    /// @param labels Per-metric dimensional labels.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    counter_t(custom_family_t<counter_t<value_type> >& family, const labels_t& labels = {})
      // custom_family_t<>::Add() -> CustomFamily<>::Add() -> counter_t<value_type>& -> counter_t<value_type&>
      : counter_t(family.Add (labels)) {}

    /// @brief Constructs a reference counter, creating both family and metric in the given registry.
    /// @param registry Registry to register the family in.
    /// @param name     Metric family name.
    /// @param help     Help/description string.
    /// @param labels   Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    counter_t(Registry& registry, const std::string& name, const std::string& help, const labels_t& labels = {})
      // registry::Add() -> Family::Add<counter_t<value_type>>() -> counter_t<value_type>& -> counter_t<value_type&>
      : counter_t(registry.Add(name, help).Add<counter_t<value_type> >(labels)) {}

    /// @brief Constructs a reference counter, creating both family and metric in the given registry.
    /// @param registry Shared pointer to Registry to register the family in.
    /// @param name     Metric family name.
    /// @param help     Help/description string.
    /// @param labels   Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    counter_t(std::shared_ptr<registry_t>& registry, const std::string& name, const std::string& help, const labels_t& labels = {})
      // registry::Add() -> Family::Add<counter_t<value_type>>() -> counter_t<value_type>& -> counter_t<value_type&>
      : counter_t(registry->Add(name, help).Add<counter_t<value_type> >(labels)) {}

    /// @brief Constructs a reference counter using the global registry.
    /// @param name   Metric family name.
    /// @param help   Help/description string.
    /// @param labels Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    counter_t(const std::string& name, const std::string& help, const labels_t& labels = {})
      // global_registry::Add() -> Family::Add<counter_t<value_type>>() -> counter_t<value_type>& -> counter_t<value_type&>
      : counter_t(global_registry.Add(name, help).Add<counter_t<value_type> >(labels)) {}

    // --- Conversion: owning → reference -----------------------------------------

    /// @brief Implicit conversion from an owning counter to a reference counter.
    /// @return A reference counter bound to this owning counter.
    //template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    //operator counter_t<value_type&>() {
    //  return counter_t<value_type&>(*this);
    //}

    // --- Non-copyable (owning form only) ----------------------------------------

    /// @brief Owning counters are non-copyable to prevent accidental duplication.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    counter_t(const counter_t&) = delete;

    /// @brief Owning counters are non-copy-assignable.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    counter_t& operator=(const counter_t&) = delete;

    // --- Public API (shared by both owning and reference forms) -----------------

    /// @brief Increments the counter by one.
    void Increment() { ++value; }

    /// @brief Increments the counter by the given non-negative value.
    ///
    /// In debug builds, throws if `val` is negative (for signed integral types).
    /// Zero and negative values are silently ignored in release builds.
    ///
    /// @param val Amount to add (must be > 0).
    /// @throws std::invalid_argument (debug only) if val is negative and integral.
    void Increment(const value_type& val) {
      #ifndef NDEBUG
      if (std::is_integral<value_type>::value && val < 0)
        throw std::invalid_argument("counter increment must be non-negative");
      #endif
      if (val > 0)
        value += val;
    }

    /// @brief Returns the current counter value.
    /// @return Atomically loaded counter value.
    value_type Get() const { return value.load(); }

    /// @brief Pre-increment operator (increments by one).
    /// @return Reference to this counter.
    counter_t& operator++()                    { ++value;      return *this; }

    /// @brief Post-increment operator (increments by one, returns reference — not previous value).
    /// @return Reference to this counter.
    counter_t& operator++(int)                 { ++value;      return *this; }

    /// @brief Compound addition operator.
    /// @param v Value to add.
    /// @return Reference to this counter.
    counter_t& operator+=(const value_type& v) { Increment(v); return *this; }

    // --- Metric interface overrides ---------------------------------------------

    /// @brief Returns the Prometheus type name for this metric.
    /// @return "counter".
    const char* type_name() const override { return "counter"; }

    /// @brief Freezes the current value into a snapshot for consistent serialization.
    void collect() override { snapshot_value = value.load(); }

    /// @brief Writes this counter's data line in the Prometheus text exposition format.
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

  /// @brief Generic counter alias with configurable value type.
  template <typename MetricType = uint64_t>
  using Counter = counter_t<MetricType>;

  /// @brief Builder alias for the default counter type (legacy from prometheus-cpp, should by double).
  using BuildCounter = Builder<counter_t<double>>;

  /// @brief Zero-copy reference handle to a default counter (SimpleAPI).
  using counter_metric_t = typename modify_t<counter_t<>>::metric_ref;

  /// @brief Typed family alias for default counters (SimpleAPI).
  using counter_family_t = custom_family_t<counter_t<>>;

} // namespace prometheus
