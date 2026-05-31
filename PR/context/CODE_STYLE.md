# Code Style — himym

Lean, modular C++17. Easy to add features, easy to rip out. No surprises.

---

## The One Rule

**Every lib, struct, and function should be removable without touching anything else.**
If removing a lib requires editing five other files, it's too coupled.

---

## Library layout

Each feature lives in `revision_libs/rev_<name>/`:

```
revision_libs/rev_foo/
    include/rev_foo.h     ← public API only, opaque structs, no implementation details
    src/foo_impl.cpp      ← all implementation in here
    CMakeLists.txt        ← builds as a static lib, links what it needs
```

**Rules:**
- One header in `include/`. That's the entire contract.
- Opaque context structs: users hold a pointer, never embed a struct they don't own.
- Libs do not include each other's headers transitively — only what they directly use.
- A new lib = new folder + add to root `CMakeLists.txt`. Nothing else changes.

---

## Naming

```cpp
// Namespaces: two levels, lowercase
namespace rev { namespace shader { ... } }

// Structs/types: PascalCase
struct ShaderCue { ... };
struct WindowConfig { ... };

// Functions: PascalCase
Program* CompileFromSource(...);
void DestroyProgram(Program* p);

// Fields/locals: snake_case
float cue_start;
int layer_order;

// Constants/defines: SCREAMING_SNAKE or constexpr
constexpr int kMaxScenes = 32;
#define GL_CLAMP_TO_EDGE 0x812F
```

---

## Structs

Prefer flat POD structs over deep hierarchies. Allocate with `new`/`malloc`, never on the stack if
the struct is large or owner-unclear.

```cpp
// Good — flat, readable, zero-initializable
struct ImageCue {
    char asset_key[64];
    float x, y, scale, opacity;
    float cue_start, cue_end;
    int layer_order;
};

// Bad — nested types the caller has to know about
struct ImageCue {
    Transform transform;   // ← forces caller to include transform.h
    Timing timing;
};
```

Fixed-size char arrays for strings inside structs (`char name[64]`). No `std::string` in
hot-path or serialized structs.

---

## Functions

- One job per function.
- No hidden state. Pass context explicitly.
- Return `bool` for fallible operations, `nullptr` for failed allocations.

```cpp
// Good
bool LoadImageCue(const char* path, ImageCue* out);
Program* CompileFromSource(const char* vert, const char* frag);  // nullptr on fail

// Bad
void LoadImageCue(const char* path);   // where does the result go?
Program CompileFromSource(...);        // who owns the copy?
```

---

## Adding a new cue type

1. Add struct to `revision_libs/rev_editor/include/rev_editor.h`
2. Add `{type}_cues[]` array + count to `SceneBlock`
3. Add export block to `ExportCues()` in `editor_context.cpp`
4. Add `Load{Type}Cue()` function to `examples/minimal_intro/main.cpp`
5. Wire rendering into the main loop

Parser field counts must match export field counts — check both when adding fields.

---

## Adding a new lib

1. Create `revision_libs/rev_<name>/include/rev_<name>.h` and `src/impl.cpp`
2. Create `revision_libs/rev_<name>/CMakeLists.txt`:
   ```cmake
   add_library(rev_<name> STATIC src/impl.cpp)
   target_include_directories(rev_<name> PUBLIC include)
   target_link_libraries(rev_<name> PRIVATE rev_platform)  # only what you need
   ```
3. Add to root `CMakeLists.txt`:
   ```cmake
   add_subdirectory(revision_libs/rev_<name>)
   ```
4. Link from the target that needs it:
   ```cmake
   target_link_libraries(editor_app PRIVATE rev_<name>)
   ```

No lib should link another lib unless it genuinely uses its types or functions.

---

## Memory

- `CreateFoo()` allocates, `DestroyFoo()` frees. Always paired.
- Caller owns everything returned by pointer unless the function name says otherwise.
- No shared_ptr. If ownership is unclear, that's a design problem — fix the design.
- Zero-init structs at creation: `memset(ctx, 0, sizeof(*ctx))` or `= {}`.

---

## Error handling

- No exceptions.
- Return `false`/`nullptr` on failure.
- Print to stdout with a clear prefix: `[LoadProject] FAILED: reason`
- No silent failures — if something goes wrong, say so.

---

## Platform specifics (Windows/GDI+)

- GDI+ must be initialized (`GdiplusStartup`) in `main()` before any image load call.
- GDI+ requires backslash separators — always convert before calling.
- Forward slashes everywhere else (exports, cues.txt).
- Windows-only APIs stay in `rev_platform` or in `main.cpp` — not buried in lib internals.

---

## What to avoid

| Avoid | Use instead |
|---|---|
| `std::string` in structs/hot-path | `char buf[N]` |
| Global mutable state | Explicit context pointers |
| Templates for one-offs | Plain functions |
| Deep inheritance | Flat structs + free functions |
| Allocating in library internals silently | Return allocated pointer, let caller control |
| Putting platform code in lib internals | Put it in `rev_platform` or `main.cpp` |
| Magic numbers inline | Named `constexpr` or `#define` with a comment |
