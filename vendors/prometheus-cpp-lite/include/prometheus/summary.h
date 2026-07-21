/*
* prometheus-cpp-lite — header-only C++ library for exposing Prometheus metrics
* https://github.com/biaks/prometheus-cpp-lite
*
* Copyright (c) 2026 Yan Kryukov ianiskr@gmail.com
* Licensed under the MIT License
*/

#pragma once

#include "prometheus/core.h"

#include <vector>
#include <algorithm>

namespace prometheus {

  // =============================================================================
  // SummaryQuantile / SummaryQuantiles — quantile definitions for summary metrics
  // =============================================================================

  /// @brief Definition of a single quantile target with an allowed error.
  struct SummaryQuantile {
    double quantile;  ///< Target quantile, e.g. 0.5, 0.9, 0.99.
    double error;     ///< Allowed error, e.g. 0.05, 0.01, 0.001.
  };

  /// @brief Ordered list of quantile/error pairs that define summary targets.
  using SummaryQuantiles = std::vector<SummaryQuantile>;

  // =============================================================================
  // SummaryLiveData<T> — thread-safe observation buffer (shared across instantiations)
  //
  // Defined outside summary_t so that summary_t<double>::LiveData and
  // summary_t<double&>::LiveData are the same type, allowing the reference
  // form to bind to the owning form's buffer.
  // =============================================================================

  /// @brief Thread-safe observation buffer holding raw samples, sum, and count.
  ///
  /// @tparam T Value type for observations and sum (typically `double`).
  template <typename T>
  struct SummaryLiveData {
    std::vector<T> observations;
    T              sample_sum   = 0;
    uint64_t       sample_count = 0;
    mutable std::mutex mutex;

    /// @brief Records a single observation.
    /// @param val Observed value.
    void Observe(T val) {
      std::lock_guard<std::mutex> lock(mutex);
      observations.push_back(val);
      sample_sum += val;
      ++sample_count;
    }

    /// @brief Takes a consistent snapshot of all current data under the mutex.
    /// @param out_obs   Output: copy of the observation buffer.
    /// @param out_sum   Output: current sum.
    /// @param out_count Output: current count.
    void Snapshot(std::vector<T>& out_obs, T& out_sum, uint64_t& out_count) {
      std::lock_guard<std::mutex> lock(mutex);
      out_obs   = observations;
      out_sum   = sample_sum;
      out_count = sample_count;
    }

    /// @brief Resets all observations (used for sliding-window semantics).
    void Reset() {
      std::lock_guard<std::mutex> lock(mutex);
      observations.clear();
      sample_sum   = 0;
      sample_count = 0;
    }
  };

  // =============================================================================
  // SummaryQuantileSnapshot — frozen quantile value for serialization
  // =============================================================================

  /// @brief Frozen quantile value for consistent serialization.
  ///
  /// @tparam T Value type (typically `double`).
  template <typename T>
  struct SummaryQuantileSnapshot {
    double quantile = 0.0;
    T      value    = 0;
  };

  // =============================================================================
  // summary_t — Prometheus summary metric
  //
  // A summary computes configurable quantiles over a sliding window of
  // observations.  Uses a simplified approach: all observations are stored
  // in a buffer, sorted at collect() time, and quantiles are computed via
  // linear interpolation.
  //
  // For production workloads a full CKMS or t-digest algorithm is recommended.
  //
  // Supports two ownership modes controlled by MetricValue:
  //
  //   summary_t<double>   — owning:    holds its own observation buffer.
  //   summary_t<double&>  — reference: binds to the buffer of an owning summary.
  //
  // The reference form enables zero-copy metric handles (see modify_t<>).
  // =============================================================================

  /// @brief Prometheus summary metric — computes quantiles over observations.
  ///
  /// @tparam MetricValue Value type. Use a plain type (e.g. `double`) for an
  ///         owning summary, or a reference type (e.g. `double&`) for a
  ///         zero-copy reference handle.
  template <typename MetricValue = double>
  class summary_t : public Metric {

