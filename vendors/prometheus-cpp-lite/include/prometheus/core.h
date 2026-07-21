
/*	
* prometheus-cpp-lite — header-only C++ library for exposing Prometheus metrics
* https://github.com/biaks/prometheus-cpp-lite
* 
* Copyright (c) 2026 Yan Kryukov ianiskr@gmail.com
* Licensed under the MIT License
*/

#pragma once

// =============================================================================
// prometheus/core.h — Core classes for Prometheus metrics library
//
// Class map:
//
//   Registry (registry_t)
//   ├── Stores a set of Family objects, keyed by metric name.
//   ├── Provides serialization of all registered families.
//   └── Thread-safe.
//
//   Family
//   ├── Stores a set of Metric objects sharing the same name, help, and base labels.
//   ├── Each metric is keyed by its own label set.
//   ├── Enforces that all metrics in one family have the same concrete type.
//   └── Thread-safe.
//
//   CustomFamily<MetricType> : Family
//   └── Compile-time typed wrapper around Family.
//       Provides type-safe Add() without explicit template argument.
//
//   Metric (abstract)
//   ├── Base class for all concrete metric types (Counter, Gauge, Histogram, etc.).
//   ├── Supports two ownership modes for labels:
//   │   ├── Owning: copies labels into owned_labels (used when created by Family).
//   │   └── Reference: points to externally-owned labels (zero-copy "ref" metrics).
//   └── Defines interface: collect(), serialize(), type_name().
//
//   TextSerializer
//   └── Utility class for Prometheus text exposition format serialization.
//       Provides static helpers for writing values, labels, and full metric lines.
//       RAII guard that switches stream locale to "C" and restores on destruction.
//
//   Builder<MetricType>
//   └── Fluent builder for registering a CustomFamily in a Registry.
//
//   SimpleAPI wrappers:
//
//   family_t / custom_family_t<MetricType>
//   └── Lightweight wrappers (SimpleAPI) around Family for convenient usage
//       with a global or user-supplied registry.
//
//   atomic_storage<V>
//   └── Type trait: selects between owning std::atomic<T> and
//       reference std::atomic<T>& depending on whether V is a reference type.
//
//   make_ref<MetricTemplate<V>>
//   └── Type trait: maps MetricTemplate<V> → MetricTemplate<V&>,
//       enabling zero-copy "reference" metric handles.
//
// =============================================================================

#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <typeinfo>
#include <typeindex>
#include <stdexcept>
#include <iostream>
#include <mutex>
#include <functional>
#include <sstream>
#include <array>
#include <cmath>
#include <cstdio>

#include "prometheus/atomic_floating.h"

namespace prometheus {

  /// @brief Ordered map of label name-value pairs used throughout the library.
  using labels_t = std::map<std::string, std::string>;

  /// @brief Alias kept for compatibility with prometheus-cpp naming conventions.
  using Labels = labels_t;

} // namespace prometheus

// =============================================================================
// std::hash specialization for labels_t
// Uses boost-style hash combining to produce a hash from all key-value pairs.
// =============================================================================
template<>
struct std::hash<prometheus::labels_t> {
  /// @brief Computes a combined hash over every key-value pair in the label set.
  /// @param labels Label set to hash.
  /// @return Combined hash value.
  inline size_t operator()(const prometheus::labels_t& labels) const noexcept {
    std::hash<std::string> hasher;
    size_t                 hash = 0;
    for (const typename prometheus::labels_t::value_type& pair : labels) {
      hash ^= hasher(pair.first)  + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
      hash ^= hasher(pair.second) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    }
    return hash;
  }
};

namespace prometheus {

  // =============================================================================
  // TextSerializer — RAII serialization context for Prometheus text exposition
  // =============================================================================

  /// @brief RAII helper for writing metrics in the Prometheus text exposition format.
  ///
  /// On construction, switches the stream locale to "C" (locale-independent
  /// numeric formatting). On destruction, restores the original locale.
  /// All static methods can also be used independently of the RAII guard.
  class TextSerializer {
    std::ostream& out;
    std::locale   saved_locale;

  public:

    /// @brief Constructs the serializer, switching the stream to the classic "C" locale.
    /// @param os Output stream to write to.
    explicit TextSerializer(std::ostream& os)
      : out(os), saved_locale(os.getloc()) {
      out.imbue(std::locale::classic());
    }

