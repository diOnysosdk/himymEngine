# rev_xm - XM Module Player

This library provides XM music playback functionality.

## Dependencies

This library requires **libxm** to function. To set it up:

1. Download libxm from: https://github.com/Artefact2/libxm
2. Create the directory: `revision_libs/rev_xm/third_party/libxm/`
3. Copy `xm.h` and `xm.c` to that directory
4. Uncomment the libxm configuration in `CMakeLists.txt`
5. Uncomment the include in `src/xm_player.cpp`

## Current Status

**STUB IMPLEMENTATION**: The library currently contains stub functions that compile but don't produce audio. Once libxm is added, the implementation will be complete.

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
