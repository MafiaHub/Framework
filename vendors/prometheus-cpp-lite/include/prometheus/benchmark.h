/*
* prometheus-cpp-lite — header-only C++ library for exposing Prometheus metrics
* https://github.com/biaks/prometheus-cpp-lite
*
* Copyright (c) 2026 Yan Kryukov ianiskr@gmail.com
* Licensed under the MIT License
*/

#pragma once

#include "prometheus/core.h"

#include <chrono>

namespace prometheus {

  // =============================================================================
  // benchmark_t — Prometheus-compatible metric for measuring elapsed time
  //
  // Accumulates wall-clock time between start()/stop() pairs into an atomic
  // double value (seconds).  Exposes the result as a Prometheus "counter"
  // (monotonically increasing elapsed time).
  //
  // Supports two ownership modes controlled by MetricValue:
  //
  //   benchmark_t<double>   — owning:    holds its own std::atomic<double>.
  //   benchmark_t<double&>  — reference: binds to the atomic of an owning benchmark.
  //
  // The reference form enables zero-copy metric handles (see modify_t<>).
  //
  // Note: start_point and the running flag are local to each instance and are
  // NOT shared between owning and reference forms.  Only the accumulated
  // elapsed_seconds value is shared.
  // =============================================================================

  /// @brief Prometheus-compatible metric for measuring elapsed wall-clock time.
  ///
  /// @tparam MetricValue Value type. Use a plain type (e.g. `double`) for an
  ///         owning benchmark, or a reference type (e.g. `double&`) for a
  ///         zero-copy reference handle.
  template <typename MetricValue = double>
  class benchmark_t : public Metric {

  public:
    using storage_type = typename atomic_storage<MetricValue>::storage_type;
    using value_type   = typename atomic_storage<MetricValue>::value_type;
    using Family       = CustomFamily<benchmark_t<value_type> >; ///< Legacy alias for backward compatibility.

  private:
    using duration_t   = std::chrono::high_resolution_clock::duration;
    using time_point_t = std::chrono::time_point<std::chrono::high_resolution_clock>;

    /// Shared atomic accumulator storing total elapsed seconds.
    storage_type elapsed_seconds;

    /// Local measurement state (always owning, never shared between instances).
    time_point_t start_point;
    #ifndef NDEBUG
    bool         running = false;
    #endif

    /// Frozen snapshot for consistent serialization.
    value_type snapshot_value { 0 };

    /// Grant access to internals so the reference form can bind to the owning form.
    friend benchmark_t<value_type&>;

  public:
    using Value = value_type;

    // --- Owning constructor (benchmark_t<value_type>) ---------------------------

    /// @brief Constructs an owning benchmark initialized to zero elapsed seconds.
    /// @param labels Per-metric dimensional labels (copied and owned).
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    explicit benchmark_t(const labels_t& labels)
      : Metric(labels), elapsed_seconds(0) {}

    // --- SimpleAPI: easy to use from the user's side, non-trivial internally.
    // --- Reference constructors (benchmark_t<value_type&>) ----------------------

    /// @brief Constructs a reference benchmark that binds to an existing owning benchmark.
    /// @param other Owning benchmark whose atomic elapsed_seconds and labels are referenced.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    benchmark_t(benchmark_t<value_type>& other)
      // benchmark_t<value_type>& -> benchmark_t<value_type&>
      : Metric(other.labels_ptr), elapsed_seconds(other.elapsed_seconds), snapshot_value(other.snapshot_value) {}

    /// @brief Constructs a reference benchmark by adding an owning benchmark to the given family.
    /// The metric value type compatibility with the family is checked at runtime.
    /// @param family Family to add the owning benchmark to.
    /// @param labels Per-metric dimensional labels.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    benchmark_t(family_t& family, const labels_t& labels = {})
      // family_t::Add<>() -> Family::Add<>() -> benchmark_t<value_type>& -> benchmark_t<value_type&>
      : benchmark_t(family.Add<benchmark_t<value_type> >(labels)) {}

    /// @brief Constructs a reference benchmark by adding an owning benchmark to the given typed family.
    /// The metric value type compatibility with the family is enforced at compile time.
    /// @param family Family to add the owning benchmark to.
    /// @param labels Per-metric dimensional labels.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    benchmark_t(custom_family_t<benchmark_t<value_type> >& family, const labels_t& labels = {})
      // custom_family_t<>::Add() -> CustomFamily<>::Add() -> benchmark_t<value_type>& -> benchmark_t<value_type&>
      : benchmark_t(family.Add(labels)) {}

    /// @brief Constructs a reference benchmark, creating both family and metric in the given registry.
    /// @param registry Registry to register the family in.
    /// @param name     Metric family name.
    /// @param help     Help/description string.
    /// @param labels   Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    benchmark_t(Registry& registry, const std::string& name, const std::string& help, const labels_t& labels = {})
      // registry::Add() -> Family::Add<benchmark_t<value_type>>() -> benchmark_t<value_type>& -> benchmark_t<value_type&>
      : benchmark_t(registry.Add(name, help, labels).Add<benchmark_t<value_type> >({})) {}