    /// @brief Restores the original locale on the stream.
    ~TextSerializer() {
      out.imbue(saved_locale);
    }

    // --- Value writers --------------------------------------------------------

    /// @brief Writes a double value, handling NaN and +/-Inf as required by the exposition format.
    /// @param out Output stream.
    /// @param value Value to write.
    static void WriteValue(std::ostream& out, double value) {
      if (std::isnan(value)) {
        out << "NaN";
      } else if (std::isinf(value)) {
        out << (value < 0 ? "-Inf" : "+Inf");
      } else {
        std::array<char, 128> buffer;
        #if __cpp_lib_to_chars >= 201611L
        auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (ec != std::errc())
          throw std::runtime_error("Could not convert double to string: " + std::make_error_code(ec).message());
        out.write(buffer.data(), ptr - buffer.data());
        #else
        int n = std::snprintf(buffer.data(), buffer.size(), "%.*g",
                              std::numeric_limits<double>::max_digits10 - 1, value);
        if (n <= 0 || static_cast<std::size_t>(n) >= buffer.size())
          throw std::runtime_error("Could not convert double to string");
        out.write(buffer.data(), n);
        #endif
      }
    }

    /// @brief Writes an unsigned 64-bit integer value.
    /// @param out Output stream.
    /// @param value Value to write.
    static void WriteValue(std::ostream& out, uint64_t value) {
      out << value;
    }

    /// @brief Writes a signed 64-bit integer value.
    /// @param out Output stream.
    /// @param value Value to write.
    static void WriteValue(std::ostream& out, int64_t value) {
      out << value;
    }

    // --- String / label helpers -----------------------------------------------

    /// @brief Writes a string with characters escaped according to the exposition format.
    /// @param out Output stream.
    /// @param value String to escape and write.
    static void WriteEscapedString(std::ostream& out, const std::string& value) {
      for (char c : value) {
        switch (c) {
          case '\n': out << '\\' << 'n'; break;
          case '\\': out << '\\' << c;   break;
          case '"':  out << '\\' << c;   break;
          default:   out << c;           break;
        }
      }
    }

    /// @brief Writes the label set in Prometheus exposition format: {name="value",...}.
    ///
    /// Merges base labels, per-metric labels, and an optional extra label
    /// (used, for example, for histogram bucket boundaries: le="...").
    /// Writes nothing if all label sources are empty.
    ///
    /// @param out               Output stream.
    /// @param base_labels       Constant labels attached to the whole family.
    /// @param metric_labels     Per-metric dimensional labels.
    /// @param extra_label_name  Optional additional label name (e.g. "le").
    /// @param extra_label_value Optional additional label value (e.g. "0.5").
    static void WriteLabels(std::ostream&      out,
                            const labels_t&    base_labels,
                            const labels_t&    metric_labels,
                            const std::string& extra_label_name  = "",
                            const std::string& extra_label_value = "") {
      bool has_any = !base_labels.empty() || !metric_labels.empty() || !extra_label_name.empty();
      if (!has_any) return;

      out << "{";
      const char* prefix = "";

      for (labels_t::const_iterator it = base_labels.begin(); it != base_labels.end(); ++it) {
        out << prefix << it->first << "=\"";
        WriteEscapedString(out, it->second);
        out << "\"";
        prefix = ",";
      }
      for (labels_t::const_iterator it = metric_labels.begin(); it != metric_labels.end(); ++it) {
        out << prefix << it->first << "=\"";
        WriteEscapedString(out, it->second);
        out << "\"";
        prefix = ",";
      }
      if (!extra_label_name.empty()) {
        out << prefix << extra_label_name << "=\"";
        WriteEscapedString(out, extra_label_value);
        out << "\"";
      }
      out << "}";
    }

    // --- High-level line writer -----------------------------------------------

