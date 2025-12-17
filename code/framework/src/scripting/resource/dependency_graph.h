#pragma once

#include "resource_manifest.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Framework::Scripting {

    /**
     * Result of a dependency graph operation.
     */
    struct DependencyGraphResult {
        bool success = false;
        std::string error;
        std::vector<std::string> cycle; // Populated if circular dependency detected

        static DependencyGraphResult Success() {
            DependencyGraphResult result;
            result.success = true;
            return result;
        }

        static DependencyGraphResult Failure(const std::string &error) {
            DependencyGraphResult result;
            result.success = false;
            result.error   = error;
            return result;
        }

        static DependencyGraphResult CycleDetected(const std::vector<std::string> &cycle) {
            DependencyGraphResult result;
            result.success = false;
            result.error   = "Circular dependency detected";
            result.cycle   = cycle;
            return result;
        }
    };

    /**
     * Node in the dependency graph representing a resource.
     */
    struct DependencyNode {
        std::string name;
        int priority = 0;

        // Resources this node depends on (outgoing edges)
        std::set<std::string> dependencies;

        // Resources that depend on this node (incoming edges / reverse dependencies)
        std::set<std::string> dependents;

        // Version constraints for each dependency
        std::map<std::string, std::string> versionConstraints;
    };

    /**
     * Directed Acyclic Graph (DAG) for managing resource dependencies.
     *
     * Responsibilities:
     * - Track dependencies between resources
     * - Detect circular dependencies
     * - Calculate load order via topological sort
     * - Track reverse dependencies for cascade operations
     */
    class DependencyGraph final {
      public:
        DependencyGraph()  = default;
        ~DependencyGraph() = default;

        // Non-copyable but moveable
        DependencyGraph(const DependencyGraph &)            = delete;
        DependencyGraph &operator=(const DependencyGraph &) = delete;
        DependencyGraph(DependencyGraph &&)                 = default;
        DependencyGraph &operator=(DependencyGraph &&)      = default;

        /**
         * Add a node to the graph.
         * @param name Unique resource name
         * @param priority Load order priority (higher = load later among independent resources)
         * @return Success or failure with error message
         */
        DependencyGraphResult AddNode(const std::string &name, int priority = 0);

        /**
         * Remove a node from the graph.
         * Also removes all edges to/from this node.
         * @param name Resource name to remove
         * @return Success or failure with error message
         */
        DependencyGraphResult RemoveNode(const std::string &name);

        /**
         * Add a dependency edge from dependent to dependency.
         * @param dependent The resource that requires another
         * @param dependency The resource being required
         * @param versionConstraint Optional version constraint (e.g., ">=1.0.0")
         * @return Success or failure (including cycle detection)
         */
        DependencyGraphResult AddDependency(const std::string &dependent, const std::string &dependency, const std::string &versionConstraint = "");

        /**
         * Remove a dependency edge.
         * @param dependent The resource that was requiring another
         * @param dependency The resource that was being required
         * @return Success or failure with error message
         */
        DependencyGraphResult RemoveDependency(const std::string &dependent, const std::string &dependency);

        /**
         * Check if adding a dependency would create a cycle.
         * @param dependent The resource that would require another
         * @param dependency The resource that would be required
         * @return True if adding this edge would create a cycle
         */
        bool WouldCreateCycle(const std::string &dependent, const std::string &dependency) const;

        /**
         * Get the load order for all resources via topological sort.
         * Resources with no dependencies come first.
         * Priority breaks ties among independent resources (lower priority = load first).
         * @return Ordered list of resource names
         */
        std::vector<std::string> GetLoadOrder() const;

        /**
         * Get the load order for a specific resource and its dependencies.
         * @param name Resource name
         * @return Ordered list starting with dependencies, ending with the resource itself
         */
        std::vector<std::string> GetLoadOrderFor(const std::string &name) const;

        /**
         * Get the unload order (reverse of load order).
         * Resources that are depended upon come last.
         * @return Ordered list of resource names for unloading
         */
        std::vector<std::string> GetUnloadOrder() const;

        /**
         * Get all resources that directly depend on the given resource.
         * @param name Resource name
         * @return Set of dependent resource names
         */
        std::set<std::string> GetDirectDependents(const std::string &name) const;

        /**
         * Get all resources that the given resource directly depends on.
         * @param name Resource name
         * @return Set of dependency resource names
         */
        std::set<std::string> GetDirectDependencies(const std::string &name) const;

        /**
         * Get all resources that transitively depend on the given resource.
         * @param name Resource name
         * @return Set of all dependent resource names (direct and transitive)
         */
        std::set<std::string> GetAllDependents(const std::string &name) const;

        /**
         * Get all resources that the given resource transitively depends on.
         * @param name Resource name
         * @return Set of all dependency resource names (direct and transitive)
         */
        std::set<std::string> GetAllDependencies(const std::string &name) const;

        /**
         * Check if a node exists in the graph.
         */
        bool HasNode(const std::string &name) const;

        /**
         * Check if a dependency relationship exists.
         */
        bool HasDependency(const std::string &dependent, const std::string &dependency) const;

        /**
         * Get the version constraint for a dependency.
         * @return Version constraint string, or empty if no constraint
         */
        std::string GetVersionConstraint(const std::string &dependent, const std::string &dependency) const;

        /**
         * Get all node names in the graph.
         */
        std::vector<std::string> GetAllNodes() const;

        /**
         * Get the number of nodes in the graph.
         */
        size_t GetNodeCount() const;

        /**
         * Clear all nodes and edges from the graph.
         */
        void Clear();

        /**
         * Validate that all dependencies can be satisfied.
         * Checks that all referenced dependencies exist as nodes.
         * @param outMissing Output: map of resource -> missing dependencies
         * @return True if all dependencies are satisfiable
         */
        bool ValidateDependencies(std::map<std::string, std::vector<std::string>> &outMissing) const;

      private:
        /**
         * Perform DFS to detect cycles starting from a node.
         * @param start Starting node
         * @param visited Nodes visited in current path
         * @param cycle Output: the cycle path if found
         * @return True if a cycle was detected
         */
        bool DetectCycle(const std::string &start, std::set<std::string> &visited, std::vector<std::string> &cycle) const;

        /**
         * Kahn's algorithm for topological sort.
         * @return Sorted node names, or empty if graph has cycles
         */
        std::vector<std::string> TopologicalSort() const;

        /**
         * Helper to collect all transitive nodes in a direction.
         */
        void CollectTransitive(const std::string &name, std::set<std::string> &result, bool followDependencies) const;

        // Node storage
        std::map<std::string, DependencyNode> _nodes;
    };

} // namespace Framework::Scripting
