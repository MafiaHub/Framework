# MafiaHub Framework System Patterns

## Architecture Overview

The MafiaHub Framework follows a modular architecture with several key subsystems that can be used together or independently based on the needs of a specific game modification project.

```
┌─────────────────────────────────────────────────────────────┐
│                    Game Modification                         │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│                    Framework Integration                     │
└───────────┬───────────────┬────────────────┬────────────────┘
            │               │                │
┌───────────▼───────┐ ┌─────▼──────┐ ┌───────▼──────────┐
│    Networking     │ │     ECS    │ │     Scripting    │
└───────────────────┘ └────────────┘ └──────────────────┘
            │               │                │
┌───────────▼───────┐ ┌─────▼──────┐ ┌───────▼──────────┐
│      Logging      │ │     GUI    │ │      Utils       │
└───────────────────┘ └────────────┘ └──────────────────┘
```

## Key Design Patterns

### Service Locator Pattern
The framework uses a service locator pattern to manage access to core subsystems. This allows components to be easily located and used throughout the codebase without tight coupling.

### Entity-Component-System (ECS)
The world and entity management follows the ECS pattern:
- **Entities**: Basic containers with unique IDs
- **Components**: Data holders attached to entities
- **Systems**: Logic that operates on components of specific types

### Observer Pattern
The framework implements event systems using the observer pattern, allowing components to subscribe to and react to events without direct dependencies.

### Factory Pattern
Various factory methods are used to create entities, components, and other objects, ensuring proper initialization and registration.

### Singleton Pattern
Some core managers and services use the singleton pattern for global access where appropriate, though this is limited to cases where a single instance is logically required.

## Component Relationships

### Networking Subsystem
- Uses a client-server model with reliable and unreliable channels
- Handles connection management, packet serialization, and synchronization
- Integrates with the ECS system for entity replication

### ECS Subsystem
- Manages entities, components, and systems
- Provides mechanisms for component registration and entity creation
- Supports serialization for network transmission and persistence

### Scripting Subsystem
- Exposes framework functionality to JavaScript/TypeScript scripts
- Provides hooks for game-specific logic
- Manages script execution and error handling

### GUI Subsystem
- Provides rendering capabilities using Ultralight and DearImGUI
- Supports both HTML/CSS-based UI and immediate-mode GUI
- Handles input and event propagation

### Logging Subsystem
- Provides structured logging capabilities
- Supports multiple output channels (console, file, remote)
- Includes error tracking and reporting via Sentry

## Data Flow
1. Input is captured from the user
2. Input is processed by game-specific code
3. Changes are made to entity components
4. Systems process the updated components
5. Changes are synchronized over the network
6. Remote changes are applied to local entities
7. Updated state is rendered to the screen

## Extension Mechanisms
- Custom components can be registered to extend entity capabilities
- New systems can be added to implement game-specific logic
- Scripts can modify behavior at runtime
- Event handlers can be registered for custom event processing

## Best Practices
- Use the service locator to access subsystems
- Register components and systems during initialization
- Use events for loose coupling between subsystems
- Leverage the ECS pattern for game objects
- Use scripting for dynamic, game-specific logic 
