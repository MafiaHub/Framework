#include "dependency_graph.h"

#include <logging/logger.h>

#include <algorithm>
#include <queue>
#include <sstream>
#include <stack>

namespace Framework::Scripting {

    DependencyGraphResult DependencyGraph::AddNode(const std::string &name, int priority) {
        if (name.empty()) {
            return DependencyGraphResult::Failure("Node name cannot be empty");
        }

        if (_nodes.find(name) != _nodes.end()) {
            return DependencyGraphResult::Failure("Node already exists: " + name);
        }

        DependencyNode node;
        node.name     = name;
        node.priority = priority;
        _nodes[name]  = std::move(node);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("DependencyGraph: Added node '{}'", name);
        return DependencyGraphResult::Success();
    }

    DependencyGraphResult DependencyGraph::RemoveNode(const std::string &name) {
        auto it = _nodes.find(name);
        if (it == _nodes.end()) {
            return DependencyGraphResult::Failure("Node does not exist: " + name);
        }

        // Remove all edges from other nodes pointing to this one
        for (const auto &dependent : it->second.dependents) {
            auto depIt = _nodes.find(dependent);
            if (depIt != _nodes.end()) {
                depIt->second.dependencies.erase(name);
                depIt->second.versionConstraints.erase(name);
            }
        }

        // Remove all edges from this node to others
        for (const auto &dependency : it->second.dependencies) {
            auto depIt = _nodes.find(dependency);
            if (depIt != _nodes.end()) {
                depIt->second.dependents.erase(name);
            }
        }

        _nodes.erase(it);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("DependencyGraph: Removed node '{}'", name);
        return DependencyGraphResult::Success();
    }