  public:
    using value_type       = typename atomic_storage<MetricValue>::value_type;
    using Family           = CustomFamily<summary_t<value_type> >; ///< Legacy alias for backward compatibility.
    using Quantile         = SummaryQuantile;          ///< Re-exported for backward compatibility.
    using Quantiles        = SummaryQuantiles;         ///< Re-exported for backward compatibility.
    using LiveData         = SummaryLiveData<value_type>;          ///< Re-exported for backward compatibility.
    using QuantileSnapshot = SummaryQuantileSnapshot<value_type>;  ///< Re-exported for backward compatibility.

    /// @brief Returns the default quantile definitions (p50, p90, p95, p99).
    /// @return Vector of default quantile/error pairs.
    static Quantiles DefaultQuantiles() {
      return {
        {0.5,  0.05},
        {0.9,  0.01},
        {0.95, 0.005},
        {0.99, 0.001}
      };
    }

  private:

    // Owning form holds LiveData directly; reference form holds a reference.
    using live_data_storage_type = std::conditional_t<
      std::is_reference<MetricValue>::value,
      LiveData&,
      LiveData>;

    live_data_storage_type live_data;
    Quantiles              quantile_defs;

    // --- Snapshot (always owning, used for consistent serialization) ------------

    std::vector<QuantileSnapshot> snapshot_quantiles {};
    uint64_t                      snapshot_count = 0;
    value_type                    snapshot_sum   = 0;

    /// Maximum number of observations kept in the buffer (sliding window).
    /// When exceeded, the oldest observations are discarded during collect().
    size_t max_observations;

    /// Grant access to internals so the reference form can bind to the owning form.
    friend summary_t<value_type&>;

    // --- Quantile computation helper -------------------------------------------

    /// @brief Computes a quantile from a sorted observation vector using linear interpolation.
    /// @param sorted Sorted vector of observations.
    /// @param q      Target quantile in [0, 1].
    /// @return Interpolated quantile value, or 0 if the vector is empty.
    static value_type ComputeQuantile(const std::vector<value_type>& sorted, double q) {
      if (sorted.empty()) return 0;
      if (q <= 0.0)       return sorted.front();
      if (q >= 1.0)       return sorted.back();

      double idx  = q * static_cast<double>(sorted.size() - 1);
      size_t lo   = static_cast<size_t>(idx);
      size_t hi   = lo + 1;
      double frac = idx - static_cast<double>(lo);

      if (hi >= sorted.size())
        return sorted.back();

      // Linear interpolation between the two surrounding elements.
      return static_cast<value_type>(
        static_cast<double>(sorted[lo]) * (1.0 - frac) +
        static_cast<double>(sorted[hi]) * frac);
    }

  public:
    using Value = value_type;

    // --- Owning constructor (summary_t<value_type>) -----------------------------

    /// @brief Constructs an owning summary with the given quantile definitions.
    ///
    /// @param labels    Per-metric dimensional labels (copied and owned).
    /// @param quantiles Quantile/error pairs to compute.
    /// @param max_obs   Maximum observations to keep in the buffer (sliding window).
    /// @throws std::invalid_argument if any quantile or error is outside [0, 1],
    ///         or if the quantile list is empty.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    explicit summary_t(const labels_t& labels,
                       const Quantiles& quantiles = DefaultQuantiles(),
                       size_t max_obs = 500000)
      : Metric(labels), quantile_defs(quantiles), max_observations(max_obs) {
      for (const Quantile& q : quantile_defs) {
        if (q.quantile < 0.0 || q.quantile > 1.0)
          throw std::invalid_argument("Quantile must be in [0, 1], got: " + std::to_string(q.quantile));
        if (q.error < 0.0 || q.error > 1.0)
          throw std::invalid_argument("Quantile error must be in [0, 1], got: " + std::to_string(q.error));
      }
      if (quantile_defs.empty())
        throw std::invalid_argument("Summary must have at least one quantile definition");
    }

    // --- SimpleAPI: easy to use from the user's side, non-trivial internally.
    // --- Reference constructors (summary_t<value_type&>) ------------------------

    /// @brief Constructs a reference summary that binds to an existing owning summary.
    /// @param other Owning summary whose live data and definitions are referenced.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    summary_t(summary_t<value_type>& other)
      // summary_t<value_type>& -> summary_t<value_type&>
      : Metric(other.labels_ptr)
      , live_data(other.live_data)
      , quantile_defs(other.quantile_defs)
      , max_observations(other.max_observations) {}

