# GitHub Copilot Agent System for HiMYM

This directory contains a comprehensive agent system for the HiMYM demoscene framework, providing intelligent task routing and specialized expertise.

## 📁 Directory Structure

```
.github/copilot/
├── README.md                          # This file
├── agents/                            # Specialized agents
│   ├── director.agent.md              # Main routing agent (start here)
│   ├── shader-author.agent.md         # GLSL shader specialist
│   ├── runtime-dev.agent.md           # Demoscene runtime specialist
│   ├── mesh-graphics.agent.md         # 3D graphics specialist
│   ├── editor-dev.agent.md            # ImGui editor specialist
│   ├── music-system.agent.md          # XM/MOD music specialist
│   ├── build-system.agent.md          # CMake/compiler specialist
│   └── docs.agent.md                  # Documentation specialist
├── instructions/                      # Shared coding standards
│   ├── coding-standards.instructions.md       # C++17 conventions
│   ├── size-optimization.instructions.md      # Demoscene size techniques
│   └── opengl-patterns.instructions.md        # OpenGL usage patterns
└── skills/                            # Shared knowledge bases
    └── himym-framework/
        └── SKILL.md                   # HiMYM framework reference
```

## 🎯 How It Works

### 1. Director Agent (Entry Point)

The **Director** agent (`director.agent.md`) is your starting point. It:
- Analyzes your task
- Determines which specialized agent(s) to activate
- Routes the task to the appropriate expert(s)

**Usage**: Just ask your question naturally. The Director will automatically delegate to the right specialist.

### 2. Specialized Agents

Each agent has deep expertise in a specific domain:

| Agent | Expertise | Use For |
|-------|-----------|---------|
| **shader-author** | GLSL shaders | Writing/optimizing vertex, fragment, compute shaders |
| **runtime-dev** | Demoscene runtime | Intros, demos, animations, size optimization |
| **mesh-graphics** | 3D rendering | Mesh creation, VAO/VBO/IBO, procedural geometry |
| **editor-dev** | ImGui tools | Timeline editor, curve editor, UI development |
| **music-system** | Audio playback | XM/MOD music integration, synchronization |
| **build-system** | CMake/compilers | Build configuration, optimization flags |
| **docs** | Documentation | README, tutorials, API docs |

### 3. Shared Resources

#### Instructions
Apply coding standards automatically based on file patterns:
- **coding-standards**: C++17 conventions, naming, memory management
- **size-optimization**: Compiler flags, code patterns for small executables
- **opengl-patterns**: Function loading, rendering pipeline best practices

#### Skills
Shared knowledge bases available to all agents:
- **himym-framework**: Complete API reference for all 7 libraries

## 🚀 Quick Start

### Example Conversations

**Creating a shader:**
```
You: "Create a Phong lighting shader for 3D meshes"
Director → Routes to shader-author
Result: Complete vertex/fragment shader with lighting
```

**Building an intro:**
```
You: "I want to make a 16KB intro with raymarching"
Director → Routes to runtime-dev + shader-author
Result: Complete intro structure with size-optimized shader
```

**Adding music:**
```
You: "How do I sync visuals to XM music?"
Director → Routes to music-system
Result: Complete integration with timeline synchronization
```

**Fixing build issues:**
```
You: "CMake can't find OpenGL functions"
Director → Routes to build-system
Result: CMake fix with proper library linking
```

## 📚 Agent Capabilities

### shader-author
- GLSL syntax and optimization
- Phong/PBR lighting models
- Raymarching and SDF functions
- Shader minification for size
- Common demoscene effects

### runtime-dev
- Intro/demo structure templates
- Animation system (curves + sequences)
- Size optimization techniques
- Platform abstraction patterns
- Real-time rendering loops

### mesh-graphics
- VAO/VBO/IBO setup and rendering
- Vertex formats and attributes
- Procedural geometry (cube, sphere, torus, plane)
- Matrix transformations
- Phong lighting implementation

