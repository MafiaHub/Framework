#define UNIT_MAX_MODULES 2
#include "logging/logger.h"
#include "unit.h"

/* TEST CATEGORIES */
#include "modules/interpolator_ut.h"
#include "modules/job_system_ut.h"

int main() {
    UNIT_CREATE("FrameworkTests");

    Framework::Logging::GetInstance()->PauseLogging(true);

    UNIT_MODULE(interpolator);
    //UNIT_MODULE(job_system);

    return UNIT_RUN();
}
