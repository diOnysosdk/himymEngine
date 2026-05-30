# rev_xm - XM Module Player

## Current Status: ✅ FULLY WORKING with libxm-windows

The rev_xm library now uses **libxm-windows** - a C89-compatible fork of libxm that works with MSVC!

### Implementation

- **Library:** [libxm-windows](https://github.com/MikeEviscerate/libxm-windows) by MikeEviscerate
- **Compatibility:** C89-compliant, compiles with VC 6.0, VS 2005, and VS 2026 (MSVC)
- **Features:** Full XM/MOD/S3M playback support
- **License:** WTFPL (same as original libxm)

### Why libxm-windows?

The original libxm requires C23's `<stdbit.h>` header, which is not yet implemented in any production compiler as of May 2026:
- ❌ **GCC 15.2.0** - `fatal error: stdbit.h: No such file or directory`
- ❌ **Clang 18.1.8** - `fatal error: 'stdbit.h' file not found`
- ❌ **MSVC** - No C23 support at all

**libxm-windows** solves this by:
- Converting the codebase to C89
- Providing polyfills for C23 features
- Using compiler intrinsics instead of stdbit.h functions

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
