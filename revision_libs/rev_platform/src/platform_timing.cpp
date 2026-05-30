#include "rev_platform.h"
#include <windows.h>

namespace rev {
namespace platform {

static LARGE_INTEGER frequency;
static LARGE_INTEGER start_time;
static bool initialized = false;

static void InitializeTiming() {
    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start_time);
        initialized = true;
    }
}

double GetTime() {
    InitializeTiming();
    
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    
    double elapsed = static_cast<double>(current_time.QuadPart - start_time.QuadPart);
    return elapsed / static_cast<double>(frequency.QuadPart);
}

void Sleep(double seconds) {
    ::Sleep(static_cast<DWORD>(seconds * 1000.0));
}

}  // namespace platform
}  // namespace rev
