/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "job_system.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Framework::Jobs::IO {

    /**
     * @brief Result of a file read operation
     */
    struct FileResult {
        std::string path;
        std::vector<uint8_t> data;
        std::string error;
        bool success = true;

        static FileResult Success(std::string filePath, std::vector<uint8_t> fileData) {
            return FileResult{std::move(filePath), std::move(fileData), "", true};
        }

        static FileResult Failure(std::string filePath, std::string errorMsg) {
            return FileResult{std::move(filePath), {}, std::move(errorMsg), false};
        }
    };

    /**
     * @brief Asynchronously read a file
     *
     * The callback will be invoked on the main thread during ProcessCompletedCallbacks().
     *
     * @param jobs The JobSystem to use
     * @param path Path to the file
     * @param onComplete Called with the file data when complete
     * @param onError Called if an error occurs (optional)
     * @param priority Task priority
     */
    void ReadFileAsync(JobSystem *jobs, const std::string &path, fu2::function<void(std::vector<uint8_t>)> onComplete, fu2::function<void(std::string)> onError = nullptr, ftl::TaskPriority priority = ftl::TaskPriority::Normal);

    /**
     * @brief Asynchronously write a file
     *
     * The callback will be invoked on the main thread during ProcessCompletedCallbacks().
     *
     * @param jobs The JobSystem to use
     * @param path Path to the file
     * @param data Data to write
     * @param onComplete Called when write is complete (optional)
     * @param onError Called if an error occurs (optional)
     * @param priority Task priority
     */
    void WriteFileAsync(JobSystem *jobs, const std::string &path, std::vector<uint8_t> data, fu2::function<void()> onComplete = nullptr, fu2::function<void(std::string)> onError = nullptr, ftl::TaskPriority priority = ftl::TaskPriority::Normal);

    /**
     * @brief Asynchronously read multiple files in parallel
     *
     * The callback will be invoked on the main thread during ProcessCompletedCallbacks()
     * when all files have been read.
     *
     * @param jobs The JobSystem to use
     * @param paths Paths to the files
     * @param onAllComplete Called with results for all files
     * @param priority Task priority
     */
    void ReadFilesAsync(JobSystem *jobs, const std::vector<std::string> &paths, fu2::function<void(std::vector<FileResult>)> onAllComplete, ftl::TaskPriority priority = ftl::TaskPriority::Normal);

    /**
     * @brief Read a file, blocking the current fiber but not the worker thread
     *
     * This should only be called from within a task running on the JobSystem.
     * The fiber will yield while the file is being read, allowing other tasks
     * to run on the worker thread.
     *
     * @param jobs The JobSystem to use
     * @param path Path to the file
     * @return The file contents
     * @throws std::runtime_error if the file cannot be read
     */
    std::vector<uint8_t> ReadFileBlocking(JobSystem *jobs, const std::string &path);

    /**
     * @brief Write a file, blocking the current fiber but not the worker thread
     *
     * This should only be called from within a task running on the JobSystem.
     *
     * @param jobs The JobSystem to use
     * @param path Path to the file
     * @param data Data to write
     * @throws std::runtime_error if the file cannot be written
     */
    void WriteFileBlocking(JobSystem *jobs, const std::string &path, const std::vector<uint8_t> &data);

    /**
     * @brief Read multiple files in parallel, blocking until all complete
     *
     * This should only be called from within a task running on the JobSystem.
     *
     * @param jobs The JobSystem to use
     * @param paths Paths to the files
     * @return Results for all files (check success flag for each)
     */
    std::vector<FileResult> ReadFilesBlocking(JobSystem *jobs, const std::vector<std::string> &paths);

} // namespace Framework::Jobs::IO