    /// @brief Constructs a reference summary by adding an owning summary to the given family.
    /// The metric value type compatibility with the family is checked at runtime.
    /// @param family   Family to add the owning summary to.
    /// @param labels   Per-metric dimensional labels.
    /// @param quantiles Quantile/error pairs to compute.
    /// @param max_obs  Maximum observations to keep in the buffer (sliding window).
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    summary_t(family_t& family, const labels_t& labels = {},
              const Quantiles& quantiles = DefaultQuantiles(), size_t max_obs = 500000)
      // family_t::Add<>() -> Family::Add<>() -> summary_t<value_type>& -> summary_t<value_type&>
      : summary_t(family.Add<summary_t<value_type> >(labels, quantiles, max_obs)) {}

    /// @brief Constructs a reference summary by adding an owning summary to the given typed family.
    /// The metric value type compatibility with the family is enforced at compile time.
    /// @param family   Family to add the owning summary to.
    /// @param labels   Per-metric dimensional labels.
    /// @param quantiles Quantile/error pairs to compute.
    /// @param max_obs  Maximum observations to keep in the buffer (sliding window).
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    summary_t(custom_family_t<summary_t<value_type> >& family, const labels_t& labels = {},
              const Quantiles& quantiles = DefaultQuantiles(), size_t max_obs = 500000)
      // custom_family_t<>::Add() -> CustomFamily<>::Add() -> summary_t<value_type>& -> summary_t<value_type&>
      : summary_t(family.Add(labels, quantiles, max_obs)) {}

    /// @brief Constructs a reference summary, creating both family and metric in the given registry.
    /// @param registry  Registry to register the family in.
    /// @param name      Metric family name.
    /// @param help      Help/description string.
    /// @param labels    Constant base labels for the family.
    /// @param quantiles Quantile/error pairs to compute.
    /// @param max_obs   Maximum observations to keep in the buffer (sliding window).
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    summary_t(Registry& registry, const std::string& name, const std::string& help,
              const labels_t& labels = {}, const Quantiles& quantiles = DefaultQuantiles(),
              size_t max_obs = 500000)
      // registry::Add() -> Family::Add<summary_t<value_type>>() -> summary_t<value_type>& -> summary_t<value_type&>
      : summary_t(registry.Add(name, help).Add<summary_t<value_type>>(labels, quantiles, max_obs)) {}

    /// @brief Constructs a reference summary, creating both family and metric in the given registry.
    /// @param registry  Shared pointer to Registry to register the family in.
    /// @param name      Metric family name.
    /// @param help      Help/description string.
    /// @param labels    Constant base labels for the family.
    /// @param quantiles Quantile/error pairs to compute.
    /// @param max_obs   Maximum observations to keep in the buffer (sliding window).
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    summary_t(std::shared_ptr<registry_t>& registry, const std::string& name, const std::string& help,
              const labels_t& labels = {}, const Quantiles& quantiles = DefaultQuantiles(),
              size_t max_obs = 500000)
      // registry::Add() -> Family::Add<summary_t<value_type>>() -> summary_t<value_type>& -> summary_t<value_type&>
      : summary_t(registry->Add(name, help).Add<summary_t<value_type>>(labels, quantiles, max_obs)) {}

    /// @brief Constructs a reference summary using the global registry.
    /// @param name      Metric family name.
    /// @param help      Help/description string.
    /// @param labels    Constant base labels for the family.
    /// @param quantiles Quantile/error pairs to compute.
    /// @param max_obs   Maximum observations to keep in the buffer (sliding window).
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    summary_t(const std::string& name, const std::string& help,
              const labels_t& labels = {}, const Quantiles& quantiles = DefaultQuantiles(),
              size_t max_obs = 500000)
      // global_registry::Add() -> Family::Add<summary_t<value_type>>() -> summary_t<value_type>& -> summary_t<value_type&>
      : summary_t(global_registry.Add(name, help).Add<summary_t<value_type>>(labels, quantiles, max_obs)) {}

    // --- Conversion: owning → reference -----------------------------------------

    /// @brief Implicit conversion from an owning summary to a reference summary.
    /// @return A reference summary bound to this owning summary.
    //template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    //operator summary_t<value_type&>() {
    //  return summary_t<value_type&>(*this);
    //}

    // --- Non-copyable (owning form only) ----------------------------------------

