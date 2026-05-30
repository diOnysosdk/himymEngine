# HowIMetYourMod - Complete Documentation Package

**Revision 2026 Intro/Demo Runtime & Scene Editor**

This folder contains all documentation, guides, and AI customizations for HowIMetYourMod, plus a **refactored C++ architecture** for building modern intro/demo frameworks.

## 📌 Two Architectures Documented

### **Original** (This Codebase)
- **Runtime**: C++ (Win32/WGL/OpenGL 3.3)
- **Editor**: Python 3.11 + tkinter
- **Status**: Working, tested, production-ready
- **Docs**: [FROM_SCRATCH.md](FROM_SCRATCH.md), [QUICK_START.md](QUICK_START.md)

### **Refactored** (Proposed for New Projects) **NEW!**
- **Runtime**: C++ modular libraries
- **Editor**: C++ + Dear ImGui
- **Status**: Design document + lessons learned
- **Docs**: [FROM_SCRATCH_V2.md](FROM_SCRATCH_V2.md), [LIBRARY_DESIGN.md](LIBRARY_DESIGN.md)
- **Benefits**: Single language, better performance, reusable components

## 📁 Documentation Structure

### Core Documentation
- **[GETTING_STARTED_NEW_PROJECT.md](GETTING_STARTED_NEW_PROJECT.md)** - **NEW!** How to start fresh with Copilot 🚀
- **[ROADMAP.md](ROADMAP.md)** - **NEW!** Week-by-week implementation plan with testable milestones
- **[REFACTORING_NOTES.md](REFACTORING_NOTES.md)** - **NEW!** Why C++ > Python, migration guide, performance comparison
- **[FROM_SCRATCH_V2.md](FROM_SCRATCH_V2.md)** - **NEW!** Modern C++ architecture guide (all C++, modular libraries)
- **[FROM_SCRATCH.md](FROM_SCRATCH.md)** - Original Python/tkinter design (reference implementation)
- **[LIBRARY_DESIGN.md](LIBRARY_DESIGN.md)** - **NEW!** Reusable C++ library specifications (7 core libraries)
- **[QUICK_START.md](QUICK_START.md)** - Get up and running in 5 minutes (Python version)
- **[SUMMARY.md](SUMMARY.md)** - Documentation inventory
- **[ARCHITECTURE.md](architecture/ARCHITECTURE.md)** - System architecture (Python version)
- **[API_REFERENCE.md](architecture/API_REFERENCE.md)** - Complete API documentation
- **[TECH_STACK.md](architecture/TECH_STACK.md)** - Technology stack overview

### User Guides
- **[EDITOR_GUIDE.md](guides/EDITOR_GUIDE.md)** - Scene Block Editor walkthrough
- **[SHADER_GUIDE.md](guides/SHADER_GUIDE.md)** - Shader authoring guide
- **[CURVE_SYSTEM_GUIDE.md](guides/CURVE_SYSTEM_GUIDE.md)** - Curve system reference
- **[3D_STAGE_GUIDE.md](guides/3D_STAGE_GUIDE.md)** - Optional 3D rendering guide
- **[CONTROLS_KNOBS.md](guides/CONTROLS_KNOBS.md)** - All tuning knobs reference

### AI Customizations
- **[agents/](ai/agents/)** - Specialized AI agent definitions
- **[skills/](ai/skills/)** - Reusable AI skill modules
- **[instructions/](ai/instructions/)** - File-specific instructions
- **[prompts/](ai/prompts/)** - Legacy prompt wrappers

### Tools & Utilities
- **[SHADER_TOOLS.md](tools/SHADER_TOOLS.md)** - shader_scene_duplicator.py, shader_id_finder.py
- **[PYTHON_UTILITIES.md](tools/PYTHON_UTILITIES.md)** - OBJ/MTL converters, text baking
- **[BUILD_SYSTEM.md](tools/BUILD_SYSTEM.md)** - CMake workflow and optimization

### Project Context
- **[PROJECT_GUIDELINES.md](context/PROJECT_GUIDELINES.md)** - Development rules (from AGENTS.md)
- **[OPENGL_EXPLAINER.md](context/OPENGL_EXPLAINER.md)** - WGL bootstrap & rendering
- **[EXTENSION_GUIDE.md](context/EXTENSION_GUIDE.md)** - How to extend the system

## 🚀 Quick Navigation

### I want to...
- **Start a NEW project with Copilot** → [GETTING_STARTED_NEW_PROJECT.md](GETTING_STARTED_NEW_PROJECT.md) 🎯 **START HERE!**
- **Build a NEW C++ intro** → [ROADMAP.md](ROADMAP.md) (week-by-week plan) → [LIBRARY_DESIGN.md](LIBRARY_DESIGN.md)
- **Understand the new architecture** → [FROM_SCRATCH_V2.md](FROM_SCRATCH_V2.md) + [REFACTORING_NOTES.md](REFACTORING_NOTES.md)
- **Build THIS intro** (Python editor) → [QUICK_START.md](QUICK_START.md) → Build section
- **Author shaders** → [SHADER_GUIDE.md](guides/SHADER_GUIDE.md) (works for both architectures)
- **Use the Python editor** → [EDITOR_GUIDE.md](guides/EDITOR_GUIDE.md)
- **Understand Python version** → [ARCHITECTURE.md](architecture/ARCHITECTURE.md) + [FROM_SCRATCH.md](FROM_SCRATCH.md)
- **Learn from mistakes** → [REFACTORING_NOTES.md](REFACTORING_NOTES.md) → Performance & Size sections
- **Configure AI agents** → [ai/README.md](ai/README.md)

## 🎯 Target Audience

- **Demoscene coders** - Windows intro/demo production (original OR refactored architecture)
- **Graphics programmers** - OpenGL shader work, modular rendering libraries
- **Tool developers** - Python/tkinter OR C++/ImGui authoring tools
- **AI developers** - GitHub Copilot workspace customization
- **Framework builders** - Reusable C++ libraries for future projects (**see LIBRARY_DESIGN.md**)

## 📝 License & Credits

This is a Revision 2026 PC production targeting:
- **Platform**: Windows 11 64-bit
- **Resolution**: 1920x1080 @ 60 Hz
- **Profile**: Demo (no size limit) or Intro (64k/128k/256k)

Developed with GitHub Copilot and Claude Sonnet 4.5 assistance.

---

**Last Updated**: May 30, 2026  
**Version**: 1.0  
**Commit**: See git log for latest changes