    /// @brief Writes a single metric line in the Prometheus text exposition format.
    ///
    /// Produces a line of the form:
    ///   name[suffix]{labels} value\n
    ///
    /// @tparam T              Type of the metric value (double, uint64_t, int64_t).
    /// @param out              Output stream.
    /// @param name             Metric family name.
    /// @param base_labels      Constant labels attached to the whole family.
    /// @param metric_labels    Per-metric dimensional labels.
    /// @param value            Metric value to write.
    /// @param suffix           Optional name suffix (e.g. "_total", "_bucket").
    /// @param extra_label_name  Optional additional label name.
    /// @param extra_label_value Optional additional label value.
    template <typename T>
    static void WriteLine(std::ostream&      out,
                          const std::string& name,
                          const labels_t&    base_labels,
                          const labels_t&    metric_labels,
                          const T&           value,
                          const std::string& suffix            = "",
                          const std::string& extra_label_name  = "",
                          const std::string& extra_label_value = "") {
      out << name << suffix;
      WriteLabels(out, base_labels, metric_labels, extra_label_name, extra_label_value);
      out << " ";
      WriteValue(out, value);
      out << "\n";
    }
  };


  // =============================================================================
  // Metric — abstract base class for all concrete metric types
  // =============================================================================

  /// @brief Abstract base class for all Prometheus metric types.
  ///
  /// Supports two label ownership modes:
  /// - **Owning**: labels are copied into `owned_labels`; `labels_ptr` points
  ///   to the internal copy.  Used when the metric is created by a Family.
  /// - **Reference**: labels are not copied; `labels_ptr` points to an
  ///   externally-owned label set.  Used for zero-copy "ref" metrics.
  class Metric {
  protected:
    labels_t        owned_labels; ///< Internal storage for owning mode.
    const labels_t* labels_ptr;   ///< Pointer used for all label access (owning or reference).

  public:

    /// @brief Constructs an owning metric that copies the given labels.
    /// @param labels Labels to copy and own.
    explicit Metric(const labels_t& labels)
      : owned_labels(labels), labels_ptr(&owned_labels) {}

    /// @brief Constructs a reference metric that points to externally-owned labels.
    /// @param labels_ptr Pointer to labels owned elsewhere (must outlive this metric).
    explicit Metric(const labels_t* labels_ptr)
      : owned_labels(), labels_ptr(labels_ptr) {}

    virtual ~Metric() = default;

    /// @brief Returns the label set associated with this metric.
    /// @return Const reference to the labels (owned or external).
    const labels_t& get_labels() const { return *labels_ptr; }

    /// @brief Returns the Prometheus type name string (e.g. "counter", "gauge").
    /// @return Null-terminated type name.
    virtual const char* type_name() const = 0;

    /// @brief Freezes the current metric value so it can be serialized consistently.
    virtual void collect() = 0;

    /// @brief Writes this metric's data lines to the output stream.
    /// @param out         Output stream.
    /// @param family_name The metric family name (used as the line prefix).
    /// @param base_labels Constant labels from the owning family.
    virtual void serialize(std::ostream& out, const std::string& family_name, const labels_t& base_labels) const = 0;
  };


  // =============================================================================
  // Family — container for a set of same-typed metrics sharing name and help
  // =============================================================================

  /// @brief Stores a set of Metric objects that share the same name, help text,
  ///        and base (constant) labels.
  ///
  /// All metrics within a single Family must be of the same concrete type.
  /// Thread-safe: all public methods acquire an internal mutex.
  class Family {

    using Metrics = std::unordered_map<labels_t, std::unique_ptr<Metric>>;

    std::string        name;
    std::string        help;
    labels_t           base_labels;
    Metrics            metrics;
    mutable std::mutex mutex;  ///< Mutable so const methods can lock.
    std::type_index    metric_type = typeid(void);

    /// Reverse lookup: metric pointer → labels key, used by Remove().
    std::unordered_map<Metric*, labels_t> labels_reverse_lookup;

    // --- Name / label validation helpers (locale-independent) -----------------

    /// @brief Checks if the character is an ASCII digit ('0'-'9').
    inline bool isLocaleIndependentDigit(char c) {
      return '0' <= c && c <= '9';
    }

    /// @brief Checks if the character is an ASCII letter or digit.
    inline bool isLocaleIndependentAlphaNumeric(char c) {
      return isLocaleIndependentDigit(c) || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
    }

    /// @brief Checks common prefix rules: non-empty, no leading digit, no "__" prefix.
    inline bool nameStartsValid(const std::string& name) {
      if (name.empty())                           return false;
      if (isLocaleIndependentDigit(name.front()))  return false;
      if (name.compare(0, 2, "__") == 0)           return false;
      return true;
    }

