// The DLL's entry point: the configuration and the log, before any Glide call.
#include "common.h"

using namespace gl2d9;

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void* reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_lock);
        LoadConfig(inst);
        LogInit();
    }
    if (reason == DLL_PROCESS_DETACH) {
        if (!reserved) {  // unloaded while the process goes on: nothing may point into it
            CrashLoggerRemove();
        }
        LogFlush(reserved != nullptr);
    }
    return TRUE;
}
