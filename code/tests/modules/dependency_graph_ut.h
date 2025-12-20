/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/resource/dependency_graph.h"

MODULE(dependency_graph, {
    using namespace Framework::Scripting;

    // ==================== Node Operations ====================

    IT("can add and check nodes", {
        DependencyGraph graph;

        auto result = graph.AddNode("resource_a");
        EQUALS(result.success, true);
        EQUALS(graph.HasNode("resource_a"), true);
        EQUALS(graph.GetNodeCount(), 1u);

        result = graph.AddNode("resource_b", 10);
        EQUALS(result.success, true);
        EQUALS(graph.HasNode("resource_b"), true);
        EQUALS(graph.GetNodeCount(), 2u);
    });

    IT("rejects duplicate node names", {
        DependencyGraph graph;

        auto result = graph.AddNode("resource_a");
        EQUALS(result.success, true);

        result = graph.AddNode("resource_a");
        EQUALS(result.success, false);
        EQUALS(graph.GetNodeCount(), 1u);
    });

    IT("can remove nodes", {
        DependencyGraph graph;
        graph.AddNode("resource_a");
        graph.AddNode("resource_b");

        EQUALS(graph.GetNodeCount(), 2u);

        auto result = graph.RemoveNode("resource_a");
        EQUALS(result.success, true);
        EQUALS(graph.HasNode("resource_a"), false);
        EQUALS(graph.GetNodeCount(), 1u);
    });

    IT("removes edges when removing a node", {
        DependencyGraph graph;
        graph.AddNode("resource_a");
        graph.AddNode("resource_b");
        graph.AddDependency("resource_b", "resource_a");

        EQUALS(graph.HasDependency("resource_b", "resource_a"), true);

        graph.RemoveNode("resource_a");

        EQUALS(graph.HasDependency("resource_b", "resource_a"), false);
    });

    IT("can get all node names", {
        DependencyGraph graph;
        graph.AddNode("alpha");
        graph.AddNode("beta");
        graph.AddNode("gamma");

        auto nodes = graph.GetAllNodes();
        EQUALS(nodes.size(), 3u);
    });

    IT("can clear all nodes", {
        DependencyGraph graph;
        graph.AddNode("resource_a");
        graph.AddNode("resource_b");
        graph.AddDependency("resource_b", "resource_a");

        graph.Clear();

        EQUALS(graph.GetNodeCount(), 0u);
        EQUALS(graph.HasNode("resource_a"), false);
    });

    // ==================== Dependency Edge Operations ====================

    IT("can add and check dependencies", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("game");

        auto result = graph.AddDependency("game", "core");
        EQUALS(result.success, true);
        EQUALS(graph.HasDependency("game", "core"), true);
        EQUALS(graph.HasDependency("core", "game"), false);
    });

    IT("can add dependencies with version constraints", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("game");

        auto result = graph.AddDependency("game", "core", ">=1.0.0");
        EQUALS(result.success, true);

        auto constraint = graph.GetVersionConstraint("game", "core");
        STREQUALS(constraint.c_str(), ">=1.0.0");
    });

    IT("rejects dependency to non-existent node", {
        DependencyGraph graph;
        graph.AddNode("game");

        auto result = graph.AddDependency("game", "nonexistent");
        EQUALS(result.success, false);
    });

    IT("rejects dependency from non-existent node", {
        DependencyGraph graph;
        graph.AddNode("core");

        auto result = graph.AddDependency("nonexistent", "core");
        EQUALS(result.success, false);
    });

    IT("can remove dependencies", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("game");
        graph.AddDependency("game", "core");

        EQUALS(graph.HasDependency("game", "core"), true);

        auto result = graph.RemoveDependency("game", "core");
        EQUALS(result.success, true);
        EQUALS(graph.HasDependency("game", "core"), false);
    });

    IT("tracks direct dependents correctly", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("game");
        graph.AddNode("ui");
        graph.AddDependency("game", "core");
        graph.AddDependency("ui", "core");

        auto dependents = graph.GetDirectDependents("core");
        EQUALS(dependents.size(), 2u);
        EQUALS(dependents.count("game"), 1u);
        EQUALS(dependents.count("ui"), 1u);
    });

    IT("tracks direct dependencies correctly", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("utils");
        graph.AddNode("game");
        graph.AddDependency("game", "core");
        graph.AddDependency("game", "utils");

        auto deps = graph.GetDirectDependencies("game");
        EQUALS(deps.size(), 2u);
        EQUALS(deps.count("core"), 1u);
        EQUALS(deps.count("utils"), 1u);
    });

    // ==================== Cycle Detection ====================

    IT("detects simple cycles", {
        DependencyGraph graph;
        graph.AddNode("a");
        graph.AddNode("b");

        graph.AddDependency("a", "b");

        EQUALS(graph.WouldCreateCycle("b", "a"), true);

        auto result = graph.AddDependency("b", "a");
        EQUALS(result.success, false);
        NEQUALS(result.cycle.size(), 0u);
    });

    IT("detects transitive cycles", {
        DependencyGraph graph;
        graph.AddNode("a");
        graph.AddNode("b");
        graph.AddNode("c");

        graph.AddDependency("a", "b");
        graph.AddDependency("b", "c");

        EQUALS(graph.WouldCreateCycle("c", "a"), true);

        auto result = graph.AddDependency("c", "a");
        EQUALS(result.success, false);
    });

    IT("detects self-dependency as cycle", {
        DependencyGraph graph;
        graph.AddNode("a");

        EQUALS(graph.WouldCreateCycle("a", "a"), true);

        auto result = graph.AddDependency("a", "a");
        EQUALS(result.success, false);
    });

    IT("allows valid non-cyclic dependencies", {
        DependencyGraph graph;
        graph.AddNode("a");
        graph.AddNode("b");
        graph.AddNode("c");

        // Diamond dependency: a->b, a->c, b->c (no cycle)
        auto result = graph.AddDependency("a", "b");
        EQUALS(result.success, true);

        result = graph.AddDependency("a", "c");
        EQUALS(result.success, true);

        result = graph.AddDependency("b", "c");
        EQUALS(result.success, true);

        EQUALS(graph.WouldCreateCycle("c", "a"), true);
        EQUALS(graph.WouldCreateCycle("c", "b"), true);
    });

    // ==================== Load Order (Topological Sort) ====================

    IT("returns correct load order for linear chain", {
        DependencyGraph graph;
        graph.AddNode("a");
        graph.AddNode("b");
        graph.AddNode("c");

        // c depends on b, b depends on a
        graph.AddDependency("c", "b");
        graph.AddDependency("b", "a");

        auto order = graph.GetLoadOrder();
        EQUALS(order.size(), 3u);

        // a must come before b, b must come before c
        size_t posA = 0, posB = 0, posC = 0;
        for (size_t i = 0; i < order.size(); i++) {
            if (order[i] == "a") posA = i;
            if (order[i] == "b") posB = i;
            if (order[i] == "c") posC = i;
        }
        EQUALS(posA < posB, true);
        EQUALS(posB < posC, true);
    });

    IT("returns correct load order for diamond dependency", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("utils");
        graph.AddNode("network");
        graph.AddNode("game");

        // game depends on utils and network
        // utils and network both depend on core
        graph.AddDependency("game", "utils");
        graph.AddDependency("game", "network");
        graph.AddDependency("utils", "core");
        graph.AddDependency("network", "core");

        auto order = graph.GetLoadOrder();
        EQUALS(order.size(), 4u);

        size_t posCore = 0, posUtils = 0, posNetwork = 0, posGame = 0;
        for (size_t i = 0; i < order.size(); i++) {
            if (order[i] == "core") posCore = i;
            if (order[i] == "utils") posUtils = i;
            if (order[i] == "network") posNetwork = i;
            if (order[i] == "game") posGame = i;
        }

        // core must come first
        EQUALS(posCore < posUtils, true);
        EQUALS(posCore < posNetwork, true);
        // game must come last
        EQUALS(posUtils < posGame, true);
        EQUALS(posNetwork < posGame, true);
    });

    IT("respects priority for independent nodes", {
        DependencyGraph graph;
        graph.AddNode("low_priority", 0);
        graph.AddNode("high_priority", 100);

        auto order = graph.GetLoadOrder();
        EQUALS(order.size(), 2u);

        // Lower priority should load first
        size_t posLow = 0, posHigh = 0;
        for (size_t i = 0; i < order.size(); i++) {
            if (order[i] == "low_priority") posLow = i;
            if (order[i] == "high_priority") posHigh = i;
        }
        EQUALS(posLow < posHigh, true);
    });

    IT("returns correct unload order (reverse of load)", {
        DependencyGraph graph;
        graph.AddNode("a");
        graph.AddNode("b");
        graph.AddNode("c");

        graph.AddDependency("c", "b");
        graph.AddDependency("b", "a");

        auto loadOrder = graph.GetLoadOrder();
        auto unloadOrder = graph.GetUnloadOrder();

        EQUALS(loadOrder.size(), unloadOrder.size());

        // Unload order should be reverse
        for (size_t i = 0; i < loadOrder.size(); i++) {
            STREQUALS(loadOrder[i].c_str(), unloadOrder[loadOrder.size() - 1 - i].c_str());
        }
    });

    IT("returns load order for specific resource", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("utils");
        graph.AddNode("game");
        graph.AddNode("unrelated");

        graph.AddDependency("game", "utils");
        graph.AddDependency("utils", "core");

        auto order = graph.GetLoadOrderFor("game");

        // Should include core, utils, game but not unrelated
        EQUALS(order.size(), 3u);

        bool hasCore = false, hasUtils = false, hasGame = false, hasUnrelated = false;
        for (const auto &name : order) {
            if (name == "core") hasCore = true;
            if (name == "utils") hasUtils = true;
            if (name == "game") hasGame = true;
            if (name == "unrelated") hasUnrelated = true;
        }
        EQUALS(hasCore, true);
        EQUALS(hasUtils, true);
        EQUALS(hasGame, true);
        EQUALS(hasUnrelated, false);
    });

    // ==================== Transitive Dependencies ====================

    IT("gets all transitive dependents", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("utils");
        graph.AddNode("game");

        graph.AddDependency("utils", "core");
        graph.AddDependency("game", "utils");

        auto allDependents = graph.GetAllDependents("core");
        EQUALS(allDependents.size(), 2u);
        EQUALS(allDependents.count("utils"), 1u);
        EQUALS(allDependents.count("game"), 1u);
    });

    IT("gets all transitive dependencies", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("utils");
        graph.AddNode("game");

        graph.AddDependency("utils", "core");
        graph.AddDependency("game", "utils");

        auto allDeps = graph.GetAllDependencies("game");
        EQUALS(allDeps.size(), 2u);
        EQUALS(allDeps.count("utils"), 1u);
        EQUALS(allDeps.count("core"), 1u);
    });

    // ==================== Validation ====================

    IT("validates all dependencies satisfied", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("game");
        graph.AddDependency("game", "core");

        std::map<std::string, std::vector<std::string>> missing;
        bool valid = graph.ValidateDependencies(missing);
        EQUALS(valid, true);
        EQUALS(missing.empty(), true);
    });

    IT("validation passes for graph with no dangling references", {
        DependencyGraph graph;
        graph.AddNode("core");
        graph.AddNode("game");
        graph.AddDependency("game", "core");

        // Remove the dependency edge, then the node - should be clean
        graph.RemoveDependency("game", "core");
        graph.RemoveNode("core");

        std::map<std::string, std::vector<std::string>> missing;
        bool valid = graph.ValidateDependencies(missing);
        EQUALS(valid, true);
        EQUALS(missing.empty(), true);
    });

    // ==================== Edge Cases ====================

    IT("handles empty graph", {
        DependencyGraph graph;

        EQUALS(graph.GetNodeCount(), 0u);
        EQUALS(graph.GetAllNodes().empty(), true);
        EQUALS(graph.GetLoadOrder().empty(), true);
        EQUALS(graph.HasNode("anything"), false);
    });

    IT("handles single node graph", {
        DependencyGraph graph;
        graph.AddNode("solo");

        auto order = graph.GetLoadOrder();
        EQUALS(order.size(), 1u);
        STREQUALS(order[0].c_str(), "solo");

        auto deps = graph.GetDirectDependencies("solo");
        EQUALS(deps.empty(), true);

        auto dependents = graph.GetDirectDependents("solo");
        EQUALS(dependents.empty(), true);
    });

    IT("handles complex dependency web", {
        DependencyGraph graph;

        // Create a realistic scenario:
        // core <- utils <- network <- game
        //      <- ui     <- hud     <-
        graph.AddNode("core", 0);
        graph.AddNode("utils", 1);
        graph.AddNode("ui", 1);
        graph.AddNode("network", 2);
        graph.AddNode("hud", 2);
        graph.AddNode("game", 3);

        graph.AddDependency("utils", "core");
        graph.AddDependency("ui", "core");
        graph.AddDependency("network", "utils");
        graph.AddDependency("hud", "ui");
        graph.AddDependency("game", "network");
        graph.AddDependency("game", "hud");

        auto order = graph.GetLoadOrder();
        EQUALS(order.size(), 6u);

        // Verify core is first
        STREQUALS(order[0].c_str(), "core");

        // Verify game is last
        STREQUALS(order[5].c_str(), "game");

        // Verify no cycles were introduced
        EQUALS(graph.WouldCreateCycle("core", "game"), true);
    });
})
