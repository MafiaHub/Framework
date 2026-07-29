
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
#include <limits>

namespace prometheus {

  // =============================================================================
  // BucketBoundaries — sorted list of upper-bound values for histogram buckets
  // =============================================================================

  /// @brief Ordered list of upper-bound values that define histogram buckets.
  ///
  /// Values must be strictly increasing and must not contain NaN.
  /// An implicit +Inf bucket is always appended automatically by histogram_t.
  using BucketBoundaries = std::vector<double>;

  // =============================================================================
  // HistogramBucket — single histogram bucket (shared across all histogram_t<> instantiations)
  // =============================================================================

  /// @brief Single histogram bucket with an upper bound and a cumulative count.
  ///
  /// Defined outside histogram_t so that histogram_t<double>::Bucket and
  /// histogram_t<double&>::Bucket are the same type, allowing the reference
  /// form to bind to the owning form's std::vector<HistogramBucket>.
  struct HistogramBucket {
    double                upper_bound;
    std::atomic<uint64_t> cumulative_count{0};

    HistogramBucket() : upper_bound(0) {}
    explicit HistogramBucket(double ub) : upper_bound(ub) {}
    HistogramBucket(HistogramBucket&& other) noexcept
      : upper_bound(other.upper_bound)
      , cumulative_count(other.cumulative_count.load()) {}
  };

  // =============================================================================
  // HistogramBucketSnapshot — frozen copy of a single bucket for serialization
  // =============================================================================

  /// @brief Frozen copy of a single bucket for consistent serialization.
  struct HistogramBucketSnapshot {
    double   upper_bound      = 0.0;
    uint64_t cumulative_count = 0;
  };

  // =============================================================================
  // histogram_t — Prometheus histogram metric
  //
  // A histogram samples observations (e.g. request durations, response sizes)
  // and counts them in configurable buckets.  It also provides a sum of all
  // observed values and the total observation count.
  //
  // Supports two ownership modes controlled by MetricValue:
  //
  //   histogram_t<double>   — owning:    holds its own buckets, count, and sum.
  //   histogram_t<double&>  — reference: binds to the storage of an owning histogram.
  //
  // The reference form enables zero-copy metric handles (see modify_t<>).
  // =============================================================================

  /// @brief Prometheus histogram metric — samples observations into buckets.
  ///
  /// @tparam MetricValue Value type. Use a plain type (e.g. `double`) for an
  ///         owning histogram, or a reference type (e.g. `double&`) for a
  ///         zero-copy reference handle.
  template <typename MetricValue = double>
  class histogram_t : public Metric {

  public:
    using value_type       = typename atomic_storage<MetricValue>::value_type;
    using BucketBoundaries = prometheus::BucketBoundaries; ///< Re-exported for backward compatibility.
    using Family           = CustomFamily<histogram_t<value_type> >; ///< Legacy alias for backward compatibility.
    using Bucket           = HistogramBucket;         ///< Re-exported for backward compatibility.
    using BucketSnapshot   = HistogramBucketSnapshot; ///< Re-exported for backward compatibility.

    /// @brief Returns the default bucket boundaries recommended by Prometheus.
    /// @return Vector of default upper-bound values.
    static BucketBoundaries DefaultBoundaries() {
      return { 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0 };
    }

  private:

    // Storage types depend on whether this is an owning or reference histogram.
    // Owning form holds vectors/atomics directly; reference form holds references.

    using buckets_storage_type = std::conditional_t<
      std::is_reference<MetricValue>::value,
      std::vector<Bucket>&,
      std::vector<Bucket>>;

    using count_storage_type = std::conditional_t<
      std::is_reference<MetricValue>::value,
      std::atomic<uint64_t>&,
      std::atomic<uint64_t>>;

    using sum_storage_type = typename atomic_storage<MetricValue>::storage_type;

    // --- Live data (shared between owning and reference forms) ------------------

    buckets_storage_type buckets;
    count_storage_type   sample_count;
    sum_storage_type     sample_sum;

    // --- Snapshot (always owning, used for consistent serialization) ------------

    std::vector<BucketSnapshot> snapshot_buckets {};
    uint64_t                    snapshot_count = 0;
    value_type                  snapshot_sum   = 0;

    /// Grant access to internals so the reference form can bind to the owning form.
    friend histogram_t<value_type&>;

  public:
    using Value = value_type;

    // --- Owning constructor (histogram_t<value_type>) ---------------------------

