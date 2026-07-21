
#include "prometheus/core.h"
#include "prometheus/file_saver.h"

namespace prometheus {
  Registry       global_registry;
  file_saver_t   file_saver  {global_registry};
}
