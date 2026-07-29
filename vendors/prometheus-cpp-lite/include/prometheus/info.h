
#pragma once

#include "prometheus/gauge.h"

namespace prometheus {

  /// @brief Info metric — a gauge fixed at 1 whose labels carry metadata.
  ///
  /// Common use case: exposing build version, commit hash, etc.
  ///
  /// @code
  ///   info_metric_t build_info(registry, "build_info", "Build information",
  ///                            {{"version", "1.2.3"}, {"commit", "abc123"}});
  /// @endcode
  ///
  /// Produces:
  ///   # HELP build_info Build information
  ///   # TYPE build_info gauge
  ///   build_info{version="1.2.3",commit="abc123"} 1
  template <typename MetricValue = double>
  class info_t : public Metric {

  public:
    using storage_type = typename atomic_storage<MetricValue>::storage_type;
    using value_type   = typename atomic_storage<MetricValue>::value_type;

  private:
    storage_type value;

    friend info_t<value_type&>;

  public:

    // --- Owning constructor ---
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    explicit info_t(const labels_t& labels)
      : Metric(labels), value(1) {}

    // --- Reference constructors ---
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    info_t(info_t<value_type>& other)
      : Metric(other.labels_ptr), value(other.value) {}

    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    info_t(family_t& family, const labels_t& labels = {})
      : info_t(family.Add<info_t<value_type> >(labels)) {}

    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    info_t(custom_family_t<info_t<value_type> >& family, const labels_t& labels = {})
      : info_t(family.Add(labels)) {}

    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    info_t(Registry& registry, const std::string& name, const std::string& help, const labels_t& labels = {})
      : info_t(registry.Add(name, help).Add<info_t<value_type>>(labels)) {}

    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    info_t(std::shared_ptr<registry_t>& registry, const std::string& name, const std::string& help, const labels_t& labels = {})
      : info_t(registry->Add(name, help).Add<info_t<value_type>>(labels)) {}
    template <typename U = MetricValue, std::enable_if_t<std::is_reference<U>::value, int> = 0>
    info_t(const std::string& name, const std::string& help, const labels_t& labels = {})
      : info_t(global_registry.Add(name, help).Add<info_t<value_type>>(labels)) {}

    // Non-copyable (owning form)
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    info_t(const info_t&) = delete;
    template <typename U = MetricValue, std::enable_if_t<!std::is_reference<U>::value, int> = 0>
    info_t& operator=(const info_t&) = delete;

    /// @brief Always returns 1.
    value_type Get() const { return value.load(); }

    // --- Metric interface ---
    const char* type_name() const override { return "gauge"; }
    void collect() override {}
    void serialize(std::ostream& out, const std::string& family_name, const labels_t& base_labels) const override {
      TextSerializer::WriteLine(out, family_name, base_labels, this->get_labels(), static_cast<value_type>(1));
    }
  };

  using info_metric_t = typename modify_t<info_t<>>::metric_ref;

} // namespace prometheus