    /// @brief Owning summaries are non-copyable to prevent accidental duplication.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    summary_t(const summary_t&) = delete;

    /// @brief Owning summaries are non-copy-assignable.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    summary_t& operator=(const summary_t&) = delete;

    // --- Public API (shared by both owning and reference forms) -----------------

    /// @brief Records a single observation.
    /// @param val Observed value.
    void Observe(value_type val) {
      live_data.Observe(val);
    }

    /// @brief Returns the total number of observations recorded so far.
    /// @return Observation count (read under mutex).
    uint64_t GetCount() const {
      std::lock_guard<std::mutex> lock(live_data.mutex);
      return live_data.sample_count;
    }

    /// @brief Returns the sum of all observed values.
    /// @return Observation sum (read under mutex).
    value_type GetSum() const {
      std::lock_guard<std::mutex> lock(live_data.mutex);
      return live_data.sample_sum;
    }

    // --- Metric interface overrides ---------------------------------------------

    /// @brief Returns the Prometheus type name for this metric.
    /// @return "summary".
    const char* type_name() const override { return "summary"; }

    /// @brief Freezes the current values into snapshots for consistent serialization.
    ///
    /// Takes a snapshot of the observation buffer, trims it to max_observations
    /// (sliding window), sorts it, and computes all configured quantiles.
    void collect() override {
      std::vector<value_type> observations;
      value_type              sum;
      uint64_t                count;

      live_data.Snapshot(observations, sum, count);

      snapshot_count = count;
      snapshot_sum   = sum;

      // Sort observations for quantile computation.
      std::sort(observations.begin(), observations.end());

      // Trim the buffer if it exceeds the sliding window size.
      if (observations.size() > max_observations) {
        size_t excess = observations.size() - max_observations;
        observations.erase(observations.begin(),
                           observations.begin() + static_cast<ptrdiff_t>(excess));
      }

      // Compute each configured quantile from the sorted buffer.
      snapshot_quantiles.resize(quantile_defs.size());
      for (size_t i = 0; i < quantile_defs.size(); ++i) {
        snapshot_quantiles[i].quantile = quantile_defs[i].quantile;
        snapshot_quantiles[i].value    = ComputeQuantile(observations, quantile_defs[i].quantile);
      }
    }

    /// @brief Writes this summary's data lines in the Prometheus text exposition format.
    ///
    /// Emits one line per quantile with a "quantile" extra label, followed by
    /// `_count` and `_sum` lines.
    ///
    /// @param out         Output stream.
    /// @param family_name Metric family name (line prefix).
    /// @param base_labels Constant labels from the owning family.
    void serialize(std::ostream& out, const std::string& family_name,
                   const labels_t& base_labels) const override {
      // Write one line per quantile with the "quantile" extra label.
      for (size_t i = 0; i < snapshot_quantiles.size(); ++i) {
        const QuantileSnapshot& q = snapshot_quantiles[i];

        // Format the quantile value as a compact string.
        std::string quantile_str;
        {
          std::array<char, 64> buf;
          int n = std::snprintf(buf.data(), buf.size(), "%g", q.quantile);
          quantile_str.assign(buf.data(), static_cast<size_t>(n));
        }

        TextSerializer::WriteLine(out, family_name, base_labels, this->get_labels(),
                                  q.value, "", "quantile", quantile_str);
      }

      // Write the total count and sum lines.
      TextSerializer::WriteLine(out, family_name, base_labels, this->get_labels(),
                                snapshot_count, "_count");
      TextSerializer::WriteLine(out, family_name, base_labels, this->get_labels(),
                                snapshot_sum, "_sum");
    }
  };

  // =============================================================================
  // Convenience aliases
  // =============================================================================

  /// @brief Generic summary alias with configurable value type.
  template <typename T = double>
  using Summary = summary_t<T>;

  /// @brief Fluent builder alias for the default summary type.
  using BuildSummary = Builder<summary_t<double>>;

  /// @brief Zero-copy reference handle to a default summary (SimpleAPI).
  using summary_metric_t = typename modify_t<summary_t<>>::metric_ref;

  /// @brief Typed family alias for default summaries (SimpleAPI).
  using summary_family_t = custom_family_t<summary_t<>>;

} // namespace prometheus