/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::External::Sentry {
    enum class SentryError { SENTRY_NONE, SENTRY_BREAKPAD_NOT_FOUND, SENTRY_CACHE_DIRECTORY_CREATION_FAILED, SENTRY_INIT_FAILED, SENTRY_INVALID_INSTANCE };
}