    /// @brief Validates a metric name against the Prometheus data model.
    ///
    /// Allowed pattern: [a-zA-Z_:][a-zA-Z0-9_:]*
    /// @see https://prometheus.io/docs/concepts/data_model/
    /// @param name Metric name to validate.
    /// @return true if the name is valid.
    inline bool CheckMetricName(const std::string& name) {
      if (!nameStartsValid(name))
        return false;
      for (char c : name)
        if (!isLocaleIndependentAlphaNumeric(c) && c != '_' && c != ':')
          return false;
      return true;
    }

    /// @brief Validates a label name against the Prometheus data model.
    ///
    /// Allowed pattern: [a-zA-Z_][a-zA-Z0-9_]*
    /// @see https://prometheus.io/docs/concepts/data_model/
    /// @param name Label name to validate.
    /// @return true if the name is valid.
    inline bool CheckLabelName(const std::string& name) {
      if (!nameStartsValid(name))
        return false;
      for (char c : name)
        if (!isLocaleIndependentAlphaNumeric(c) && c != '_')
          return false;
      return true;
    }

  public:

    /// @brief Constructs a metric family with the given name, help text, and base labels.
    /// @param name_        Metric name (must satisfy Prometheus naming rules).
    /// @param help_        Human-readable help/description string.
    /// @param base_labels_ Constant labels applied to every metric in this family.
    /// @throws std::invalid_argument if the metric name or any label name is invalid.
    Family(const std::string& name_, const std::string& help_, const labels_t& base_labels_ = labels_t())
      : name(name_), help(help_), base_labels(base_labels_) {
      if (!CheckMetricName(name_))
        throw std::invalid_argument("Invalid metric name: '" + name_ + "'");
      for (const labels_t::value_type& label_pair : base_labels_) {
        if (!CheckLabelName(label_pair.first))
          throw std::invalid_argument("Invalid label name: '" + label_pair.first + "'");
      }
    }

    ~Family() = default;

    Family(const Family&) = delete;
    Family& operator=(const Family&) = delete;

    /// @brief Returns the constant (base) labels of this family.
    /// @return Const reference to the base label set.
    const labels_t& get_base_labels() const { return base_labels; }

    /// @brief Creates or retrieves a metric with the given per-metric labels.
    ///
    /// If a metric with identical labels already exists, returns a reference
    /// to the existing one.  All metrics in the family must have the same
    /// concrete MetricType; mixing types causes a runtime exception.
    ///
    /// @tparam MetricType Concrete metric class (must derive from Metric).
    /// @tparam Args       Additional constructor arguments forwarded to MetricType.
    /// @param metric_labels Per-metric dimensional labels.
    /// @param args          Extra arguments forwarded to the MetricType constructor.
    /// @return Reference to the (possibly new) metric instance.
    /// @throws std::runtime_error      if a metric of a different type already exists.
    /// @throws std::invalid_argument   if a label name is invalid or duplicates a base label.
    template <typename MetricType, typename... Args>
    MetricType& Add(const labels_t& metric_labels, Args&&... args) {
      static_assert(std::is_base_of<Metric, MetricType>::value, "MetricType must derive from Metric");
      std::lock_guard<std::mutex> lock(mutex);

      // Ensure all metrics in this family share the same concrete type.
      if (metric_type != typeid(void) && metric_type != typeid(MetricType))
        throw std::runtime_error("family '" + name + "': metric exists with different type");

      // Return existing metric if labels match.
      typename Metrics::iterator it = metrics.find(metric_labels);
      if (it != metrics.end())
        return *static_cast<MetricType*>(it->second.get());

      // Validate label names before creating a new metric.
      for (const labels_t::value_type& label_pair : metric_labels) {
        if (!CheckLabelName(label_pair.first))
          throw std::invalid_argument("Invalid label name: '" + label_pair.first + "'");
        if (base_labels.count(label_pair.first))
          throw std::invalid_argument("Label name '" + label_pair.first + "' already present in constant labels");
      }

      // Record the concrete type on first insertion.
      if (metric_type == typeid(void))
        metric_type = typeid(MetricType);

      MetricType* result = new MetricType (metric_labels, std::forward<Args>(args)...);
      std::unique_ptr<Metric> new_metric(result);
      labels_reverse_lookup.emplace(result, metric_labels);
      metrics.emplace(metric_labels, std::move(new_metric));
      return *result;
    }

