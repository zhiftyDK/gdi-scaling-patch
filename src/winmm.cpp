// Define this to avoid including the multimedia API headers that would conflict
#define MMNODRV
#define MMNOSOUND
#define MMNOWAVE
#define MMNOMIDI
#define MMNOAUX
#define MMNOMIXER
#define MMNOJOY
#define MMNOMCI
#define MMNOMMIO
#define MMNOMMSYSTEM

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

typedef int (WINAPI *SetDIBitsToDevice_t)(
    HDC,int,int,DWORD,DWORD,int,int,UINT,UINT,const VOID*,const BITMAPINFO*,UINT);

void* tSDTD;  // trampoline for original function
HWND gameWindow = NULL;
bool windowResized = false;
HMODULE hOriginalWinmm = NULL;

// Get screen dimensions
int GetScreenWidth() {
    return GetSystemMetrics(SM_CXSCREEN);
}

int GetScreenHeight() {
    return GetSystemMetrics(SM_CYSCREEN);
}

// Find and resize the game window to fullscreen
void ResizeGameWindow() {
    // Always check and maintain fullscreen
    if (!gameWindow || !IsWindow(gameWindow)) {
        gameWindow = GetForegroundWindow();
    }
    
    if (gameWindow) {
        int screenWidth = GetScreenWidth();
        int screenHeight = GetScreenHeight();
        
        // Check if window needs resizing
        RECT windowRect;
        GetWindowRect(gameWindow, &windowRect);
        int currentWidth = windowRect.right - windowRect.left;
        int currentHeight = windowRect.bottom - windowRect.top;
        
        // Only resize if not already fullscreen
        if (!windowResized || currentWidth != screenWidth || currentHeight != screenHeight || 
            windowRect.left != 0 || windowRect.top != 0) {
            
            // Remove window borders and make it fullscreen
            LONG style = GetWindowLong(gameWindow, GWL_STYLE);
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
            SetWindowLong(gameWindow, GWL_STYLE, style);
            
            LONG exStyle = GetWindowLong(gameWindow, GWL_EXSTYLE);
            exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
            SetWindowLong(gameWindow, GWL_EXSTYLE, exStyle);
            
            // Resize to fullscreen
            SetWindowPos(gameWindow, HWND_TOP, 0, 0, screenWidth, screenHeight, 
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            
            windowResized = true;
        }
    }
}

// Hooked function: scale the bitmap to fill the window
int WINAPI hSetDIBitsToDevice(
    HDC hdc, int x, int y, DWORD cx, DWORD cy,
    int xs, int ys, UINT s, UINT l,
    const VOID* bits, const BITMAPINFO* bmi, UINT u)
{
    // Make sure window is fullscreen
    ResizeGameWindow();
    
    // Get the actual window/DC dimensions
    HWND hwnd = WindowFromDC(hdc);
    RECT clientRect;
    int windowWidth = 0;
    int windowHeight = 0;
    
    if (hwnd && GetClientRect(hwnd, &clientRect)) {
        windowWidth = clientRect.right - clientRect.left;
        windowHeight = clientRect.bottom - clientRect.top;
    } else {
        // DC doesn't have a window (memory DC) - use screen dimensions
        windowWidth = GetScreenWidth();
        windowHeight = GetScreenHeight();
    }
    
    // ALWAYS scale if the window/DC is fullscreen-sized
    if (windowWidth > 1000 && windowHeight > 600) {
        // Get source dimensions - use the full bitmap size from BITMAPINFO if available
        int srcWidth = cx;
        int srcHeight = cy;
        
        if (bmi && bmi->bmiHeader.biWidth > 0 && abs(bmi->bmiHeader.biHeight) > 0) {
            srcWidth = bmi->bmiHeader.biWidth;
            srcHeight = abs(bmi->bmiHeader.biHeight);
        }
        
        // Calculate scale to fit window while maintaining aspect ratio
        float scaleX = (float)windowWidth / (float)srcWidth;
        float scaleY = (float)windowHeight / (float)srcHeight;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;  // Use smaller scale to fit
        
        int dstWidth = (int)(srcWidth * scale);
        int dstHeight = (int)(srcHeight * scale);
        
        // Center the image
        int dstX = (windowWidth - dstWidth) / 2;
        int dstY = (windowHeight - dstHeight) / 2;
        
        // Create memory DC for double buffering to eliminate flicker
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, windowWidth, windowHeight);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
        
        // Fill background with black in memory DC
        RECT fullRect = {0, 0, windowWidth, windowHeight};
        FillRect(memDC, &fullRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
        
        // Draw scaled image to memory DC
        SetStretchBltMode(memDC, COLORONCOLOR);  // Faster, better for pixel art
        
        StretchDIBits(
            memDC,
            dstX, dstY,
            dstWidth, dstHeight,
            0, 0,  // Use full source bitmap
            srcWidth, srcHeight,
            bits,
            bmi,
            u,
            SRCCOPY
        );
        
        // Copy complete frame from memory DC to screen in one operation (no flicker!)
        BitBlt(hdc, 0, 0, windowWidth, windowHeight, memDC, 0, 0, SRCCOPY);
        
        // Cleanup
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        
        return dstHeight;
    }
    
    // Fallback to original function if we can't scale
    return ((SetDIBitsToDevice_t)tSDTD)(hdc, x, y, cx, cy, xs, ys, s, l, bits, bmi, u);
}

// Minimal trampoline patching helpers
void WriteJump(void* src, void* dst)
{
    DWORD old;
    VirtualProtect(src, 5, PAGE_EXECUTE_READWRITE, &old);
    uint8_t* p = (uint8_t*)src;
    p[0] = 0xE9;
    *(uint32_t*)(p+1) = (uint32_t)((uint8_t*)dst - (uint8_t*)src - 5);
    VirtualProtect(src, 5, old, &old);
}

void* CreateTrampoline(void* target)
{
    const int len = 5;
    uint8_t* tramp = (uint8_t*)VirtualAlloc(
        NULL, len+5, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;
    
    memcpy(tramp, target, len);
    tramp[len] = 0xE9;
    *(uint32_t*)(tramp+len+1) = (uint32_t)((uint8_t*)target + len - tramp - len - 5);
    return tramp;
}

void HookSetDIBitsToDevice()
{
    HMODULE gdi = GetModuleHandleA("gdi32.dll");
    if(!gdi) return;

    FARPROC addr = GetProcAddress(gdi, "SetDIBitsToDevice");
    if(!addr) return;

    tSDTD = CreateTrampoline((void*)addr);
    if (!tSDTD) return;
    
    WriteJump((void*)addr, (void*)(uintptr_t)hSetDIBitsToDevice);
}

// Forward declaration
void InitializeFunctionPointers();

// Load original winmm.dll from system directory  
BOOL LoadOriginalWinmm()
{
    char systemPath[MAX_PATH];
    
    // Get the system directory (SysWOW64 for 32-bit on 64-bit, System32 for native)
    if (GetSystemDirectoryA(systemPath, MAX_PATH) == 0) {
        return FALSE;
    }
    
    lstrcatA(systemPath, "\\winmm.dll");
    
    hOriginalWinmm = LoadLibraryA(systemPath);
    return (hOriginalWinmm != NULL);
}

DWORD WINAPI Init(LPVOID)
{
    Sleep(500);  // Give the game time to create its window
    HookSetDIBitsToDevice();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID)
{
    if(r == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(h);
        
        // Load the original winmm.dll
        if (!LoadOriginalWinmm()) {
            return FALSE;
        }
        
        // Initialize all function pointers
        InitializeFunctionPointers();
        
        CreateThread(0, 0, Init, 0, 0, 0);
    }
    else if (r == DLL_PROCESS_DETACH)
    {
        if (hOriginalWinmm) {
            FreeLibrary(hOriginalWinmm);
        }
    }
    return TRUE;
}

// Function pointer declarations for all winmm functions
static FARPROC pCloseDriver = NULL;
static FARPROC pDefDriverProc = NULL;
static FARPROC pDriverCallback = NULL;
static FARPROC pDrvGetModuleHandle = NULL;
static FARPROC pGetDriverModuleHandle = NULL;
static FARPROC pOpenDriver = NULL;
static FARPROC pPlaySound = NULL;
static FARPROC pPlaySoundA = NULL;
static FARPROC pPlaySoundW = NULL;
static FARPROC pSendDriverMessage = NULL;
static FARPROC pauxGetDevCapsA = NULL;
static FARPROC pauxGetDevCapsW = NULL;
static FARPROC pauxGetNumDevs = NULL;
static FARPROC pauxGetVolume = NULL;
static FARPROC pauxOutMessage = NULL;
static FARPROC pauxSetVolume = NULL;
static FARPROC pjoyConfigChanged = NULL;
static FARPROC pjoyGetDevCapsA = NULL;
static FARPROC pjoyGetDevCapsW = NULL;
static FARPROC pjoyGetNumDevs = NULL;
static FARPROC pjoyGetPos = NULL;
static FARPROC pjoyGetPosEx = NULL;
static FARPROC pjoyGetThreshold = NULL;
static FARPROC pjoyReleaseCapture = NULL;
static FARPROC pjoySetCapture = NULL;
static FARPROC pjoySetThreshold = NULL;
static FARPROC pmciDriverNotify = NULL;
static FARPROC pmciDriverYield = NULL;
static FARPROC pmciExecute = NULL;
static FARPROC pmciFreeCommandResource = NULL;
static FARPROC pmciGetCreatorTask = NULL;
static FARPROC pmciGetDeviceIDA = NULL;
static FARPROC pmciGetDeviceIDFromElementIDA = NULL;
static FARPROC pmciGetDeviceIDFromElementIDW = NULL;
static FARPROC pmciGetDeviceIDW = NULL;
static FARPROC pmciGetDriverData = NULL;
static FARPROC pmciGetErrorStringA = NULL;
static FARPROC pmciGetErrorStringW = NULL;
static FARPROC pmciGetYieldProc = NULL;
static FARPROC pmciLoadCommandResource = NULL;
static FARPROC pmciSendCommandA = NULL;
static FARPROC pmciSendCommandW = NULL;
static FARPROC pmciSendStringA = NULL;
static FARPROC pmciSendStringW = NULL;
static FARPROC pmciSetDriverData = NULL;
static FARPROC pmciSetYieldProc = NULL;
static FARPROC pmidiConnect = NULL;
static FARPROC pmidiDisconnect = NULL;
static FARPROC pmidiInAddBuffer = NULL;
static FARPROC pmidiInClose = NULL;
static FARPROC pmidiInGetDevCapsA = NULL;
static FARPROC pmidiInGetDevCapsW = NULL;
static FARPROC pmidiInGetErrorTextA = NULL;
static FARPROC pmidiInGetErrorTextW = NULL;
static FARPROC pmidiInGetID = NULL;
static FARPROC pmidiInGetNumDevs = NULL;
static FARPROC pmidiInMessage = NULL;
static FARPROC pmidiInOpen = NULL;
static FARPROC pmidiInPrepareHeader = NULL;
static FARPROC pmidiInReset = NULL;
static FARPROC pmidiInStart = NULL;
static FARPROC pmidiInStop = NULL;
static FARPROC pmidiInUnprepareHeader = NULL;
static FARPROC pmidiOutCacheDrumPatches = NULL;
static FARPROC pmidiOutCachePatches = NULL;
static FARPROC pmidiOutClose = NULL;
static FARPROC pmidiOutGetDevCapsA = NULL;
static FARPROC pmidiOutGetDevCapsW = NULL;
static FARPROC pmidiOutGetErrorTextA = NULL;
static FARPROC pmidiOutGetErrorTextW = NULL;
static FARPROC pmidiOutGetID = NULL;
static FARPROC pmidiOutGetNumDevs = NULL;
static FARPROC pmidiOutGetVolume = NULL;
static FARPROC pmidiOutLongMsg = NULL;
static FARPROC pmidiOutMessage = NULL;
static FARPROC pmidiOutOpen = NULL;
static FARPROC pmidiOutPrepareHeader = NULL;
static FARPROC pmidiOutReset = NULL;
static FARPROC pmidiOutSetVolume = NULL;
static FARPROC pmidiOutShortMsg = NULL;
static FARPROC pmidiOutUnprepareHeader = NULL;
static FARPROC pmidiStreamClose = NULL;
static FARPROC pmidiStreamOpen = NULL;
static FARPROC pmidiStreamOut = NULL;
static FARPROC pmidiStreamPause = NULL;
static FARPROC pmidiStreamPosition = NULL;
static FARPROC pmidiStreamProperty = NULL;
static FARPROC pmidiStreamRestart = NULL;
static FARPROC pmidiStreamStop = NULL;
static FARPROC pmixerClose = NULL;
static FARPROC pmixerGetControlDetailsA = NULL;
static FARPROC pmixerGetControlDetailsW = NULL;
static FARPROC pmixerGetDevCapsA = NULL;
static FARPROC pmixerGetDevCapsW = NULL;
static FARPROC pmixerGetID = NULL;
static FARPROC pmixerGetLineControlsA = NULL;
static FARPROC pmixerGetLineControlsW = NULL;
static FARPROC pmixerGetLineInfoA = NULL;
static FARPROC pmixerGetLineInfoW = NULL;
static FARPROC pmixerGetNumDevs = NULL;
static FARPROC pmixerMessage = NULL;
static FARPROC pmixerOpen = NULL;
static FARPROC pmixerSetControlDetails = NULL;
static FARPROC pmmioAdvance = NULL;
static FARPROC pmmioAscend = NULL;
static FARPROC pmmioClose = NULL;
static FARPROC pmmioCreateChunk = NULL;
static FARPROC pmmioDescend = NULL;
static FARPROC pmmioFlush = NULL;
static FARPROC pmmioGetInfo = NULL;
static FARPROC pmmioInstallIOProcA = NULL;
static FARPROC pmmioInstallIOProcW = NULL;
static FARPROC pmmioOpenA = NULL;
static FARPROC pmmioOpenW = NULL;
static FARPROC pmmioRead = NULL;
static FARPROC pmmioRenameA = NULL;
static FARPROC pmmioRenameW = NULL;
static FARPROC pmmioSeek = NULL;
static FARPROC pmmioSendMessage = NULL;
static FARPROC pmmioSetBuffer = NULL;
static FARPROC pmmioSetInfo = NULL;
static FARPROC pmmioStringToFOURCCA = NULL;
static FARPROC pmmioStringToFOURCCW = NULL;
static FARPROC pmmioWrite = NULL;
static FARPROC pmmsystemGetVersion = NULL;
static FARPROC psndPlaySoundA = NULL;
static FARPROC psndPlaySoundW = NULL;
static FARPROC ptimeBeginPeriod = NULL;
static FARPROC ptimeEndPeriod = NULL;
static FARPROC ptimeGetDevCaps = NULL;
static FARPROC ptimeGetSystemTime = NULL;
static FARPROC ptimeGetTime = NULL;
static FARPROC ptimeKillEvent = NULL;
static FARPROC ptimeSetEvent = NULL;
static FARPROC pwaveInAddBuffer = NULL;
static FARPROC pwaveInClose = NULL;
static FARPROC pwaveInGetDevCapsA = NULL;
static FARPROC pwaveInGetDevCapsW = NULL;
static FARPROC pwaveInGetErrorTextA = NULL;
static FARPROC pwaveInGetErrorTextW = NULL;
static FARPROC pwaveInGetID = NULL;
static FARPROC pwaveInGetNumDevs = NULL;
static FARPROC pwaveInGetPosition = NULL;
static FARPROC pwaveInMessage = NULL;
static FARPROC pwaveInOpen = NULL;
static FARPROC pwaveInPrepareHeader = NULL;
static FARPROC pwaveInReset = NULL;
static FARPROC pwaveInStart = NULL;
static FARPROC pwaveInStop = NULL;
static FARPROC pwaveInUnprepareHeader = NULL;
static FARPROC pwaveOutBreakLoop = NULL;
static FARPROC pwaveOutClose = NULL;
static FARPROC pwaveOutGetDevCapsA = NULL;
static FARPROC pwaveOutGetDevCapsW = NULL;
static FARPROC pwaveOutGetErrorTextA = NULL;
static FARPROC pwaveOutGetErrorTextW = NULL;
static FARPROC pwaveOutGetID = NULL;
static FARPROC pwaveOutGetNumDevs = NULL;
static FARPROC pwaveOutGetPitch = NULL;
static FARPROC pwaveOutGetPlaybackRate = NULL;
static FARPROC pwaveOutGetPosition = NULL;
static FARPROC pwaveOutGetVolume = NULL;
static FARPROC pwaveOutMessage = NULL;
static FARPROC pwaveOutOpen = NULL;
static FARPROC pwaveOutPause = NULL;
static FARPROC pwaveOutPrepareHeader = NULL;
static FARPROC pwaveOutReset = NULL;
static FARPROC pwaveOutRestart = NULL;
static FARPROC pwaveOutSetPitch = NULL;
static FARPROC pwaveOutSetPlaybackRate = NULL;
static FARPROC pwaveOutSetVolume = NULL;
static FARPROC pwaveOutUnprepareHeader = NULL;
static FARPROC pwaveOutWrite = NULL;

// Initialize all function pointers
void InitializeFunctionPointers()
{
    if (!hOriginalWinmm) return;
    
    pCloseDriver = GetProcAddress(hOriginalWinmm, "CloseDriver");
    pDefDriverProc = GetProcAddress(hOriginalWinmm, "DefDriverProc");
    pDriverCallback = GetProcAddress(hOriginalWinmm, "DriverCallback");
    pDrvGetModuleHandle = GetProcAddress(hOriginalWinmm, "DrvGetModuleHandle");
    pGetDriverModuleHandle = GetProcAddress(hOriginalWinmm, "GetDriverModuleHandle");
    pOpenDriver = GetProcAddress(hOriginalWinmm, "OpenDriver");
    pPlaySound = GetProcAddress(hOriginalWinmm, "PlaySound");
    pPlaySoundA = GetProcAddress(hOriginalWinmm, "PlaySoundA");
    pPlaySoundW = GetProcAddress(hOriginalWinmm, "PlaySoundW");
    pSendDriverMessage = GetProcAddress(hOriginalWinmm, "SendDriverMessage");
    pauxGetDevCapsA = GetProcAddress(hOriginalWinmm, "auxGetDevCapsA");
    pauxGetDevCapsW = GetProcAddress(hOriginalWinmm, "auxGetDevCapsW");
    pauxGetNumDevs = GetProcAddress(hOriginalWinmm, "auxGetNumDevs");
    pauxGetVolume = GetProcAddress(hOriginalWinmm, "auxGetVolume");
    pauxOutMessage = GetProcAddress(hOriginalWinmm, "auxOutMessage");
    pauxSetVolume = GetProcAddress(hOriginalWinmm, "auxSetVolume");
    pjoyConfigChanged = GetProcAddress(hOriginalWinmm, "joyConfigChanged");
    pjoyGetDevCapsA = GetProcAddress(hOriginalWinmm, "joyGetDevCapsA");
    pjoyGetDevCapsW = GetProcAddress(hOriginalWinmm, "joyGetDevCapsW");
    pjoyGetNumDevs = GetProcAddress(hOriginalWinmm, "joyGetNumDevs");
    pjoyGetPos = GetProcAddress(hOriginalWinmm, "joyGetPos");
    pjoyGetPosEx = GetProcAddress(hOriginalWinmm, "joyGetPosEx");
    pjoyGetThreshold = GetProcAddress(hOriginalWinmm, "joyGetThreshold");
    pjoyReleaseCapture = GetProcAddress(hOriginalWinmm, "joyReleaseCapture");
    pjoySetCapture = GetProcAddress(hOriginalWinmm, "joySetCapture");
    pjoySetThreshold = GetProcAddress(hOriginalWinmm, "joySetThreshold");
    pmciDriverNotify = GetProcAddress(hOriginalWinmm, "mciDriverNotify");
    pmciDriverYield = GetProcAddress(hOriginalWinmm, "mciDriverYield");
    pmciExecute = GetProcAddress(hOriginalWinmm, "mciExecute");
    pmciFreeCommandResource = GetProcAddress(hOriginalWinmm, "mciFreeCommandResource");
    pmciGetCreatorTask = GetProcAddress(hOriginalWinmm, "mciGetCreatorTask");
    pmciGetDeviceIDA = GetProcAddress(hOriginalWinmm, "mciGetDeviceIDA");
    pmciGetDeviceIDFromElementIDA = GetProcAddress(hOriginalWinmm, "mciGetDeviceIDFromElementIDA");
    pmciGetDeviceIDFromElementIDW = GetProcAddress(hOriginalWinmm, "mciGetDeviceIDFromElementIDW");
    pmciGetDeviceIDW = GetProcAddress(hOriginalWinmm, "mciGetDeviceIDW");
    pmciGetDriverData = GetProcAddress(hOriginalWinmm, "mciGetDriverData");
    pmciGetErrorStringA = GetProcAddress(hOriginalWinmm, "mciGetErrorStringA");
    pmciGetErrorStringW = GetProcAddress(hOriginalWinmm, "mciGetErrorStringW");
    pmciGetYieldProc = GetProcAddress(hOriginalWinmm, "mciGetYieldProc");
    pmciLoadCommandResource = GetProcAddress(hOriginalWinmm, "mciLoadCommandResource");
    pmciSendCommandA = GetProcAddress(hOriginalWinmm, "mciSendCommandA");
    pmciSendCommandW = GetProcAddress(hOriginalWinmm, "mciSendCommandW");
    pmciSendStringA = GetProcAddress(hOriginalWinmm, "mciSendStringA");
    pmciSendStringW = GetProcAddress(hOriginalWinmm, "mciSendStringW");
    pmciSetDriverData = GetProcAddress(hOriginalWinmm, "mciSetDriverData");
    pmciSetYieldProc = GetProcAddress(hOriginalWinmm, "mciSetYieldProc");
    pmidiConnect = GetProcAddress(hOriginalWinmm, "midiConnect");
    pmidiDisconnect = GetProcAddress(hOriginalWinmm, "midiDisconnect");
    pmidiInAddBuffer = GetProcAddress(hOriginalWinmm, "midiInAddBuffer");
    pmidiInClose = GetProcAddress(hOriginalWinmm, "midiInClose");
    pmidiInGetDevCapsA = GetProcAddress(hOriginalWinmm, "midiInGetDevCapsA");
    pmidiInGetDevCapsW = GetProcAddress(hOriginalWinmm, "midiInGetDevCapsW");
    pmidiInGetErrorTextA = GetProcAddress(hOriginalWinmm, "midiInGetErrorTextA");
    pmidiInGetErrorTextW = GetProcAddress(hOriginalWinmm, "midiInGetErrorTextW");
    pmidiInGetID = GetProcAddress(hOriginalWinmm, "midiInGetID");
    pmidiInGetNumDevs = GetProcAddress(hOriginalWinmm, "midiInGetNumDevs");
    pmidiInMessage = GetProcAddress(hOriginalWinmm, "midiInMessage");
    pmidiInOpen = GetProcAddress(hOriginalWinmm, "midiInOpen");
    pmidiInPrepareHeader = GetProcAddress(hOriginalWinmm, "midiInPrepareHeader");
    pmidiInReset = GetProcAddress(hOriginalWinmm, "midiInReset");
    pmidiInStart = GetProcAddress(hOriginalWinmm, "midiInStart");
    pmidiInStop = GetProcAddress(hOriginalWinmm, "midiInStop");
    pmidiInUnprepareHeader = GetProcAddress(hOriginalWinmm, "midiInUnprepareHeader");
    pmidiOutCacheDrumPatches = GetProcAddress(hOriginalWinmm, "midiOutCacheDrumPatches");
    pmidiOutCachePatches = GetProcAddress(hOriginalWinmm, "midiOutCachePatches");
    pmidiOutClose = GetProcAddress(hOriginalWinmm, "midiOutClose");
    pmidiOutGetDevCapsA = GetProcAddress(hOriginalWinmm, "midiOutGetDevCapsA");
    pmidiOutGetDevCapsW = GetProcAddress(hOriginalWinmm, "midiOutGetDevCapsW");
    pmidiOutGetErrorTextA = GetProcAddress(hOriginalWinmm, "midiOutGetErrorTextA");
    pmidiOutGetErrorTextW = GetProcAddress(hOriginalWinmm, "midiOutGetErrorTextW");
    pmidiOutGetID = GetProcAddress(hOriginalWinmm, "midiOutGetID");
    pmidiOutGetNumDevs = GetProcAddress(hOriginalWinmm, "midiOutGetNumDevs");
    pmidiOutGetVolume = GetProcAddress(hOriginalWinmm, "midiOutGetVolume");
    pmidiOutLongMsg = GetProcAddress(hOriginalWinmm, "midiOutLongMsg");
    pmidiOutMessage = GetProcAddress(hOriginalWinmm, "midiOutMessage");
    pmidiOutOpen = GetProcAddress(hOriginalWinmm, "midiOutOpen");
    pmidiOutPrepareHeader = GetProcAddress(hOriginalWinmm, "midiOutPrepareHeader");
    pmidiOutReset = GetProcAddress(hOriginalWinmm, "midiOutReset");
    pmidiOutSetVolume = GetProcAddress(hOriginalWinmm, "midiOutSetVolume");
    pmidiOutShortMsg = GetProcAddress(hOriginalWinmm, "midiOutShortMsg");
    pmidiOutUnprepareHeader = GetProcAddress(hOriginalWinmm, "midiOutUnprepareHeader");
    pmidiStreamClose = GetProcAddress(hOriginalWinmm, "midiStreamClose");
    pmidiStreamOpen = GetProcAddress(hOriginalWinmm, "midiStreamOpen");
    pmidiStreamOut = GetProcAddress(hOriginalWinmm, "midiStreamOut");
    pmidiStreamPause = GetProcAddress(hOriginalWinmm, "midiStreamPause");
    pmidiStreamPosition = GetProcAddress(hOriginalWinmm, "midiStreamPosition");
    pmidiStreamProperty = GetProcAddress(hOriginalWinmm, "midiStreamProperty");
    pmidiStreamRestart = GetProcAddress(hOriginalWinmm, "midiStreamRestart");
    pmidiStreamStop = GetProcAddress(hOriginalWinmm, "midiStreamStop");
    pmixerClose = GetProcAddress(hOriginalWinmm, "mixerClose");
    pmixerGetControlDetailsA = GetProcAddress(hOriginalWinmm, "mixerGetControlDetailsA");
    pmixerGetControlDetailsW = GetProcAddress(hOriginalWinmm, "mixerGetControlDetailsW");
    pmixerGetDevCapsA = GetProcAddress(hOriginalWinmm, "mixerGetDevCapsA");
    pmixerGetDevCapsW = GetProcAddress(hOriginalWinmm, "mixerGetDevCapsW");
    pmixerGetID = GetProcAddress(hOriginalWinmm, "mixerGetID");
    pmixerGetLineControlsA = GetProcAddress(hOriginalWinmm, "mixerGetLineControlsA");
    pmixerGetLineControlsW = GetProcAddress(hOriginalWinmm, "mixerGetLineControlsW");
    pmixerGetLineInfoA = GetProcAddress(hOriginalWinmm, "mixerGetLineInfoA");
    pmixerGetLineInfoW = GetProcAddress(hOriginalWinmm, "mixerGetLineInfoW");
    pmixerGetNumDevs = GetProcAddress(hOriginalWinmm, "mixerGetNumDevs");
    pmixerMessage = GetProcAddress(hOriginalWinmm, "mixerMessage");
    pmixerOpen = GetProcAddress(hOriginalWinmm, "mixerOpen");
    pmixerSetControlDetails = GetProcAddress(hOriginalWinmm, "mixerSetControlDetails");
    pmmioAdvance = GetProcAddress(hOriginalWinmm, "mmioAdvance");
    pmmioAscend = GetProcAddress(hOriginalWinmm, "mmioAscend");
    pmmioClose = GetProcAddress(hOriginalWinmm, "mmioClose");
    pmmioCreateChunk = GetProcAddress(hOriginalWinmm, "mmioCreateChunk");
    pmmioDescend = GetProcAddress(hOriginalWinmm, "mmioDescend");
    pmmioFlush = GetProcAddress(hOriginalWinmm, "mmioFlush");
    pmmioGetInfo = GetProcAddress(hOriginalWinmm, "mmioGetInfo");
    pmmioInstallIOProcA = GetProcAddress(hOriginalWinmm, "mmioInstallIOProcA");
    pmmioInstallIOProcW = GetProcAddress(hOriginalWinmm, "mmioInstallIOProcW");
    pmmioOpenA = GetProcAddress(hOriginalWinmm, "mmioOpenA");
    pmmioOpenW = GetProcAddress(hOriginalWinmm, "mmioOpenW");
    pmmioRead = GetProcAddress(hOriginalWinmm, "mmioRead");
    pmmioRenameA = GetProcAddress(hOriginalWinmm, "mmioRenameA");
    pmmioRenameW = GetProcAddress(hOriginalWinmm, "mmioRenameW");
    pmmioSeek = GetProcAddress(hOriginalWinmm, "mmioSeek");
    pmmioSendMessage = GetProcAddress(hOriginalWinmm, "mmioSendMessage");
    pmmioSetBuffer = GetProcAddress(hOriginalWinmm, "mmioSetBuffer");
    pmmioSetInfo = GetProcAddress(hOriginalWinmm, "mmioSetInfo");
    pmmioStringToFOURCCA = GetProcAddress(hOriginalWinmm, "mmioStringToFOURCCA");
    pmmioStringToFOURCCW = GetProcAddress(hOriginalWinmm, "mmioStringToFOURCCW");
    pmmioWrite = GetProcAddress(hOriginalWinmm, "mmioWrite");
    pmmsystemGetVersion = GetProcAddress(hOriginalWinmm, "mmsystemGetVersion");
    psndPlaySoundA = GetProcAddress(hOriginalWinmm, "sndPlaySoundA");
    psndPlaySoundW = GetProcAddress(hOriginalWinmm, "sndPlaySoundW");
    ptimeBeginPeriod = GetProcAddress(hOriginalWinmm, "timeBeginPeriod");
    ptimeEndPeriod = GetProcAddress(hOriginalWinmm, "timeEndPeriod");
    ptimeGetDevCaps = GetProcAddress(hOriginalWinmm, "timeGetDevCaps");
    ptimeGetSystemTime = GetProcAddress(hOriginalWinmm, "timeGetSystemTime");
    ptimeGetTime = GetProcAddress(hOriginalWinmm, "timeGetTime");
    ptimeKillEvent = GetProcAddress(hOriginalWinmm, "timeKillEvent");
    ptimeSetEvent = GetProcAddress(hOriginalWinmm, "timeSetEvent");
    pwaveInAddBuffer = GetProcAddress(hOriginalWinmm, "waveInAddBuffer");
    pwaveInClose = GetProcAddress(hOriginalWinmm, "waveInClose");
    pwaveInGetDevCapsA = GetProcAddress(hOriginalWinmm, "waveInGetDevCapsA");
    pwaveInGetDevCapsW = GetProcAddress(hOriginalWinmm, "waveInGetDevCapsW");
    pwaveInGetErrorTextA = GetProcAddress(hOriginalWinmm, "waveInGetErrorTextA");
    pwaveInGetErrorTextW = GetProcAddress(hOriginalWinmm, "waveInGetErrorTextW");
    pwaveInGetID = GetProcAddress(hOriginalWinmm, "waveInGetID");
    pwaveInGetNumDevs = GetProcAddress(hOriginalWinmm, "waveInGetNumDevs");
    pwaveInGetPosition = GetProcAddress(hOriginalWinmm, "waveInGetPosition");
    pwaveInMessage = GetProcAddress(hOriginalWinmm, "waveInMessage");
    pwaveInOpen = GetProcAddress(hOriginalWinmm, "waveInOpen");
    pwaveInPrepareHeader = GetProcAddress(hOriginalWinmm, "waveInPrepareHeader");
    pwaveInReset = GetProcAddress(hOriginalWinmm, "waveInReset");
    pwaveInStart = GetProcAddress(hOriginalWinmm, "waveInStart");
    pwaveInStop = GetProcAddress(hOriginalWinmm, "waveInStop");
    pwaveInUnprepareHeader = GetProcAddress(hOriginalWinmm, "waveInUnprepareHeader");
    pwaveOutBreakLoop = GetProcAddress(hOriginalWinmm, "waveOutBreakLoop");
    pwaveOutClose = GetProcAddress(hOriginalWinmm, "waveOutClose");
    pwaveOutGetDevCapsA = GetProcAddress(hOriginalWinmm, "waveOutGetDevCapsA");
    pwaveOutGetDevCapsW = GetProcAddress(hOriginalWinmm, "waveOutGetDevCapsW");
    pwaveOutGetErrorTextA = GetProcAddress(hOriginalWinmm, "waveOutGetErrorTextA");
    pwaveOutGetErrorTextW = GetProcAddress(hOriginalWinmm, "waveOutGetErrorTextW");
    pwaveOutGetID = GetProcAddress(hOriginalWinmm, "waveOutGetID");
    pwaveOutGetNumDevs = GetProcAddress(hOriginalWinmm, "waveOutGetNumDevs");
    pwaveOutGetPitch = GetProcAddress(hOriginalWinmm, "waveOutGetPitch");
    pwaveOutGetPlaybackRate = GetProcAddress(hOriginalWinmm, "waveOutGetPlaybackRate");
    pwaveOutGetPosition = GetProcAddress(hOriginalWinmm, "waveOutGetPosition");
    pwaveOutGetVolume = GetProcAddress(hOriginalWinmm, "waveOutGetVolume");
    pwaveOutMessage = GetProcAddress(hOriginalWinmm, "waveOutMessage");
    pwaveOutOpen = GetProcAddress(hOriginalWinmm, "waveOutOpen");
    pwaveOutPause = GetProcAddress(hOriginalWinmm, "waveOutPause");
    pwaveOutPrepareHeader = GetProcAddress(hOriginalWinmm, "waveOutPrepareHeader");
    pwaveOutReset = GetProcAddress(hOriginalWinmm, "waveOutReset");
    pwaveOutRestart = GetProcAddress(hOriginalWinmm, "waveOutRestart");
    pwaveOutSetPitch = GetProcAddress(hOriginalWinmm, "waveOutSetPitch");
    pwaveOutSetPlaybackRate = GetProcAddress(hOriginalWinmm, "waveOutSetPlaybackRate");
    pwaveOutSetVolume = GetProcAddress(hOriginalWinmm, "waveOutSetVolume");
    pwaveOutUnprepareHeader = GetProcAddress(hOriginalWinmm, "waveOutUnprepareHeader");
    pwaveOutWrite = GetProcAddress(hOriginalWinmm, "waveOutWrite");
}

// Wrapper macro - simple naked function that jumps to function pointer
#define WRAP_FUNC(name) \
extern "C" __declspec(naked) void __declspec(dllexport) name() { \
    __asm jmp dword ptr [p##name] \
}

WRAP_FUNC(CloseDriver)
WRAP_FUNC(DefDriverProc)
WRAP_FUNC(DriverCallback)
WRAP_FUNC(DrvGetModuleHandle)
WRAP_FUNC(GetDriverModuleHandle)
WRAP_FUNC(OpenDriver)
WRAP_FUNC(PlaySound)
WRAP_FUNC(PlaySoundA)
WRAP_FUNC(PlaySoundW)
WRAP_FUNC(SendDriverMessage)
WRAP_FUNC(auxGetDevCapsA)
WRAP_FUNC(auxGetDevCapsW)
WRAP_FUNC(auxGetNumDevs)
WRAP_FUNC(auxGetVolume)
WRAP_FUNC(auxOutMessage)
WRAP_FUNC(auxSetVolume)
WRAP_FUNC(joyConfigChanged)
WRAP_FUNC(joyGetDevCapsA)
WRAP_FUNC(joyGetDevCapsW)
WRAP_FUNC(joyGetNumDevs)
WRAP_FUNC(joyGetPos)
WRAP_FUNC(joyGetPosEx)
WRAP_FUNC(joyGetThreshold)
WRAP_FUNC(joyReleaseCapture)
WRAP_FUNC(joySetCapture)
WRAP_FUNC(joySetThreshold)
WRAP_FUNC(mciDriverNotify)
WRAP_FUNC(mciDriverYield)
WRAP_FUNC(mciExecute)
WRAP_FUNC(mciFreeCommandResource)
WRAP_FUNC(mciGetCreatorTask)
WRAP_FUNC(mciGetDeviceIDA)
WRAP_FUNC(mciGetDeviceIDFromElementIDA)
WRAP_FUNC(mciGetDeviceIDFromElementIDW)
WRAP_FUNC(mciGetDeviceIDW)
WRAP_FUNC(mciGetDriverData)
WRAP_FUNC(mciGetErrorStringA)
WRAP_FUNC(mciGetErrorStringW)
WRAP_FUNC(mciGetYieldProc)
WRAP_FUNC(mciLoadCommandResource)
WRAP_FUNC(mciSendCommandA)
WRAP_FUNC(mciSendCommandW)
WRAP_FUNC(mciSendStringA)
WRAP_FUNC(mciSendStringW)
WRAP_FUNC(mciSetDriverData)
WRAP_FUNC(mciSetYieldProc)
WRAP_FUNC(midiConnect)
WRAP_FUNC(midiDisconnect)
WRAP_FUNC(midiInAddBuffer)
WRAP_FUNC(midiInClose)
WRAP_FUNC(midiInGetDevCapsA)
WRAP_FUNC(midiInGetDevCapsW)
WRAP_FUNC(midiInGetErrorTextA)
WRAP_FUNC(midiInGetErrorTextW)
WRAP_FUNC(midiInGetID)
WRAP_FUNC(midiInGetNumDevs)
WRAP_FUNC(midiInMessage)
WRAP_FUNC(midiInOpen)
WRAP_FUNC(midiInPrepareHeader)
WRAP_FUNC(midiInReset)
WRAP_FUNC(midiInStart)
WRAP_FUNC(midiInStop)
WRAP_FUNC(midiInUnprepareHeader)
WRAP_FUNC(midiOutCacheDrumPatches)
WRAP_FUNC(midiOutCachePatches)
WRAP_FUNC(midiOutClose)
WRAP_FUNC(midiOutGetDevCapsA)
WRAP_FUNC(midiOutGetDevCapsW)
WRAP_FUNC(midiOutGetErrorTextA)
WRAP_FUNC(midiOutGetErrorTextW)
WRAP_FUNC(midiOutGetID)
WRAP_FUNC(midiOutGetNumDevs)
WRAP_FUNC(midiOutGetVolume)
WRAP_FUNC(midiOutLongMsg)
WRAP_FUNC(midiOutMessage)
WRAP_FUNC(midiOutOpen)
WRAP_FUNC(midiOutPrepareHeader)
WRAP_FUNC(midiOutReset)
WRAP_FUNC(midiOutSetVolume)
WRAP_FUNC(midiOutShortMsg)
WRAP_FUNC(midiOutUnprepareHeader)
WRAP_FUNC(midiStreamClose)
WRAP_FUNC(midiStreamOpen)
WRAP_FUNC(midiStreamOut)
WRAP_FUNC(midiStreamPause)
WRAP_FUNC(midiStreamPosition)
WRAP_FUNC(midiStreamProperty)
WRAP_FUNC(midiStreamRestart)
WRAP_FUNC(midiStreamStop)
WRAP_FUNC(mixerClose)
WRAP_FUNC(mixerGetControlDetailsA)
WRAP_FUNC(mixerGetControlDetailsW)
WRAP_FUNC(mixerGetDevCapsA)
WRAP_FUNC(mixerGetDevCapsW)
WRAP_FUNC(mixerGetID)
WRAP_FUNC(mixerGetLineControlsA)
WRAP_FUNC(mixerGetLineControlsW)
WRAP_FUNC(mixerGetLineInfoA)
WRAP_FUNC(mixerGetLineInfoW)
WRAP_FUNC(mixerGetNumDevs)
WRAP_FUNC(mixerMessage)
WRAP_FUNC(mixerOpen)
WRAP_FUNC(mixerSetControlDetails)
WRAP_FUNC(mmioAdvance)
WRAP_FUNC(mmioAscend)
WRAP_FUNC(mmioClose)
WRAP_FUNC(mmioCreateChunk)
WRAP_FUNC(mmioDescend)
WRAP_FUNC(mmioFlush)
WRAP_FUNC(mmioGetInfo)
WRAP_FUNC(mmioInstallIOProcA)
WRAP_FUNC(mmioInstallIOProcW)
WRAP_FUNC(mmioOpenA)
WRAP_FUNC(mmioOpenW)
WRAP_FUNC(mmioRead)
WRAP_FUNC(mmioRenameA)
WRAP_FUNC(mmioRenameW)
WRAP_FUNC(mmioSeek)
WRAP_FUNC(mmioSendMessage)
WRAP_FUNC(mmioSetBuffer)
WRAP_FUNC(mmioSetInfo)
WRAP_FUNC(mmioStringToFOURCCA)
WRAP_FUNC(mmioStringToFOURCCW)
WRAP_FUNC(mmioWrite)
WRAP_FUNC(mmsystemGetVersion)
WRAP_FUNC(sndPlaySoundA)
WRAP_FUNC(sndPlaySoundW)
WRAP_FUNC(timeBeginPeriod)
WRAP_FUNC(timeEndPeriod)
WRAP_FUNC(timeGetDevCaps)
WRAP_FUNC(timeGetSystemTime)
WRAP_FUNC(timeGetTime)
WRAP_FUNC(timeKillEvent)
WRAP_FUNC(timeSetEvent)
WRAP_FUNC(waveInAddBuffer)
WRAP_FUNC(waveInClose)
WRAP_FUNC(waveInGetDevCapsA)
WRAP_FUNC(waveInGetDevCapsW)
WRAP_FUNC(waveInGetErrorTextA)
WRAP_FUNC(waveInGetErrorTextW)
WRAP_FUNC(waveInGetID)
WRAP_FUNC(waveInGetNumDevs)
WRAP_FUNC(waveInGetPosition)
WRAP_FUNC(waveInMessage)
WRAP_FUNC(waveInOpen)
WRAP_FUNC(waveInPrepareHeader)
WRAP_FUNC(waveInReset)
WRAP_FUNC(waveInStart)
WRAP_FUNC(waveInStop)
WRAP_FUNC(waveInUnprepareHeader)
WRAP_FUNC(waveOutBreakLoop)
WRAP_FUNC(waveOutClose)
WRAP_FUNC(waveOutGetDevCapsA)
WRAP_FUNC(waveOutGetDevCapsW)
WRAP_FUNC(waveOutGetErrorTextA)
WRAP_FUNC(waveOutGetErrorTextW)
WRAP_FUNC(waveOutGetID)
WRAP_FUNC(waveOutGetNumDevs)
WRAP_FUNC(waveOutGetPitch)
WRAP_FUNC(waveOutGetPlaybackRate)
WRAP_FUNC(waveOutGetPosition)
WRAP_FUNC(waveOutGetVolume)
WRAP_FUNC(waveOutMessage)
WRAP_FUNC(waveOutOpen)
WRAP_FUNC(waveOutPause)
WRAP_FUNC(waveOutPrepareHeader)
WRAP_FUNC(waveOutReset)
WRAP_FUNC(waveOutRestart)
WRAP_FUNC(waveOutSetPitch)
WRAP_FUNC(waveOutSetPlaybackRate)
WRAP_FUNC(waveOutSetVolume)
WRAP_FUNC(waveOutUnprepareHeader)
WRAP_FUNC(waveOutWrite)