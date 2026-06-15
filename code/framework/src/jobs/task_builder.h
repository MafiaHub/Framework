/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "job_system.h"

#include <memory>
#include <string>
#include <vector>

namespace Framework::Jobs {

    class TaskNode;
    class TaskGraph;

    /**
     * @brief A node in a task graph representing a single task with dependencies
     */
    class TaskNode final {
        friend class TaskGraph;

      public:
        using TaskFunc = fu2::function<void()>;

        TaskNode(std::string name, TaskFunc func) : _name(std::move(name)), _func(std::move(func)) {
        }

        /**
         * @brief Add a dependency - this task will wait for the dependency to complete
         * @param dependency The task that must complete before this one
         * @return Reference to this task for chaining
         */
        TaskNode &DependsOn(std::shared_ptr<TaskNode> dependency) {
            _dependencies.push_back(std::move(dependency));
            return *this;
        }

        /**
         * @brief Add multiple dependencies
         * @param dependencies Tasks that must complete before this one
         * @return Reference to this task for chaining
         */
        template <typename... Args>
        TaskNode &DependsOn(std::shared_ptr<TaskNode> first, Args... rest) {
            DependsOn(std::move(first));
            return DependsOn(std::forward<Args>(rest)...);
        }

        const std::string &GetName() const {
            return _name;
        }

      private:
        std::string _name;
        TaskFunc _func;
        std::vector<std::shared_ptr<TaskNode>> _dependencies;
        bool _executed = false;
    };

    /**
     * @brief Builder for creating and executing task graphs with dependencies
     *
     * Usage:
     * @code
     * TaskGraph graph(jobSystem);
     *
     * auto loadConfig = graph.CreateTask("LoadConfig", []{ loadConfig(); });
     * auto loadAssets = graph.CreateTask("LoadAssets", []{ loadAssets(); });
     * auto init = graph.CreateTask("Init", []{ initialize(); });
     *
     * init->DependsOn(loadConfig, loadAssets);
     *
     * graph.Execute(init);  // Runs loadConfig and loadAssets in parallel, then init
     * @endcode
     */
    // Not thread-safe (build/Execute from one thread); can deadlock if fan-out exceeds worker threads.
    class TaskGraph final {
      public:
        explicit TaskGraph(JobSystem *jobSystem) : _jobSystem(jobSystem) {
        }

        /**
         * @brief Create a new task node
         * @param name Task name (for debugging/profiling)
         * @param func The function to execute
         * @return Shared pointer to the task node
         */
        std::shared_ptr<TaskNode> CreateTask(const std::string &name, TaskNode::TaskFunc func) {
            auto node = std::make_shared<TaskNode>(name, std::move(func));
            _nodes.push_back(node);
            return node;
        }

        /**
         * @brief Create a task with an unnamed/auto-generated name
         * @param func The function to execute
         * @return Shared pointer to the task node
         */
        std::shared_ptr<TaskNode> CreateTask(TaskNode::TaskFunc func) {
            return CreateTask("Task_" + std::to_string(_nodes.size()), std::move(func));
        }

        /**
         * @brief Execute the task graph starting from the given root task
         *
         * This will execute all dependencies in parallel where possible,
         * waiting for dependencies to complete before executing dependent tasks.
         *
         * @param root The root task to execute (all its dependencies will be executed first)
         * @param priority Task priority for all tasks in the graph
         */
        void Execute(std::shared_ptr<TaskNode> root, ftl::TaskPriority priority = ftl::TaskPriority::Normal) {
            // Reset execution state
            for (auto &node : _nodes) {
                node->_executed = false;
            }

            ExecuteNode(root, priority);
        }

        /**
         * @brief Execute all tasks in the graph that have no dependents
         * @param priority Task priority
         */
        void ExecuteAll(ftl::TaskPriority priority = ftl::TaskPriority::Normal) {
            // Find all leaf nodes (tasks with no dependents)
            std::vector<std::shared_ptr<TaskNode>> roots;

            for (auto &node : _nodes) {
                bool isDependent = false;
                for (auto &other : _nodes) {
                    for (auto &dep : other->_dependencies) {
                        if (dep == node) {
                            isDependent = true;
                            break;
                        }
                    }
                    if (isDependent)
                        break;
                }
                if (!isDependent) {
                    roots.push_back(node);
                }
            }

            // Reset execution state
            for (auto &node : _nodes) {
                node->_executed = false;
            }

            // Execute all roots
            auto wg = _jobSystem->CreateWaitGroup();
            wg->Add(static_cast<int32_t>(roots.size()));

            for (auto &root : roots) {
                _jobSystem->Schedule([this, root, priority, &wg]() {
                    ExecuteNode(root, priority);
                    wg->Done();
                });
            }

            wg->Wait();
        }

      private:
        JobSystem *_jobSystem;
        std::vector<std::shared_ptr<TaskNode>> _nodes;

        void ExecuteNode(std::shared_ptr<TaskNode> node, ftl::TaskPriority priority) {
            if (node->_executed) {
                return;
            }

            // Execute dependencies first
            if (!node->_dependencies.empty()) {
                auto wg = _jobSystem->CreateWaitGroup();
                wg->Add(static_cast<int32_t>(node->_dependencies.size()));

                for (auto &dep : node->_dependencies) {
                    if (!dep->_executed) {
                        _jobSystem->Schedule(wg.get(), [this, dep, priority]() {
                            ExecuteNode(dep, priority);
                        }, priority);
                    } else {
                        wg->Done();
                    }
                }

                wg->Wait();
            }

            // Execute this node
            node->_func();
            node->_executed = true;
        }
    };

} // namespace Framework::Jobs