    /// @brief Removes a previously added metric from this family.
    ///
    /// Does nothing if the given pointer was not returned by Add().
    ///
    /// @param metric Pointer to the metric to remove.
    void Remove(Metric* metric) {
      std::lock_guard<std::mutex> lock(mutex);
      auto reverse_it = labels_reverse_lookup.find(metric);
      if (reverse_it == labels_reverse_lookup.end())
        return;
      const labels_t& metric_labels = reverse_it->second;
      metrics.erase(metric_labels);
      labels_reverse_lookup.erase(reverse_it);
    }

    /// @brief Checks whether a metric with the given labels exists in this family.
    /// @param metric_labels Labels to look up.
    /// @return true if such a metric is present.
    bool Has(const labels_t& metric_labels) const {
      std::lock_guard<std::mutex> lock(mutex);
      return metrics.find(metric_labels) != metrics.end();
    }

    /// @brief Serializes all metrics in this family to the output stream.
    ///
    /// First calls collect() on every metric to freeze values, then writes
    /// the HELP / TYPE header lines followed by each metric's data lines.
    ///
    /// @param out Output stream to write to.
    void serialize(std::ostream& out) {
      std::lock_guard<std::mutex> lock(mutex);

      if (metrics.empty()) return;

      // Freeze current values so serialization is consistent.
      for (const typename Metrics::value_type& metric : metrics)
        metric.second->collect();

      const char* type_str = metrics.begin()->second->type_name();

      // Write HELP and TYPE header lines.
      if (!help.empty())
        out << "# HELP " << name << " " << help << "\n";
      out << "# TYPE " << name << " " << type_str << "\n";

      #if 1 // Collect pointers sorted by labels.

      std::map<labels_t, Metric*> sorted;
      for (const typename Metrics::value_type& metric : metrics)
        sorted.emplace(metric.first, metric.second.get());

      for (const typename std::map<labels_t, Metric*>::value_type& entry : sorted)
        entry.second->serialize(out, name, base_labels);

      #else // Write in arbitrary order.
      for (const typename Metrics::value_type& metric : metrics)
        metric.second->serialize(out, name, base_labels);
      #endif
    }
  };

  // Forward declaration for CustomFamily::Build().
  class Registry;

  // =============================================================================
  // CustomFamily<MetricType> — compile-time typed wrapper around Family
  // =============================================================================

  /// @brief Compile-time typed wrapper around Family.
  ///
  /// Stores the MetricType at the type level so that Add() does not require
  /// an explicit template argument at the call site.
  ///
  /// @tparam MetricType Concrete metric class (must derive from Metric).
  template <typename MetricType>
  class CustomFamily : public Family {
  public:

    /// @brief Creates or retrieves a metric with the given per-metric labels.
    /// @tparam Args Additional constructor arguments forwarded to MetricType.
    /// @param metric_labels Per-metric dimensional labels.
    /// @param args          Extra arguments forwarded to the MetricType constructor.
    /// @return Reference to the (possibly new) metric instance.
    template <typename... Args>
    MetricType& Add(const labels_t& metric_labels, Args&&... args) {
      return Family::Add<MetricType>(metric_labels, std::forward<Args>(args)...);
    }

    /// @brief Registers (or retrieves) a CustomFamily in the given registry.
    ///
    /// Defined after Registry so that Registry is a complete type.
    ///
    /// @param registry Registry to register in.
    /// @param name     Metric family name.
    /// @param help     Help/description string.
    /// @param labels   Constant base labels.
    /// @return Reference to the registered CustomFamily.
    static CustomFamily<MetricType>& Build(Registry& registry, const std::string& name, const std::string& help, const labels_t& labels = labels_t());
  };

  // =============================================================================
  // Registry — top-level container for metric families
  // =============================================================================

  /// @brief Top-level store for metric families, keyed by metric name.
  ///
  /// Thread-safe: all public methods acquire an internal mutex.
  class Registry {

    using Families = std::unordered_map<std::string, std::unique_ptr<Family>>;

    Families           families;
    mutable std::mutex mutex;  ///< Mutable so const methods can lock.

  public:

    Registry() = default;
    Registry(Registry&& other) : families(std::move(other.families)) {};

    Registry& operator=(Registry&& other) {
      families = std::move(other.families);
      return *this;
    }

