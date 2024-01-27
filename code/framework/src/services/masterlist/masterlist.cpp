#include "masterlist.h"

namespace Framework::Services {
    MasterlistBase::MasterlistBase() {
        _client = std::make_shared<httplib::Client>("https://api.mafiahub.dev");
    }
}
