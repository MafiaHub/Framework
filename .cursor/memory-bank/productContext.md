# MafiaHub Framework Product Context

## Purpose
The MafiaHub Framework exists to solve the common challenges faced when developing multi-player modifications for AAA games. Instead of each modification project recreating similar systems from scratch, this framework provides a unified foundation that can be leveraged across multiple game projects.

## Problems Solved

### Fragmentation and Duplication
Before this framework, each multi-player modification project would create its own networking, entity management, and other core systems. This resulted in duplicated effort, inconsistent implementations, and difficulty in sharing improvements between projects.

### Technical Complexity
Implementing robust networking, entity synchronization, and scripting systems is complex and time-consuming. The framework simplifies this by providing ready-to-use components that handle these complexities.

### Cross-Platform Support
Supporting multiple platforms (Windows, Linux, MacOS) requires specialized knowledge and significant testing. The framework abstracts platform-specific implementations, allowing developers to focus on game-specific features.

### Maintenance Burden
By centralizing core functionality, bugs can be fixed once in the framework rather than in each individual project. This significantly reduces the maintenance burden on modification developers.

## How It Should Work

### Component-Based Architecture
The framework uses a modular approach where developers can select and integrate the components they need. This flexibility allows it to support a wide range of game modification types.

### Extension Points
While providing robust defaults, the framework is designed to be extended. Game-specific functionality can be added without modifying the core framework code.

### Development Workflow
1. Developers clone the framework
2. They add their game-specific modification to the `code/projects` directory
3. The framework automatically integrates the modification and provides access to all framework features
4. Developers focus on implementing game-specific logic using the framework's APIs

### Runtime Behavior
- The framework initializes core systems (networking, ECS, scripting)
- Game-specific code registers entities, components, and systems
- Network synchronization happens automatically for registered components
- Lua scripting allows for runtime modification of game behavior

## User Experience Goals

### For Modification Developers
- Reduce development time by leveraging pre-built components
- Provide clear, consistent APIs for common game modification tasks
- Enable focus on game-specific features rather than infrastructure
- Facilitate collaboration through shared code and standardized patterns

### For End Users (Players)
- Consistent experience across different game modifications
- Better performance through optimized, shared code
- Reduced bugs through well-tested core components
- Support for a wider range of platforms 