    /// @brief Registers a new family or returns an existing one with the same name.
    ///
    /// If a family with the given name already exists, its base labels must match;
    /// otherwise a runtime exception is thrown.  The help text is not compared.
    ///
    /// @param name        Metric family name.
    /// @param help        Help/description string.
    /// @param base_labels Constant labels applied to every metric in this family.
    /// @return Reference to the (possibly new) Family.
    /// @throws std::runtime_error if the family already exists with different base labels.
    Family& Add(const std::string& name, const std::string& help, const labels_t& base_labels = labels_t()) {
      std::lock_guard<std::mutex> lock(mutex);

      typename Families::iterator it = families.find(name);
      if (it != families.end()) {
        if (it->second->get_base_labels() != base_labels)
          throw std::runtime_error("registry: family '" + name + "' already exists with different base_labels");
        return *(it->second.get());
      }

      Family* created = new Family(name, help, base_labels);
      std::unique_ptr<Family> up(created);
      families[name] = std::move(up);
      return *created;
    }

    /// @brief Registers a new typed family or returns an existing one.
    ///
    /// Delegates to the untyped Add() and then reinterpret_casts the result
    /// to CustomFamily<MetricType>.
    ///
    /// @note This is technically UB via reinterpret_cast, but is safe in
    ///       practice because CustomFamily adds no data members and is
    ///       layout-compatible with Family.
    ///
    /// @tparam MetricType Concrete metric class.
    /// @param name        Metric family name.
    /// @param help        Help/description string.
    /// @param base_labels Constant labels.
    /// @return Reference to the CustomFamily.
    template <typename MetricType>
    CustomFamily<MetricType>& Add(const std::string& name, const std::string& help, const labels_t& base_labels = labels_t()) {
      Family& family = this->Add(name, help, base_labels);
      return *reinterpret_cast<CustomFamily<MetricType>*>(&family);
    }

    /// @brief Removes the family with the given name from the registry.
    ///
    /// Does nothing if no family with this name exists.
    ///
    /// @param name Metric family name to remove.
    /// @return true if the family was found and removed.
    bool Remove(const std::string& name) {
      std::lock_guard<std::mutex> lock(mutex);
      return families.erase(name) > 0;
    }

    /// @brief Removes all families from the registry.
    void RemoveAll() {
      std::lock_guard<std::mutex> lock(mutex);
      families.clear();
    }

    /// @brief Serializes all registered families to the given output stream.
    /// @param out Output stream.
    void serialize(std::ostream& out) {
      std::lock_guard<std::mutex> lock(mutex);

      // Switch to the classic locale for locale-independent number formatting.
      std::locale saved = out.getloc();
      out.imbue(std::locale::classic());

      #if 1 // Collect pointers sorted by family name.
      
      std::map<std::string, Family*> sorted;
      for (const typename Families::value_type& entry : families)
        sorted.emplace(entry.first, entry.second.get());

      for (const typename std::map<std::string, Family*>::value_type& entry : sorted)
        entry.second->serialize(out);

      #else // Serialize in arbitrary order without sorting.

      for (const typename Families::value_type& family : families)
        family.second->serialize(out);
      #endif

      out.imbue(saved);
    }

    /// @brief Serializes all registered families and returns the result as a string.
    /// @return Serialized text in Prometheus exposition format.
    std::string serialize() {
      std::ostringstream oss;
      serialize(oss);
      return oss.str();
    }
  };

  // --- Deferred definition of CustomFamily<MetricType>::Build() ---------------

  /// @brief Registers (or retrieves) a CustomFamily in the given registry.
  template <typename MetricType>
  CustomFamily<MetricType>& CustomFamily<MetricType>::Build(Registry& registry, const std::string& name, const std::string& help, const labels_t& labels) {
    return registry.Add<MetricType>(name, help, labels);
  }

  // =============================================================================
  // Builder<MetricType> — fluent builder for registering a CustomFamily
  // =============================================================================

  /// @brief Fluent builder that accumulates name, help, and labels,
  ///        then registers a CustomFamily in one call to Register().
  ///
  /// @tparam MetricType Concrete metric class.
  template <typename MetricType>
  class Builder {
    std::string name_;
    std::string help_;
    labels_t    labels_;

  public:
    Builder() = default;

    /// @brief Sets the metric family name.
    /// @param name Metric name.
    /// @return Reference to this builder for chaining.
    Builder& Name(const std::string& name)   { name_   = name;   return *this; }

    /// @brief Sets the help/description string.
    /// @param help Help text.
    /// @return Reference to this builder for chaining.
    Builder& Help(const std::string& help)   { help_   = help;   return *this; }

