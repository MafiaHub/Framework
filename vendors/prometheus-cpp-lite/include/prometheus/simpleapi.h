
// Compatibility shim — v1.0 header name.
// In v2.0 all core classes are in core.h.
#pragma once
#pragma message("warning: <prometheus/simpleapi.h> is deprecated in v2.0, use <prometheus/prometheus.h> instead")
#include "prometheus/prometheus.h"

namespace prometheus {
  namespace simpleapi {
    using counter_family_t   [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::counter_family_t;
    using counter_metric_t   [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::counter_metric_t;
    using gauge_family_t     [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::gauge_family_t;
    using gauge_metric_t     [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::gauge_metric_t;
    using summary_family_t   [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::summary_family_t;
    using summary_metric_t   [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::summary_metric_t;
    using histogram_family_t [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::histogram_family_t;
    using histogram_metric_t [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::histogram_metric_t;
    using benchmark_family_t [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::benchmark_family_t;
    using benchmark_metric_t [[deprecated("Use prometheus namespace instead of prometheus::simpleapi")]] = prometheus::benchmark_metric_t;
  }
}