### editor-dev
- Dear ImGui integration
- Timeline editor with playback controls
- Curve editor with draggable points
- Scene graph hierarchical display
- Property inspector patterns

### music-system
- libxm-windows integration (C89-compatible)
- XM/MOD/S3M playback
- Music-to-visual synchronization
- Pattern-based vs. time-based sync
- Audio buffer management

### build-system
- CMake configuration (Visual Studio, Ninja, MinGW)
- Compiler optimization flags (MSVC, GCC, Clang)
- Static library management
- Size analysis and optimization
- Cross-compilation setup

### docs
- Technical writing standards
- API documentation templates
- Tutorial structure
- README best practices
- Markdown formatting

## 🎨 Framework Libraries

The HiMYM framework consists of 7 modular libraries:

| Library | Purpose | Size Impact |
|---------|---------|-------------|
| **rev_platform** | Window, OpenGL context, input | Essential |
| **rev_shader** | GLSL shader compilation | Essential |
| **rev_xm** | XM/MOD music playback | +2-4 KB |
| **rev_curve** | Bézier curve animations | +1-2 KB |
| **rev_sequence** | Timeline/cue management | +1 KB |
| **rev_editor** | ImGui visual tools | Editor only |
| **rev_mesh** | 3D mesh rendering | +2-3 KB |

**Typical intro sizes:**
- Minimal (shader only): 16 KB
- Animated (shader + curves): 20 KB
- With music (shader + XM): 24 KB
- Full intro: 30-64 KB

## 🔧 Customization

### Adding New Agents

1. Create `new-agent.agent.md` in `agents/` directory
2. Add YAML frontmatter with name, description, applyTo, allowedTools
3. Document expertise and provide examples
4. Update `director.agent.md` to include the new agent in routing logic

### Creating New Instructions

1. Create `new-topic.instructions.md` in `instructions/` directory
2. Add YAML frontmatter with applyTo patterns
3. Document standards, patterns, and best practices
4. Files automatically apply based on applyTo glob patterns

### Adding New Skills

1. Create subdirectory in `skills/` (e.g., `skills/my-skill/`)
2. Create `SKILL.md` with YAML frontmatter
3. Document knowledge, APIs, examples
4. Skills are available to all agents

## 📖 Best Practices

### For Users

1. **Be specific**: "Create a Phong shader" vs. "Make it look nice"
2. **Mention constraints**: "For a 16KB intro" helps agents optimize
3. **State the goal**: "I want to render a rotating cube with lighting"
4. **Trust the routing**: Director will find the right expert(s)

### For Agent Development

1. **Stay focused**: Each agent should have a clear domain
2. **Provide examples**: Show working code, not just theory
3. **Consider size**: Always mention size impact for demoscene work
4. **Cross-reference**: Link to related agents/skills when relevant

## 🐛 Troubleshooting

### Agent not activating?
- Check `applyTo` patterns in agent frontmatter
- Verify file paths match the patterns
- Try explicitly mentioning the agent: "Use shader-author to..."

### Wrong agent selected?
- Provide more context in your query
- Mention the specific library: "Using rev_mesh..."
- Reference the desired agent directly

### Need multiple agents?
- Director can activate multiple agents for complex tasks
- Example: "Create a music-synchronized mesh animation" → runtime-dev + mesh-graphics + music-system

## 📝 Notes

- **C++17 only**: No C++20/23 features
- **Windows target**: Primary platform is Windows 10/11
- **OpenGL 3.3**: Core profile, no compatibility mode
- **libxm-windows**: Uses C89-compatible fork (not original libxm)
- **Size matters**: All code decisions consider executable size

## 🤝 Contributing

When adding agents, instructions, or skills:
1. Follow existing structure and naming conventions
2. Test with real scenarios
3. Document with examples
4. Consider size impact for demoscene priorities

## 📄 License

Same license as the HiMYM framework project.

---

**Created**: 2026
**Framework**: HiMYM (How I Met Your Mother) demoscene framework
**Purpose**: Intelligent coding assistance for demoscene development