    /// @brief Sets the constant base labels.
    /// @param labels Base label set.
    /// @return Reference to this builder for chaining.
    Builder& Labels(const labels_t& labels)  { labels_ = labels; return *this; }

    /// @brief Registers the family in the given registry and returns it.
    /// @param registry Registry to register in.
    /// @return Reference to the registered CustomFamily.
    CustomFamily<MetricType>& Register(Registry& registry) {
      return registry.Add<MetricType>(name_, help_, labels_);
    }
  };




  // =============================================================================
  // SimpleAPI — lightweight wrappers for convenient everyday use
  //
  // Metrics are assumed to be created once and never removed, so references
  // returned by Add() remain valid for the lifetime of the registry.
  // =============================================================================

  /// @brief Alias for Registry used in the SimpleAPI layer.
  using registry_t = Registry;

  /// @brief Global default registry used when no explicit registry is provided.
  extern registry_t global_registry;

  // =============================================================================
  // atomic_storage<V> — type trait for atomic storage selection
  // =============================================================================

  /// @brief Selects the appropriate atomic storage type based on whether V is a reference.
  ///
  /// - If V is a non-reference type, `storage_type` is `std::atomic<V>` (owning).
  /// - If V is a reference type `T&`, `storage_type` is `std::atomic<T>&` (non-owning reference).
  ///
  /// @tparam V Value type; may be `T` (owning) or `T&` (reference).
  template <typename MetricValue>
  struct atomic_storage {
    using value_type   = std::remove_reference_t<MetricValue>;
    using storage_type = std::conditional_t<
      std::is_reference<MetricValue>::value,
      std::atomic<value_type>&,
      std::atomic<value_type>
    >;
  };

  // =============================================================================
  // modify_t — type trait mapping MetricTemplate<V> → MetricTemplate<V&>
  // =============================================================================

  /// @brief Maps a metric type `MetricTemplate<V>` to its reference counterpart
  ///        `MetricTemplate<V&>`, enabling zero-copy "reference" metric handles.
  ///
  /// Works for any single-type-parameter metric template without per-type
  /// specialization.
  ///
  /// @tparam MetricType The owning metric type to derive the reference form from.
  template <typename MetricType>
  struct modify_t;

  /// @brief Partial specialization that deduces the template and value type.
  ///
  /// Given `MetricTemplate<V>`:
  /// - `value_type` is `std::remove_reference_t<V>`.
  /// - `metric_own` is the owning form `MetricTemplate<value_type>`.
  /// - `metric_ref` is the reference form `MetricTemplate<value_type&>`.
  ///
  /// @tparam MetricTemplate Single-type-parameter metric class template.
  /// @tparam V              Value type (possibly already a reference).
  template <template<typename> class MetricTemplate, typename MetricValue>
  struct modify_t<MetricTemplate<MetricValue>> {
    using value_type = std::remove_reference_t<MetricValue>;
    using metric_own = MetricTemplate<value_type>;
    using metric_ref = MetricTemplate<value_type&>;
    static_assert(std::is_base_of<Metric, metric_own>::value, "modify_t: MetricTemplate must be derived from Metric");
  };

  // =============================================================================
  // family_t — untyped SimpleAPI wrapper around Family
  // =============================================================================

  /// @brief Lightweight wrapper that binds a Family reference to a registry.
  ///
  /// Can be constructed with an explicit registry or with the global one.
  class family_t {
  protected:
    Family& ref;  ///< Reference to the underlying Family.

  public:

    /// @brief Constructs a family_t bound to the specified registry.
    /// @param registry    Registry to register the family in.
    /// @param name        Metric family name.
    /// @param help        Help/description string.
    /// @param base_labels Constant base labels.
    family_t(registry_t& registry, const std::string& name, const std::string& help, const labels_t& base_labels = {})
      : ref(registry.Add(name, help, base_labels)) {}

    /// @brief Constructs a family_t bound to the specified registry.
    /// @param registry    Shared ptr to registry to register the family in.
    /// @param name        Metric family name.
    /// @param help        Help/description string.
    /// @param base_labels Constant base labels.
    family_t(std::shared_ptr<registry_t>& registry, const std::string& name, const std::string& help, const labels_t& base_labels = {})
      : ref(registry->Add(name, help, base_labels)) {}

