/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/states/machine.h"
#include <atomic>
#include <chrono>

// Test states
class InitialState: public Framework::Utils::States::IState {
public:
    const char *GetName() const override {
        return "Initial";
    }
    int32_t GetId() const override { return 1; }
    bool OnEnter(Framework::Utils::States::Machine* machine) override { return true; }
    bool OnUpdate(Framework::Utils::States::Machine* machine) override { return false; }
    bool OnExit(Framework::Utils::States::Machine* machine) override { return true; }
};

class ProcessingState: public Framework::Utils::States::IState {
private:
    static std::atomic<int> _counter;
public:
    static void ResetCounter() { _counter = 0; }
    static int GetCounter() { return _counter.load(); }

   const char *GetName() const override {
        return "Processing";
    }
    int32_t GetId() const override { return 2; }
    bool OnEnter(Framework::Utils::States::Machine* machine) override { 
        _counter++; 
        return true; 
    }
    bool OnUpdate(Framework::Utils::States::Machine* machine) override { return false; }
    bool OnExit(Framework::Utils::States::Machine* machine) override { return true; }
};

class FailingState: public Framework::Utils::States::IState {
private:
    static std::atomic<int> _failures;
public:
    static void ResetFailures() { _failures = 0; }
    static int GetFailures() { return _failures.load(); }

    const char *GetName() const override {
        return "Failing";
    }
    int32_t GetId() const override { return 3; }
    bool OnEnter(Framework::Utils::States::Machine* machine) override { return true; }
    bool OnUpdate(Framework::Utils::States::Machine* machine) override { 
        _failures++;
        return true; 
    }
    bool OnExit(Framework::Utils::States::Machine* machine) override { return true; }
};

// Initialize static members
std::atomic<int> ProcessingState::_counter(0);
std::atomic<int> FailingState::_failures(0);

MODULE(state_machine, {
    using namespace Framework::Utils::States;

    IT("can register and transition between states", {
        auto machine = std::make_unique<Machine>();
        machine->RegisterState<InitialState>();
        
        EQUALS(machine->GetCurrentState(), nullptr);
        
        // Request initial state
        EQUALS(machine->RequestNextState(1), true);
        machine->Update(); // Enter InitialState
        
        auto state = machine->GetCurrentState();
        EQUALS(state != nullptr, true);
        EQUALS(state->GetId(), 1);
        EQUALS(state->GetName(), "Initial");
    });

    IT("handles invalid state transitions gracefully", {
        auto machine = std::make_unique<Machine>();
        machine->RegisterState<InitialState>();
        
        // Try to request non-existent state
        EQUALS(machine->RequestNextState(999), false);
        
        // Request valid state
        EQUALS(machine->RequestNextState(1), true);
        
        // Try to request another state while transition is pending
        EQUALS(machine->RequestNextState(1), false);
    });

    IT("executes state lifecycle correctly", {
        auto machine = std::make_unique<Machine>();
        ProcessingState::ResetCounter();
        
        machine->RegisterState<InitialState>();
        machine->RegisterState<ProcessingState>();
        
        // Start Initial state
        EQUALS(machine->RequestNextState(1), true);
        machine->Update(); // Enter
        EQUALS(machine->GetCurrentState()->GetId(), 1);
        
        // Request transition while in Initial state
        EQUALS(machine->RequestNextState(2), true);
        machine->Update(); // Should move to Exit state since we requested next
        machine->Update(); // Should move to Next state
        machine->Update(); // Should Enter ProcessingState
        
        EQUALS(ProcessingState::GetCounter(), 1);
        EQUALS(machine->GetCurrentState()->GetId(), 2);
    });

    IT("handles failing states properly", {
        auto machine = std::make_unique<Machine>();
        FailingState::ResetFailures();
        
        machine->RegisterState<InitialState>();
        machine->RegisterState<FailingState>();
        
        // Initial state
        EQUALS(machine->RequestNextState(1), true);
        machine->Update(); // Enter
        machine->Update(); // Update
        
        // Transition to failing state
        EQUALS(machine->RequestNextState(3), true);
        machine->Update(); // Exit Initial
        machine->Update(); // Enter Failing
        machine->Update(); // Update Failing (should increment counter and request exit)
        machine->Update(); // Exit Failing
        
        EQUALS(FailingState::GetFailures(), 1);
    });

    IT("can handle rapid state transitions", {
        auto machine = std::make_unique<Machine>();
        ProcessingState::ResetCounter();
        
        machine->RegisterState<InitialState>();
        machine->RegisterState<ProcessingState>();
        
        // Perform rapid transitions
        for(int i = 0; i < 1000; i++) {
            machine->RequestNextState(1);
            machine->Update(); // Enter
            machine->Update(); // Update
            
            machine->RequestNextState(2);
            machine->Update(); // Exit Initial
            machine->Update(); // Enter Processing
            machine->Update(); // Update Processing
        }
        
        EQUALS(ProcessingState::GetCounter(), 1000);
    });

    IT("maintains thread safety under concurrent access", {
        auto machine = std::make_unique<Machine>();
        ProcessingState::ResetCounter();
        std::atomic<bool> running = true;
        
        machine->RegisterState<InitialState>();
        machine->RegisterState<ProcessingState>();
        
        // Thread constantly updating the state machine
        std::thread updater([&]() {
            while(running) {
                machine->Update();
                std::this_thread::yield();
            }
        });
        
        // Thread requesting state transitions
        std::thread requester([&]() {
            for(int i = 0; i < 100; i++) {
                machine->RequestNextState(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                machine->RequestNextState(2);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        
        requester.join();
        running = false;
        updater.join();
        
        EQUALS(ProcessingState::GetCounter() > 0, true);
    });

    IT("handles recursive state updates safely", {
        auto machine = std::make_unique<Machine>();
        ProcessingState::ResetCounter();

        // Create a state that tries to perform state machine operations during callbacks
        class RecursiveState: public Framework::Utils::States::IState {
          public:
            const char *GetName() const override {
                return "Recursive";
            }
            int32_t GetId() const override {
                return 4;
            }
            bool OnEnter(Machine *machine) override {
                // Try recursive update
                machine->Update();
                return true;
            }
            bool OnUpdate(Machine *machine) override {
                return true;
            }
            bool OnExit(Machine *machine) override {
                return true;
            }
        };

        machine->RegisterState<ProcessingState>();
        machine->RegisterState<RecursiveState>();

        // This should not deadlock
        EQUALS(machine->RequestNextState(4), true);
        machine->Update();
        machine->Update();
    });
});