    /// @brief Constructs a reference benchmark, creating both family and metric in the given registry.
    /// @param registry Shared pointer to Registry to register the family in.
    /// @param name     Metric family name.
    /// @param help     Help/description string.
    /// @param labels   Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    benchmark_t(std::shared_ptr<registry_t>& registry, const std::string& name, const std::string& help, const labels_t& labels = {})
      // registry::Add() -> Family::Add<benchmark_t<value_type>>() -> benchmark_t<value_type>& -> benchmark_t<value_type&>
      : benchmark_t(registry->Add(name, help, labels).Add<benchmark_t<value_type> >({})) {}

    /// @brief Constructs a reference benchmark using the global registry.
    /// @param name   Metric family name.
    /// @param help   Help/description string.
    /// @param labels Constant base labels for the family.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    benchmark_t(const std::string& name, const std::string& help, const labels_t& labels = {})
      // global_registry::Add() -> Family::Add<benchmark_t<value_type>>() -> benchmark_t<value_type>& -> benchmark_t<value_type&>
      : benchmark_t(global_registry.Add(name, help, labels).Add<benchmark_t<value_type>>({})) {}

    // --- Conversion: owning → reference -----------------------------------------

    /// @brief Implicit conversion from an owning benchmark to a reference benchmark.
    /// @return A reference benchmark bound to this owning benchmark.
    //template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    //operator benchmark_t<value_type&>() {
    //  return benchmark_t<value_type&>(*this);
    //}

    // --- Non-copyable (owning form only) ----------------------------------------

    /// @brief Owning benchmarks are non-copyable to prevent accidental duplication.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    benchmark_t(const benchmark_t&) = delete;

    /// @brief Owning benchmarks are non-copy-assignable.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    benchmark_t& operator=(const benchmark_t&) = delete;

    // --- Public API (shared by both owning and reference forms) -----------------

    /// @brief Starts a time measurement.
    ///
    /// Records the current high-resolution clock time point.
    /// In debug builds, throws if a measurement is already in progress.
    ///
    /// @throws std::runtime_error (debug only) if called while already running.
    void start() {
      #ifndef NDEBUG
      if (running)
        throw std::runtime_error("try to start already started benchmark");
      running = true;
      #endif
      start_point = std::chrono::high_resolution_clock::now();
    }

    /// @brief Stops the current time measurement and adds the elapsed time to the accumulator.
    ///
    /// Computes the duration since the last start() call and atomically adds it
    /// (in seconds) to the shared elapsed_seconds value using a CAS loop.
    /// In debug builds, throws if no measurement is in progress.
    ///
    /// @throws std::runtime_error (debug only) if called while not running.
    void stop() {
      #ifndef NDEBUG
      if (!running)
        throw std::runtime_error("try to stop already stopped benchmark");
      running = false;
      #endif
      auto   now   = std::chrono::high_resolution_clock::now();
      double delta = std::chrono::duration_cast<std::chrono::duration<double>>(
        now - start_point).count();

      // Atomic add via compare-and-swap loop (std::atomic<double> may lack fetch_add).
      value_type current = elapsed_seconds.load();
      while (!elapsed_seconds.compare_exchange_weak(
        current, current + static_cast<value_type>(delta),
        std::memory_order_release, std::memory_order_relaxed))
        ;
    }

    /// @brief Returns the total elapsed time accumulated so far (in seconds).
    /// @return Atomically loaded elapsed seconds.
    value_type Get() const { return elapsed_seconds.load(); }

    // --- Metric interface overrides ---------------------------------------------

    /// @brief Returns the Prometheus type name for this metric.
    ///
    /// Exposed as "counter" because elapsed time is monotonically increasing.
    ///
    /// @return "counter".
    const char* type_name() const override { return "counter"; }

    /// @brief Freezes the current value into a snapshot for consistent serialization.
    void collect() override {
      snapshot_value = elapsed_seconds.load();
    }

    /// @brief Writes this benchmark's data line in the Prometheus text exposition format.
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

  /// @brief Generic benchmark alias with configurable value type.
  template <typename T = double>
  using Benchmark = benchmark_t<T>;

  /// @brief Fluent builder alias for the default benchmark type.
  using BuildBenchmark = Builder<benchmark_t<double>>;

  /// @brief Zero-copy reference handle to a default benchmark (SimpleAPI).
  using benchmark_metric_t = typename modify_t<benchmark_t<>>::metric_ref;

  /// @brief Typed family alias for default benchmarks (SimpleAPI).
  using benchmark_family_t = custom_family_t<benchmark_t<>>;

} // namespace prometheus