    /// @brief Constructs an owning histogram with the given bucket boundaries.
    ///
    /// An implicit +Inf bucket is always appended as the last bucket.
    /// Boundaries must be strictly increasing and must not contain NaN.
    ///
    /// @param labels     Per-metric dimensional labels (copied and owned).
    /// @param boundaries Sorted upper-bound values for the buckets.
    /// @throws std::invalid_argument if boundaries are not strictly increasing or contain NaN.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    explicit histogram_t(const labels_t& labels, const BucketBoundaries& boundaries = DefaultBoundaries())
      : Metric(labels), sample_count(0), sample_sum(0) {
      // Validate that boundaries are strictly increasing.
      for (size_t i = 1; i < boundaries.size(); ++i)
        if (boundaries[i] <= boundaries[i - 1])
          throw std::invalid_argument("Bucket boundaries must be strictly increasing");

      buckets.reserve(boundaries.size() + 1);
      for (double b : boundaries) {
        if (std::isnan(b))
          throw std::invalid_argument("Bucket boundary must not be NaN");
        buckets.emplace_back(b);
      }
      // Always add the implicit +Inf bucket.
      buckets.emplace_back(std::numeric_limits<double>::infinity());
    }

    // --- SimpleAPI: easy to use from the user's side, non-trivial internally.
    // --- Reference constructors (histogram_t<value_type&>) ----------------------

    /// @brief Constructs a reference histogram that binds to an existing owning histogram.
    /// @param other Owning histogram whose buckets, count, and sum are referenced.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    histogram_t(histogram_t<value_type>& other)
      // histogram_t<value_type>& -> histogram_t<value_type&>
      : Metric(other.labels_ptr)
      , buckets(other.buckets)
      , sample_count(other.sample_count)
      , sample_sum(other.sample_sum) {}

    /// @brief Constructs a reference histogram by adding an owning histogram to the given family.
    /// The metric value type compatibility with the family is checked at runtime.
    /// @param family     Family to add the owning histogram to.
    /// @param labels     Per-metric dimensional labels.
    /// @param boundaries Sorted upper-bound values for the buckets.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    histogram_t(family_t& family, const labels_t& labels = {}, const BucketBoundaries& boundaries = DefaultBoundaries())
      // family_t::Add<>() -> Family::Add<>() -> histogram_t<value_type>& -> histogram_t<value_type&>
      : histogram_t(family.Add<histogram_t<value_type> >(labels, boundaries)) {}

    /// @brief Constructs a reference histogram by adding an owning histogram to the given typed family.
    /// The metric value type compatibility with the family is enforced at compile time.
    /// @param family     Family to add the owning histogram to.
    /// @param labels     Per-metric dimensional labels.
    /// @param boundaries Sorted upper-bound values for the buckets.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    histogram_t(custom_family_t<histogram_t<value_type> >& family, const labels_t& labels = {}, const BucketBoundaries& boundaries = DefaultBoundaries())
      // custom_family_t<>::Add() -> CustomFamily<>::Add() -> histogram_t<value_type>& -> histogram_t<value_type&>
      : histogram_t(family.Add(labels, boundaries)) {}

    /// @brief Constructs a reference histogram, creating both family and metric in the given registry.
    /// @param registry   Registry to register the family in.
    /// @param name       Metric family name.
    /// @param help       Help/description string.
    /// @param labels     Constant base labels for the family.
    /// @param boundaries Sorted upper-bound values for the buckets.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    histogram_t(Registry& registry, const std::string& name, const std::string& help,
                const labels_t& labels = {}, const BucketBoundaries& boundaries = DefaultBoundaries())
      // registry::Add() -> Family::Add<histogram_t<value_type>>() -> histogram_t<value_type>& -> histogram_t<value_type&>
      : histogram_t(registry.Add(name, help).Add<histogram_t<value_type>>(labels, boundaries)) {}

