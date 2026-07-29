/*
* prometheus-cpp-lite — header-only C++ library for exposing Prometheus metrics
* https://github.com/biaks/prometheus-cpp-lite
*
* Copyright (c) 2026 Yan Kryukov ianiskr@gmail.com
* Licensed under the MIT License
*/

#pragma once

#include "prometheus/core.h"

#include <thread>
#include <chrono>
#include <string>
#include <fstream>
#include <memory>

namespace prometheus {

  /// @brief Periodically serializes a Registry to a file in Prometheus text exposition format.
  ///
  /// Useful for environments where a Prometheus node_exporter textfile collector
  /// reads `.prom` files from a directory.
  ///
  /// Typical usage:
  /// @code
  ///   std::shared_ptr<registry_t> registry = std::make_shared<registry_t>();
  ///   file_saver_t saver(registry, std::chrono::seconds(10), "./metrics.prom");
  ///   // metrics are now written to ./metrics.prom every 10 seconds
  /// @endcode
  class file_saver_t {

    std::shared_ptr<registry_t> registry_ptr  { nullptr };
    std::thread                 worker_thread;
    std::atomic<bool>           must_die      { false };

    std::chrono::seconds        period        { 10 };
    std::string                 filename      { "./metrics.prom" };

  public:

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor.  Registry, period, and filename must be set before calling start().
    file_saver_t() = default;

    /// @brief Constructs with a shared registry.  Call start() to begin saving.
    /// @param registry_ Shared pointer to the registry to serialize.
    explicit file_saver_t(std::shared_ptr<registry_t> registry_)
      : registry_ptr(std::move(registry_)) {}

    /// @brief Constructs with a shared registry.  Call start() to begin saving.
    /// @param registry_ Registry to serialize.
    explicit file_saver_t(registry_t& registry_)
      : registry_ptr(make_non_owning(registry_)) {}

    /// @brief Constructs with a shared registry, save period, and output filename.  Starts immediately.
    /// @param registry_ Shared pointer to the registry to serialize.
    /// @param period_   Interval between file writes.
    /// @param filename_ Path to the output file.
    file_saver_t(std::shared_ptr<registry_t> registry_, const std::chrono::seconds& period_, const std::string& filename_)
      : registry_ptr(std::move(registry_)), period(period_), filename(filename_) {
      start();
    }

    /// @brief Constructs with a shared registry, save period, and output filename.  Starts immediately.
    /// @param registry_ registry to serialize.
    /// @param period_   Interval between file writes.
    /// @param filename_ Path to the output file.
    file_saver_t(registry_t& registry_, const std::chrono::seconds& period_, const std::string& filename_)
      : registry_ptr(make_non_owning(registry_)), period(period_), filename(filename_) {
      start();
    }

    /// @brief Stops the saver and joins the worker thread.
    ~file_saver_t() {
      stop();
    }

    // Non-copyable (owns a thread).
    file_saver_t(const file_saver_t&) = delete;
    file_saver_t& operator=(const file_saver_t&) = delete;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Replaces the registry to serialize.
    /// @param registry_ New shared pointer to a registry.
    void set_registry(std::shared_ptr<registry_t> registry_) {
      registry_ptr = std::move(registry_);
    }

    /// @brief Sets the save interval.
    /// @param new_period Interval between file writes.
    void set_delay(const std::chrono::seconds& new_period) {
      period = new_period;
    }

    /// @brief Sets the output filename and verifies that it can be opened for writing.
    /// @param filename_ Path to the output file.
    /// @return true if the file was successfully opened (and closed) for writing.
    bool set_out_file(const std::string& filename_) {
      filename = filename_;
      std::fstream out_file_stream;
      out_file_stream.open(filename, std::fstream::out | std::fstream::binary);
      bool open_success = out_file_stream.is_open();
      out_file_stream.close();
      return open_success;
    }

    // =========================================================================
    // Control
    // =========================================================================

    /// @brief Sets the save period and filename, then starts the worker thread.
    /// @param period_   Interval between file writes.
    /// @param filename_ Path to the output file.
    void start(const std::chrono::seconds& period_, const std::string& filename_) {
      set_delay(period_);
      set_out_file(filename_);
      start();
    }

    /// @brief Starts the file saver worker thread.
    void start() {
      must_die = false;
      worker_thread = std::thread{ &file_saver_t::worker_function, this };
    }

    /// @brief Stops the file saver and joins the worker thread.
    void stop() {
      must_die = true;
      if (worker_thread.joinable())
        worker_thread.join();
    }

  private:

    // =========================================================================
    // Worker
    // =========================================================================

    /// @brief Worker loop: periodically serializes the registry to the output file.
    ///
    /// The sleep interval is divided into small fractions so that stop() is
    /// responsive even with a long save period.
    void worker_function() {
      // Divide the period into small fractions for responsive shutdown.
      const uint64_t divider  = 100;
      uint64_t       fraction = divider;

      while (true) {
        std::chrono::milliseconds period_ms = std::chrono::duration_cast<std::chrono::milliseconds>(period);
        std::this_thread::sleep_for(period_ms / divider);

        if (must_die || --fraction == 0) {
          fraction = divider;

          if (registry_ptr)
            save_once();

          if (must_die)
            return;
        }
      }
    }

    /// @brief Performs a single save: serializes the registry to the output file.
    ///
    /// Opens the file, writes the serialized metrics, and closes it.
    /// The file is overwritten on each save (not appended).
    void save_once() {
      std::fstream out_file_stream;
      out_file_stream.open(filename, std::fstream::out | std::fstream::binary);
      if (out_file_stream.is_open()) {
        registry_ptr->serialize(out_file_stream);
        out_file_stream.close();
      }
    }
  };

  /// @brief Alias for backward compatibility.
  using SaveToFile = file_saver_t;

} // namespace prometheus
