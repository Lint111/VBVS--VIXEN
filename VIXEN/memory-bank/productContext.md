# Product Context

## Why This Project Exists

This project exists to provide a structured, hands-on learning path for mastering Vulkan graphics programming. Unlike tutorials that abstract away complexity, this project builds understanding from the ground up by implementing every step of the Vulkan initialization pipeline.

## Problem It Solves

### Learning Challenge
Vulkan has a steep learning curve due to its explicit, low-level nature. Many developers struggle to:
- Understand the initialization sequence
- Manage the verbose API correctly
- Handle resources and memory properly
- Debug validation layer errors
- Bridge the gap between theory and practice

### Solution Approach
This project provides:
- **Step-by-step implementation** following chapter progression
- **Clear architecture** with separation of concerns
- **Proper error handling** and validation from the start
- **Well-documented code** with explanatory comments
- **Foundation for expansion** - each chapter builds on the previous

## How It Should Work

### User Experience (Developer Learning Journey)

1. **Chapter-by-Chapter Progression**
   - Each chapter introduces new Vulkan concepts
   - Code builds incrementally
   - Previous chapters remain as reference

2. **Clear Code Organization**
   - Each Vulkan subsystem has its own class
   - Responsibilities are well-defined
   - Easy to locate and understand specific functionality

3. **Immediate Feedback**
   - Validation layers provide detailed error information
   - Build system catches errors early
   - Code runs and produces visible results

4. **Reference Implementation**
   - Can be used as template for future projects
   - Demonstrates best practices
   - Shows correct API usage patterns

## Current Chapter Focus: Device Handshake

### What Users Should Experience

Running the application should:
1. Initialize Vulkan instance successfully
2. Enumerate available physical devices (GPUs)
3. Display device information (optional, for learning)
4. Select appropriate physical device
5. Create logical device with required queues
6. Exit cleanly with proper resource destruction

### Expected Output
- Console output showing initialization steps
- Validation layer messages (if enabled)
- Successful completion or clear error messages
- No memory leaks or resource leaks

## Design Philosophy

### Explicitness Over Convenience
- Show all Vulkan calls explicitly
- Don't hide complexity in helper functions too early
- Make the learning visible in the code

### Correctness Over Performance
- Prioritize proper API usage
- Implement validation and error checking
- Performance optimization comes later

### Incrementalism Over Completeness
- Build working code at each chapter
- Each addition is testable
- Refactor as understanding grows

## Future Vision

As chapters progress, this project should evolve into:
- A rendering engine with swapchain and presentation
- Shader pipeline implementation
- Texture and buffer management
- Advanced Vulkan features (compute shaders, ray tracing, etc.)

Each addition maintains the core philosophy: clear, correct, educational code.
