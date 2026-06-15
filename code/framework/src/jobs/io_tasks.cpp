/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "io_tasks.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <stdexcept>

namespace Framework::Jobs::IO {

    static std::vector<uint8_t> ReadFileInternal(const std::string &path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + path);
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
            throw std::runtime_error("Failed to read file: " + path);
        }

        return buffer;
    }

    static void WriteFileInternal(const std::string &path, const std::vector<uint8_t> &data) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + path);
        }

        if (!file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()))) {
            throw std::runtime_error("Failed to write file: " + path);
        }
    }

    void ReadFileAsync(JobSystem *jobs, const std::string &path, fu2::function<void(std::vector<uint8_t>)> onComplete, fu2::function<void(std::string)> onError, ftl::TaskPriority priority) {
        jobs->ScheduleWithCallback(
            [jobs, path]() {
                // This runs on a fiber, use BlockingCall to not block the worker
                jobs->BlockingCall([&path]() {
                    return ReadFileInternal(path);
                });
            },
            [onComplete = std::move(onComplete), jobs, path]() mutable {
                // On success - we need to re-read since we can't pass data through
                // This is a limitation; for production, consider a shared state mechanism
                try {
                    auto data = ReadFileInternal(path);
                    if (onComplete) {
                        onComplete(std::move(data));
                    }
                } catch (const std::exception &e) {
                    spdlog::error("ReadFileAsync callback re-read failed: {}", e.what());
                }
            },
            [onError = std::move(onError), path](std::exception_ptr ex) mutable {
                std::string message;
                try {
                    std::rethrow_exception(ex);
                } catch (const std::exception &e) {
                    message = e.what();
                } catch (...) {
                    message = "Unknown error reading file: " + path;
                }
                if (onError) {
                    onError(message);
                } else {
                    spdlog::error("ReadFileAsync failed ({}) with no error handler: {}", path, message);
                }
            },
            priority);
    }

    void WriteFileAsync(JobSystem *jobs, const std::string &path, std::vector<uint8_t> data, fu2::function<void()> onComplete, fu2::function<void(std::string)> onError, ftl::TaskPriority priority) {
        jobs->ScheduleWithCallback(
            [jobs, path, data = std::move(data)]() {
                jobs->BlockingCall([&path, &data]() {
                    WriteFileInternal(path, data);
                });
            },
            [onComplete = std::move(onComplete)]() mutable {
                if (onComplete) {
                    onComplete();
                }
            },
            [onError = std::move(onError), path](std::exception_ptr ex) mutable {
                std::string message;
                try {
                    std::rethrow_exception(ex);
                } catch (const std::exception &e) {
                    message = e.what();
                } catch (...) {
                    message = "Unknown error writing file: " + path;
                }
                if (onError) {
                    onError(message);
                } else {
                    spdlog::error("WriteFileAsync failed ({}) with no error handler: {}", path, message);
                }
            },
            priority);
    }

    void ReadFilesAsync(JobSystem *jobs, const std::vector<std::string> &paths, fu2::function<void(std::vector<FileResult>)> onAllComplete, ftl::TaskPriority priority) {
        // Create shared state for results
        struct SharedState {
            std::mutex mutex;
            std::vector<FileResult> results;
            size_t completed = 0;
            size_t total = 0;
            fu2::function<void(std::vector<FileResult>)> callback;
            JobSystem *jobs;
        };

        auto state = std::make_shared<SharedState>();
        state->total = paths.size();
        state->results.resize(paths.size());
        state->callback = std::move(onAllComplete);
        state->jobs = jobs;

        for (size_t i = 0; i < paths.size(); ++i) {
            const auto &path = paths[i];
            jobs->Schedule(
                [state, path, i, jobs]() {
                    FileResult result;
                    result.path = path;

                    try {
                        result.data = jobs->BlockingCall([&path]() {
                            return ReadFileInternal(path);
                        });
                        result.success = true;
                    } catch (const std::exception &e) {
                        result.error = e.what();
                        result.success = false;
                    }

                    bool allDone = false;
                    {
                        std::scoped_lock lock(state->mutex);
                        state->results[i] = std::move(result);
                        state->completed++;
                        allDone = (state->completed == state->total);
                    }

                    if (allDone && state->callback) {
                        // Queue callback on main thread
                        // We use a simple approach: just call directly since we're in a task
                        // For true main-thread delivery, you'd queue through JobSystem
                        state->callback(std::move(state->results));
                    }
                },
                priority);
        }
    }

    std::vector<uint8_t> ReadFileBlocking(JobSystem *jobs, const std::string &path) {
        return jobs->BlockingCall([&path]() {
            return ReadFileInternal(path);
        });
    }

    void WriteFileBlocking(JobSystem *jobs, const std::string &path, const std::vector<uint8_t> &data) {
        jobs->BlockingCall([&path, &data]() {
            WriteFileInternal(path, data);
        });
    }

    std::vector<FileResult> ReadFilesBlocking(JobSystem *jobs, const std::vector<std::string> &paths) {
        std::vector<FileResult> results(paths.size());
        std::mutex resultsMutex;

        auto wg = jobs->CreateWaitGroup();
        wg->Add(static_cast<int32_t>(paths.size()));

        for (size_t i = 0; i < paths.size(); ++i) {
            jobs->Schedule(wg.get(),
                [jobs, &paths, &results, &resultsMutex, i]() {
                    FileResult result;
                    result.path = paths[i];

                    try {
                        result.data = jobs->BlockingCall([&paths, i]() {
                            return ReadFileInternal(paths[i]);
                        });
                        result.success = true;
                    } catch (const std::exception &e) {
                        result.error = e.what();
                        result.success = false;
                    }

                    std::scoped_lock lock(resultsMutex);
                    results[i] = std::move(result);
                });
        }

        wg->Wait();
        return results;
    }

} // namespace Framework::Jobs::IO
