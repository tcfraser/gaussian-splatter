# Project goals

This repository is a learning project for building a simple Gaussian splat renderer.
The priority is to understand how each subsystem works, why it is needed, and how it fits into the renderer.
High-performance rendering is a goal, but clarity and incremental learning should guide development.

# Preferred environment and tools

- Edit code inside WSL using Neovim.
- Target a native Windows application, with MSVC as the preferred C++ compiler.
- Use NVIDIA graphics drivers and the CUDA toolkit for GPU computation and high-performance rendering work.
- Use SDL3 for window creation and input handling.
- Keep the workflow friendly to terminal-based editing; avoid requiring a particular IDE.

# Development pace and teaching approach

- Work in small, focused steps that introduce one main concept or subsystem at a time.
- Before implementing a step, briefly explain its purpose, the subsystem's role, and how it connects to what already exists.
- Explain unfamiliar terminology when it first appears. 
- Make data flow, resource ownership, and CPU/GPU responsibilities explicit when relevant.
- Prefer straightforward, readable code over clever abstractions or a large framework. 
- Keep changes small enough to read and discuss. 
- After a step, explain what changed, how to build or run it, and what observable result demonstrates that it works. State what was actually verified and any limits on verification.
- Let the user set the pace between steps.

# Implementation and verification

- Keep dependencies minimal and explain the role of each new dependency.
- Document build and run instructions as the workflow is established. 
- Do not assume compilers, CUDA, SDL3, or environment configuration are already present.
- Keep README.md limited to external dependencies, how to install them, and how to call the build script. Update it whenever a dependency is introduced or changed.

# Style
- Never break lines in the middle of a sentence, assume I will read files with line wrapping on.