    /// @brief Constructs a family_t bound to the global registry.
    /// @param name        Metric family name.
    /// @param help        Help/description string.
    /// @param base_labels Constant base labels.
    family_t(const std::string& name, const std::string& help, const labels_t& base_labels = {})
      : ref(global_registry.Add(name, help, base_labels)) {}

    /// @brief Implicit conversion to the underlying Family reference.
    //operator Family&() { return ref; }

    /// @brief Implicit conversion to the underlying const Family reference.
    //operator const Family&() const { return ref; }

    /// @brief Creates or retrieves a metric of the given type with the specified labels.
    /// @tparam MetricType   Concrete metric class.
    /// @tparam Args         Additional constructor arguments forwarded to MetricType.
    /// @param metric_labels Per-metric dimensional labels.
    /// @param args          Extra arguments forwarded to the MetricType constructor.
    /// @return Reference to the metric instance.
    template <typename MetricType, typename... Args>
    typename modify_t<MetricType>::metric_own& Add(const labels_t& metric_labels, Args&&... args) {
      return ref.Add<typename modify_t<MetricType>::metric_own>(metric_labels, std::forward<Args>(args)...  );
    }
  };

  // =============================================================================
  // custom_family_t<MetricType> — typed SimpleAPI wrapper around Family
  // =============================================================================

  /// @brief Typed SimpleAPI wrapper that stores MetricType at the type level,
  ///        providing an Add() that doesn't require an explicit template argument.
  ///
  /// @tparam MetricType Concrete metric class.
  template <typename MetricType>
  class custom_family_t : family_t {
  public:

    /// @brief Constructs a typed family wrapper bound to the specified registry.
    /// @param registry    Registry to register the family in.
    /// @param name        Metric family name.
    /// @param help        Help/description string.
    /// @param base_labels Constant base labels.
    custom_family_t(registry_t& registry, const std::string& name, const std::string& help, const labels_t& base_labels = {})
      : family_t(registry, name, help, base_labels) {}

    /// @brief Constructs a typed family wrapper bound to the specified registry.
    /// @param registry    Shared ptr to registry to register the family in.
    /// @param name        Metric family name.
    /// @param help        Help/description string.
    /// @param base_labels Constant base labels.
    custom_family_t(std::shared_ptr<registry_t>& registry, const std::string& name, const std::string& help, const labels_t& base_labels = {})
      : family_t(registry, name, help, base_labels) {}

    /// @brief Constructs a typed family wrapper bound to the global registry.
    /// @param name        Metric family name.
    /// @param help        Help/description string.
    /// @param base_labels Constant base labels.
    custom_family_t(const std::string& name, const std::string& help, const labels_t& base_labels = {})
      : family_t(name, help, base_labels) {}

    /// @brief Implicit conversion to the underlying Family reference.
    //operator CustomFamily<MetricType>&() { return *reinterpret_cast<CustomFamily<MetricType>*>(&ref); }

    /// @brief Implicit conversion to the underlying const Family reference.
    //operator const CustomFamily<MetricType>&() const { return *reinterpret_cast<CustomFamily<MetricType>*>(&ref); }

    /// @brief Creates or retrieves a metric with the given per-metric labels.
    /// @tparam Args         Additional constructor arguments forwarded to MetricType.
    /// @param metric_labels Per-metric dimensional labels.
    /// @param args          Extra arguments forwarded to the MetricType constructor.
    /// @return Reference to the metric instance.
    template <typename... Args>
    typename modify_t<MetricType>::metric_own& Add(const labels_t& metric_labels, Args&&... args) {
      CustomFamily<typename modify_t<MetricType>::metric_own>& typed_ref = *reinterpret_cast<CustomFamily<typename modify_t<MetricType>::metric_own>*>(&ref);
      return typed_ref.Add (metric_labels, std::forward<Args>(args)...);
    }
  };

  /// @brief Wraps a reference to a registry into a shared_ptr with a no-op deleter.
  ///
  /// The resulting shared_ptr will never delete the pointee, making it safe
  /// to use with stack-allocated or otherwise externally-owned registries.
  ///
  /// @param registry Registry reference to wrap.
  /// @return Non-owning shared_ptr to the registry.
  inline std::shared_ptr<registry_t> make_non_owning(registry_t& registry) {
    return std::shared_ptr<registry_t>(&registry, [](registry_t*) noexcept {});
  }

} // namespace prometheus
