#include <windows.h>
#include <stdint.h>
#include <string.h>

typedef int (WINAPI *SetDIBitsToDevice_t)(
    HDC,int,int,DWORD,DWORD,int,int,UINT,UINT,const VOID*,const BITMAPINFO*,UINT);

void* tSDTD;  // trampoline for original function
HWND gameWindow = NULL;
bool windowResized = false;

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
        CreateThread(0, 0, Init, 0, 0, 0);
    }
    return TRUE;
}