    DependencyGraphResult DependencyGraph::AddDependency(const std::string &dependent, const std::string &dependency, const std::string &versionConstraint) {
        if (dependent == dependency) {
            return DependencyGraphResult::Failure("Resource cannot depend on itself: " + dependent);
        }

        auto depIt = _nodes.find(dependent);
        if (depIt == _nodes.end()) {
            return DependencyGraphResult::Failure("Dependent node does not exist: " + dependent);
        }

        auto depOnIt = _nodes.find(dependency);
        if (depOnIt == _nodes.end()) {
            return DependencyGraphResult::Failure("Dependency node does not exist: " + dependency);
        }

        // Check if this would create a cycle
        if (WouldCreateCycle(dependent, dependency)) {
            // Find and report the cycle
            std::set<std::string> visited;
            std::vector<std::string> cycle;

            // Temporarily add the edge to find the cycle path
            depIt->second.dependencies.insert(dependency);
            depOnIt->second.dependents.insert(dependent);

            DetectCycle(dependent, visited, cycle);

            // Remove the temporary edge
            depIt->second.dependencies.erase(dependency);
            depOnIt->second.dependents.erase(dependent);

            return DependencyGraphResult::CycleDetected(cycle);
        }

        // Add the dependency
        depIt->second.dependencies.insert(dependency);
        depOnIt->second.dependents.insert(dependent);

        if (!versionConstraint.empty()) {
            depIt->second.versionConstraints[dependency] = versionConstraint;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("DependencyGraph: Added dependency '{}' -> '{}'", dependent, dependency);
        return DependencyGraphResult::Success();
    }

    DependencyGraphResult DependencyGraph::RemoveDependency(const std::string &dependent, const std::string &dependency) {
        auto depIt = _nodes.find(dependent);
        if (depIt == _nodes.end()) {
            return DependencyGraphResult::Failure("Dependent node does not exist: " + dependent);
        }

        auto depOnIt = _nodes.find(dependency);
        if (depOnIt == _nodes.end()) {
            return DependencyGraphResult::Failure("Dependency node does not exist: " + dependency);
        }

        depIt->second.dependencies.erase(dependency);
        depIt->second.versionConstraints.erase(dependency);
        depOnIt->second.dependents.erase(dependent);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("DependencyGraph: Removed dependency '{}' -> '{}'", dependent, dependency);
        return DependencyGraphResult::Success();
    }

    bool DependencyGraph::WouldCreateCycle(const std::string &dependent, const std::string &dependency) const {
        // A cycle would be created if there's already a path from dependency to dependent
        // (adding dependent -> dependency would close the cycle)

        if (dependent == dependency) {
            return true;
        }

        // BFS from dependency to see if we can reach dependent
        std::set<std::string> visited;
        std::queue<std::string> toVisit;
        toVisit.push(dependency);

        while (!toVisit.empty()) {
            std::string current = toVisit.front();
            toVisit.pop();

            if (current == dependent) {
                return true; // Found a path back to dependent
            }

            if (visited.find(current) != visited.end()) {
                continue;
            }
            visited.insert(current);

            auto it = _nodes.find(current);
            if (it != _nodes.end()) {
                for (const auto &dep : it->second.dependencies) {
                    if (visited.find(dep) == visited.end()) {
                        toVisit.push(dep);
                    }
                }
            }
        }

        return false;
    }

    std::vector<std::string> DependencyGraph::GetLoadOrder() const {
        return TopologicalSort();
    }

    std::vector<std::string> DependencyGraph::GetLoadOrderFor(const std::string &name) const {
        if (_nodes.find(name) == _nodes.end()) {
            return {};
        }

        // Get all dependencies (transitive)
        std::set<std::string> allDeps = GetAllDependencies(name);
        allDeps.insert(name); // Include the resource itself

        // Build a subgraph and sort it
        DependencyGraph subgraph;
        for (const auto &nodeName : allDeps) {
            auto it = _nodes.find(nodeName);
            if (it != _nodes.end()) {
                subgraph.AddNode(nodeName, it->second.priority);
            }
        }

        // Add edges within the subgraph
        for (const auto &nodeName : allDeps) {
            auto it = _nodes.find(nodeName);
            if (it != _nodes.end()) {
                for (const auto &dep : it->second.dependencies) {
                    if (allDeps.find(dep) != allDeps.end()) {
                        subgraph.AddDependency(nodeName, dep, GetVersionConstraint(nodeName, dep));
                    }
                }
            }
        }

        return subgraph.TopologicalSort();
    }

    std::vector<std::string> DependencyGraph::GetUnloadOrder() const {
        auto loadOrder = GetLoadOrder();
        std::reverse(loadOrder.begin(), loadOrder.end());
        return loadOrder;
    }

    std::set<std::string> DependencyGraph::GetDirectDependents(const std::string &name) const {
        auto it = _nodes.find(name);
        if (it == _nodes.end()) {
            return {};
        }
        return it->second.dependents;
    }

    std::set<std::string> DependencyGraph::GetDirectDependencies(const std::string &name) const {
        auto it = _nodes.find(name);
        if (it == _nodes.end()) {
            return {};
        }
        return it->second.dependencies;
    }

    std::set<std::string> DependencyGraph::GetAllDependents(const std::string &name) const {
        std::set<std::string> result;
        CollectTransitive(name, result, false);
        return result;
    }

    std::set<std::string> DependencyGraph::GetAllDependencies(const std::string &name) const {
        std::set<std::string> result;
        CollectTransitive(name, result, true);
        return result;
    }

    bool DependencyGraph::HasNode(const std::string &name) const {
        return _nodes.find(name) != _nodes.end();
    }

    bool DependencyGraph::HasDependency(const std::string &dependent, const std::string &dependency) const {
        auto it = _nodes.find(dependent);
        if (it == _nodes.end()) {
            return false;
        }
        return it->second.dependencies.find(dependency) != it->second.dependencies.end();
    }

    std::string DependencyGraph::GetVersionConstraint(const std::string &dependent, const std::string &dependency) const {
        auto it = _nodes.find(dependent);
        if (it == _nodes.end()) {
            return "";
        }
        auto vcIt = it->second.versionConstraints.find(dependency);
        if (vcIt == it->second.versionConstraints.end()) {
            return "";
        }
        return vcIt->second;
    }

    std::vector<std::string> DependencyGraph::GetAllNodes() const {
        std::vector<std::string> result;
        result.reserve(_nodes.size());
        for (const auto &pair : _nodes) {
            result.push_back(pair.first);
        }
        return result;
    }

    size_t DependencyGraph::GetNodeCount() const {
        return _nodes.size();
    }

    void DependencyGraph::Clear() {
        _nodes.clear();
    }

    bool DependencyGraph::ValidateDependencies(std::map<std::string, std::vector<std::string>> &outMissing) const {
        outMissing.clear();
        bool allValid = true;

        for (const auto &pair : _nodes) {
            const auto &node = pair.second;
            for (const auto &dep : node.dependencies) {
                if (_nodes.find(dep) == _nodes.end()) {
                    outMissing[pair.first].push_back(dep);
                    allValid = false;
                }
            }
        }

        return allValid;
    }

    bool DependencyGraph::DetectCycle(const std::string &start, std::set<std::string> &visited, std::vector<std::string> &cycle) const {
        if (visited.find(start) != visited.end()) {
            // Found cycle, reconstruct path
            cycle.push_back(start);
            return true;
        }

        visited.insert(start);
        cycle.push_back(start);

        auto it = _nodes.find(start);
        if (it != _nodes.end()) {
            for (const auto &dep : it->second.dependencies) {
                if (DetectCycle(dep, visited, cycle)) {
                    return true;
                }
            }
        }

        cycle.pop_back();
        visited.erase(start);
        return false;
    }

    std::vector<std::string> DependencyGraph::TopologicalSort() const {
        if (_nodes.empty()) {
            return {};
        }

        // Kahn's algorithm with priority-based tie-breaking

        // Calculate in-degrees (number of dependencies)
        std::map<std::string, int> inDegree;
        for (const auto &pair : _nodes) {
            inDegree[pair.first] = static_cast<int>(pair.second.dependencies.size());
        }

        // Priority queue: (priority, name) - lower priority first
        auto cmp = [this](const std::string &a, const std::string &b) {
            auto itA = _nodes.find(a);
            auto itB = _nodes.find(b);
            if (itA == _nodes.end() || itB == _nodes.end()) {
                return a > b;
            }
            if (itA->second.priority != itB->second.priority) {
                return itA->second.priority > itB->second.priority; // Lower priority first
            }
            return a > b; // Alphabetical tie-breaker
        };
        std::priority_queue<std::string, std::vector<std::string>, decltype(cmp)> ready(cmp);

        // Start with nodes that have no dependencies
        for (const auto &pair : inDegree) {
            if (pair.second == 0) {
                ready.push(pair.first);
            }
        }

        std::vector<std::string> result;
        result.reserve(_nodes.size());

        while (!ready.empty()) {
            std::string current = ready.top();
            ready.pop();
            result.push_back(current);

            // Reduce in-degree for all dependents
            auto it = _nodes.find(current);
            if (it != _nodes.end()) {
                for (const auto &dependent : it->second.dependents) {
                    auto degIt = inDegree.find(dependent);
                    if (degIt != inDegree.end()) {
                        degIt->second--;
                        if (degIt->second == 0) {
                            ready.push(dependent);
                        }
                    }
                }
            }
        }

        // If result size doesn't match node count, there's a cycle
        if (result.size() != _nodes.size()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("DependencyGraph: Cycle detected during topological sort");
            return {};
        }

        return result;
    }

    void DependencyGraph::CollectTransitive(const std::string &name, std::set<std::string> &result, bool followDependencies) const {
        std::queue<std::string> toVisit;

        auto it = _nodes.find(name);
        if (it == _nodes.end()) {
            return;
        }

        const auto &startSet = followDependencies ? it->second.dependencies : it->second.dependents;
        for (const auto &next : startSet) {
            toVisit.push(next);
        }

        while (!toVisit.empty()) {
            std::string current = toVisit.front();
            toVisit.pop();

            if (result.find(current) != result.end()) {
                continue;
            }
            result.insert(current);

            auto nodeIt = _nodes.find(current);
            if (nodeIt != _nodes.end()) {
                const auto &nextSet = followDependencies ? nodeIt->second.dependencies : nodeIt->second.dependents;
                for (const auto &next : nextSet) {
                    if (result.find(next) == result.end()) {
                        toVisit.push(next);
                    }
                }
            }
        }
    }

} // namespace Framework::Scripting
