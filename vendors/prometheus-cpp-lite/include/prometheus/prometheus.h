
#include "prometheus/counter.h"
#include "prometheus/gauge.h"
#include "prometheus/summary.h"
#include "prometheus/histogram.h"
#include "prometheus/benchmark.h"
#include "prometheus/info.h"

#include <prometheus/file_saver.h>

namespace prometheus {
  extern Registry      global_registry;
  extern file_saver_t  file_saver;
}
