# rev_xm - XM Module Player

## Current Status: Stub Implementation

The rev_xm library provides an API for XM (FastTracker II Extended Module) music playback, but currently uses a **stub implementation** because the underlying libxm library requires C23 features that are not yet widely available.

### Why Stub?

libxm (https://github.com/Artefact2/libxm) requires:
- **C23 standard** with `<stdbit.h>` header
- Even **GCC 15.2.0** (May 2026) doesn't fully support `<stdbit.h>` yet
- **MSVC** has no C23 support at all

**Error with GCC 15.2.0:**
```
xm_internal.h:14:10: fatal error: stdbit.h: No such file or directory
```

### Future Options

1. **Wait for C23 compiler support** - When GCC/Clang fully implement `<stdbit.h>`
2. **Use alternative library** - miniaudio, SDL_mixer, FamiTracker, etc.
3. **Implement custom XM parser** - Lightweight parser for demoscene use
4. **Patch libxm** - Create polyfill for `<stdbit.h>` functions

## Dependencies

This library has **libxm** downloaded in `third_party/libxm/` but cannot compile it yet due to C23 requirements.

## Usage Example

```cpp
#include "rev_xm.h"

// Embedded XM data
extern const unsigned char music_xm[];
extern const size_t music_xm_len;

// Create player
auto* player = rev::xm::CreatePlayer(music_xm, music_xm_len, 48000);

// In audio callback
float audio_buffer[1024];
rev::xm::Update(player, audio_buffer, 512);  // 512 frames = 1024 samples stereo

// Cleanup
rev::xm::DestroyPlayer(player);
```