    /// @brief Constructs a reference histogram, creating both family and metric in the given registry.
    /// @param registry   Shared pointer to Registry to register the family in.
    /// @param name       Metric family name.
    /// @param help       Help/description string.
    /// @param labels     Constant base labels for the family.
    /// @param boundaries Sorted upper-bound values for the buckets.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    histogram_t(std::shared_ptr<registry_t>& registry, const std::string& name, const std::string& help,
                const labels_t& labels = {}, const BucketBoundaries& boundaries = DefaultBoundaries())
      // registry::Add() -> Family::Add<histogram_t<value_type>>() -> histogram_t<value_type>& -> histogram_t<value_type&>
      : histogram_t(registry->Add(name, help).Add<histogram_t<value_type>>(labels, boundaries)) {}

    /// @brief Constructs a reference histogram using the global registry.
    /// @param name       Metric family name.
    /// @param help       Help/description string.
    /// @param labels     Constant base labels for the family.
    /// @param boundaries Sorted upper-bound values for the buckets.
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    histogram_t(const std::string& name, const std::string& help,
                const labels_t& labels = {}, const BucketBoundaries& boundaries = DefaultBoundaries())
      // global_registry::Add() -> Family::Add<histogram_t<value_type>>() -> histogram_t<value_type>& -> histogram_t<value_type&>
      : histogram_t(global_registry.Add(name, help).Add<histogram_t<value_type>>(labels, boundaries)) {}

    // --- Conversion: owning → reference -----------------------------------------

    /// @brief Implicit conversion from an owning histogram to a reference histogram.
    /// @return A reference histogram bound to this owning histogram.
    //template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    //operator histogram_t<value_type&>() {
    //  return histogram_t<value_type&>(*this);
    //}

    // --- Non-copyable (owning form only) ----------------------------------------

    /// @brief Owning histograms are non-copyable to prevent accidental duplication.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    histogram_t(const histogram_t&) = delete;

    /// @brief Owning histograms are non-copy-assignable.
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    histogram_t& operator=(const histogram_t&) = delete;

    // --- Public API (shared by both owning and reference forms) -----------------

    /// @brief Records an observation.
    ///
    /// Finds the first bucket whose upper bound is >= val, then increments
    /// that bucket and all subsequent buckets (cumulative counts).
    /// Also adds val to the running sum and increments the total count.
    ///
    /// @param val Observed value.
    void Observe(value_type val) {
      double dval = static_cast<double>(val);

      // Find the first bucket whose upper bound covers this value.
      size_t i = 0;
      for (; i < buckets.size(); ++i)
        if (dval <= buckets[i].upper_bound)
          break;

      // Increment all buckets from the found one to the end (cumulative).
      for (; i < buckets.size(); ++i)
        ++buckets[i].cumulative_count;

      sample_sum += val;
      ++sample_count;
    }

    /// @brief Returns the total number of observations.
    /// @return Atomically loaded observation count.
    uint64_t GetCount() const { return sample_count.load(); }

    /// @brief Returns the sum of all observed values.
    /// @return Atomically loaded sum.
    value_type GetSum() const { return sample_sum.load(); }

    // --- Metric interface overrides ---------------------------------------------

    /// @brief Returns the Prometheus type name for this metric.
    /// @return "histogram".
    const char* type_name() const override { return "histogram"; }

    /// @brief Freezes the current values into snapshots for consistent serialization.
    ///
    /// Copies the current count, sum, and all bucket cumulative counts into
    /// local snapshot fields so that serialization reads a consistent set.
    void collect() override {
      snapshot_count = sample_count.load();
      snapshot_sum   = sample_sum.load();
      snapshot_buckets.resize(buckets.size());
      for (size_t i = 0; i < buckets.size(); ++i) {
        snapshot_buckets[i].upper_bound     = buckets[i].upper_bound;
        snapshot_buckets[i].cumulative_count = buckets[i].cumulative_count.load();
      }
    }

    /// @brief Writes this histogram's data lines in the Prometheus text exposition format.
    ///
    /// Emits one `_bucket{le="..."}` line per bucket, followed by `_count` and `_sum` lines.
    ///
    /// @param out         Output stream.
    /// @param family_name Metric family name (line prefix).
    /// @param base_labels Constant labels from the owning family.
    void serialize(std::ostream& out, const std::string& family_name,
                   const labels_t& base_labels) const override {
      // Write one line per bucket with the "le" (less-or-equal) extra label.
      for (size_t i = 0; i < snapshot_buckets.size(); ++i) {
        const BucketSnapshot& b = snapshot_buckets[i];

        // Format the upper bound: "+Inf" for infinity, numeric otherwise.
        std::ostringstream bound_oss;
        if (std::isinf(b.upper_bound))
          bound_oss << "+Inf";
        else
          TextSerializer::WriteValue(bound_oss, b.upper_bound);
        std::string bound_str = bound_oss.str();

        TextSerializer::WriteLine(out, family_name, base_labels, this->get_labels(),
                                  b.cumulative_count, "_bucket", "le", bound_str);
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

  /// @brief Generic histogram alias with configurable value type.
  template <typename T = double>
  using Histogram = histogram_t<T>;

  /// @brief Fluent builder alias for the default histogram type.
  using BuildHistogram = Builder<histogram_t<double>>;

  /// @brief Zero-copy reference handle to a default histogram (SimpleAPI).
  using histogram_metric_t = typename modify_t<histogram_t<>>::metric_ref;

  /// @brief Typed family alias for default histograms (SimpleAPI).
  using histogram_family_t = custom_family_t<histogram_t<>>;

} // namespace prometheus