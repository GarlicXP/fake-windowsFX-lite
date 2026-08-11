#pragma warning(disable: 4819)

#pragma comment(linker, "/subsystem:windows")
#pragma comment(linker, "/entry:WinMainCRTStartup")

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <algorithm>
#include <strsafe.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Shell32.lib")

#ifndef DWMWA_EXTENDED_FRAME_BOUNDERS
#define DWMWA_EXTENDED_FRAME_BOUNDERS 9
#endif

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001

ID3D11Device* g_d3dDevice = nullptr;
ID2D1Factory1* g_d2dFactory = nullptr;
ID2D1Device* g_d2dDevice = nullptr;
ID2D1DeviceContext* g_d2dContext = nullptr;
IDCompositionDevice* g_dcompDevice = nullptr;

HWND g_hMainWnd = nullptr;
HHOOK g_mouseHook = nullptr;
NOTIFYICONDATAW g_nid = { sizeof(NOTIFYICONDATAW) };

IDCompositionTarget* g_activeDCompTarget = nullptr;
IDCompositionVisual* g_activeRootVisual = nullptr;
IDCompositionSurface* g_activeDCompSurface = nullptr;
IDCompositionMatrixTransform* g_activeTransform = nullptr;
HWND g_activeHwndClone = nullptr;
HWND g_activeTargetHwnd = nullptr;

LONG_PTR g_origTargetExStyle = 0;
BYTE g_origAlpha = 255;
DWORD g_origFlags = 0;
bool g_hadLayeredStyle = false;

POINT g_initialWindowPos = { 0, 0 };
POINT g_initialMousePt = { 0, 0 };
float g_currentSkew = 0.0f;
int g_width = 0; int g_height = 0;
int g_paddingX = 0; int g_paddingY = 0;
int g_cloneWidth = 0; int g_cloneHeight = 0;
bool g_isTrackingDrag = false;
bool g_isEffectActive = false;

struct MyMatrix3x2 { float m11, m12; float m21, m22; float m31, m32; };

bool InitGlobalDComp() {
    if (g_dcompDevice) return true;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &g_d3dDevice, nullptr, nullptr);
    if (FAILED(hr) || !g_d3dDevice) return false;
    IDXGIDevice* dxgiDevice = nullptr;
    hr = g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr) || !dxgiDevice) return false;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory);
    if (g_d2dFactory) g_d2dFactory->CreateDevice(dxgiDevice, &g_d2dDevice);
    if (g_d2dDevice) g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_d2dContext);
    DCompositionCreateDevice(dxgiDevice, __uuidof(IDCompositionDevice), (void**)&g_dcompDevice);
    dxgiDevice->Release();
    return (g_dcompDevice && g_d2dContext);
}

bool SetupTargetDComp(HWND hwnd) {
    if (!InitGlobalDComp()) return false;
    g_dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &g_activeDCompTarget);
    g_dcompDevice->CreateVisual(&g_activeRootVisual);
    g_dcompDevice->CreateSurface(g_cloneWidth, g_cloneHeight, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &g_activeDCompSurface);
    g_activeRootVisual->SetContent(g_activeDCompSurface);
    g_dcompDevice->CreateMatrixTransform(&g_activeTransform);
    g_activeRootVisual->SetTransform(g_activeTransform);
    g_activeDCompTarget->SetRoot(g_activeRootVisual);
    g_dcompDevice->Commit();
    return true;
}

bool CaptureFullWindowToDCompSurface(HWND targetHwnd) {
    if (!g_activeDCompSurface || !g_d2dContext) return false;
    HDC hdcMem = CreateCompatibleDC(NULL);
    HBITMAP hBmp = CreateCompatibleBitmap(GetDC(targetHwnd), g_width, g_height);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);
    PrintWindow(targetHwnd, hdcMem, PW_RENDERFULLCONTENT);
    BITMAPINFO bmi = { 0 }; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = g_width; bmi.bmiHeader.biHeight = -g_height; bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    BYTE* pBits = new BYTE[g_width * g_height * 4]; GetDIBits(hdcMem, hBmp, 0, g_height, pBits, &bmi, DIB_RGB_COLORS);
    for (int i = 0; i < g_width * g_height; ++i) { pBits[i * 4 + 3] = 255; }
    IDXGISurface* dxgiSurface = nullptr; POINT offset;
    HRESULT hr = g_activeDCompSurface->BeginDraw(nullptr, __uuidof(IDXGISurface), (void**)&dxgiSurface, &offset);
    if (SUCCEEDED(hr) && dxgiSurface) {
        ID2D1Bitmap1* targetBitmap = nullptr; D2D1_BITMAP_PROPERTIES1 props = {}; props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM; props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED; props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        if (SUCCEEDED(g_d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface, &props, &targetBitmap))) {
            D2D1_SIZE_U sz = D2D1::SizeU(g_width, g_height); D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)); ID2D1Bitmap* srcBmp = nullptr;
            g_d2dContext->CreateBitmap(sz, pBits, g_width * 4, &bp, &srcBmp);
            if (srcBmp) {
                g_d2dContext->SetTarget(targetBitmap); g_d2dContext->BeginDraw(); g_d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
                D2D1_RECT_F destRect = D2D1::RectF((float)g_paddingX, (float)g_paddingY, (float)(g_paddingX + g_width), (float)(g_paddingY + g_height)); g_d2dContext->DrawBitmap(srcBmp, destRect);
                g_d2dContext->EndDraw(); g_d2dContext->SetTarget(nullptr); srcBmp->Release();
            }
            targetBitmap->Release();
        }
        dxgiSurface->Release(); g_activeDCompSurface->EndDraw(); g_dcompDevice->Commit();
    }
    delete[] pBits; SelectObject(hdcMem, hOld); DeleteObject(hBmp); DeleteDC(hdcMem); return true;
}

void UpdateVisualShear(float skew) {
    if (!g_activeTransform || !g_dcompDevice) return;
    float centerY = (float)g_paddingY + (float)g_height / 2.0f;
    MyMatrix3x2 matrix = { 1.0f, 0.0f, skew, 1.0f, -skew * centerY, 0.0f };
    g_activeTransform->SetMatrix(*reinterpret_cast<const D2D_MATRIX_3X2_F*>(&matrix));
    g_dcompDevice->Commit();
}

void CleanupClone() {
    if (g_activeTargetHwnd && IsWindow(g_activeTargetHwnd)) {
        if (!g_hadLayeredStyle) SetWindowLongPtr(g_activeTargetHwnd, GWL_EXSTYLE, g_origTargetExStyle);
        else SetLayeredWindowAttributes(g_activeTargetHwnd, 0, g_origAlpha, g_origFlags);
        SetWindowPos(g_activeTargetHwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        RedrawWindow(g_activeTargetHwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
    if (g_activeDCompSurface) { g_activeDCompSurface->Release(); g_activeDCompSurface = nullptr; }
    if (g_activeTransform) { g_activeTransform->Release(); g_activeTransform = nullptr; }
    if (g_activeRootVisual) { g_activeRootVisual->Release(); g_activeRootVisual = nullptr; }
    if (g_activeDCompTarget) { g_activeDCompTarget->Release(); g_activeDCompTarget = nullptr; }
    if (g_activeHwndClone && IsWindow(g_activeHwndClone)) { DestroyWindow(g_activeHwndClone); }
    g_activeHwndClone = nullptr; g_activeTargetHwnd = nullptr; g_isEffectActive = false; g_isTrackingDrag = false; g_currentSkew = 0.0f;
}

void ActivateDWMClone(HWND targetHwnd, POINT startPt) {
    if (g_activeHwndClone || !targetHwnd || !IsWindow(targetHwnd)) return;
    g_activeTargetHwnd = targetHwnd; g_initialMousePt = startPt; RECT rcTarget; HRESULT hrAttr = DwmGetWindowAttribute(g_activeTargetHwnd, DWMWA_EXTENDED_FRAME_BOUNDERS, &rcTarget, sizeof(RECT));
    if (FAILED(hrAttr) || (rcTarget.right - rcTarget.left <= 0)) GetWindowRect(g_activeTargetHwnd, &rcTarget);
    g_initialWindowPos = { rcTarget.left, rcTarget.top }; g_width = rcTarget.right - rcTarget.left; g_height = rcTarget.bottom - rcTarget.top;
    if (g_width <= 20 || g_height <= 20) return;
    g_paddingX = (int)(g_height * 0.45f) + 20; g_paddingY = 15; g_cloneWidth = g_width + 2 * g_paddingX; g_cloneHeight = g_height + 2 * g_paddingY;
    g_origTargetExStyle = GetWindowLongPtr(g_activeTargetHwnd, GWL_EXSTYLE); g_hadLayeredStyle = (g_origTargetExStyle & WS_EX_LAYERED) != 0;
    if (g_hadLayeredStyle) { COLORREF key; GetLayeredWindowAttributes(g_activeTargetHwnd, &key, &g_origAlpha, &g_origFlags); }
    HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(g_hMainWnd, GWLP_HINSTANCE);
    g_activeHwndClone = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST, L"WindowFXCloneClass", L"", WS_POPUP, g_initialWindowPos.x - g_paddingX, g_initialWindowPos.y - g_paddingY, g_cloneWidth, g_cloneHeight, nullptr, nullptr, hInstance, nullptr);
    if (g_activeHwndClone) {
        SetupTargetDComp(g_activeHwndClone); CaptureFullWindowToDCompSurface(g_activeTargetHwnd);
        SetWindowPos(g_activeHwndClone, HWND_TOPMOST, g_initialWindowPos.x - g_paddingX, g_initialWindowPos.y - g_paddingY, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetWindowLongPtr(g_activeTargetHwnd, GWL_EXSTYLE, g_origTargetExStyle | WS_EX_LAYERED); SetLayeredWindowAttributes(g_activeTargetHwnd, 0, 1, LWA_ALPHA);
        g_isEffectActive = true;
    }
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
        if (wParam == WM_LBUTTONDOWN) {
            POINT pt = pMouseStruct->pt;
            HWND target = WindowFromPoint(pt);
            while (target && GetParent(target)) { target = GetParent(target); }
            if (target) {
                wchar_t className[256] = { 0 };
                GetClassNameW(target, className, 256);

                bool isDesktop = (wcscmp(className, L"Shell_TrayWnd") == 0 ||
                    wcscmp(className, L"Progman") == 0 ||
                    wcscmp(className, L"WorkerW") == 0 ||
                    wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0 ||
                    wcscmp(className, L"WindowFXMainClass") == 0 ||
                    wcscmp(className, L"WindowFXCloneClass") == 0);

                if (!isDesktop) {
                    // 识别文件资源管理器窗口类名 (CabinetWClass / ExplorerWClass)
                    bool isExplorer = (wcscmp(className, L"CabinetWClass") == 0 || wcscmp(className, L"ExplorerWClass") == 0);

                    DWORD_PTR hitResult = 0;
                    if (SendMessageTimeoutW(target, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y), SMTO_ABORTIFHUNG, 100, &hitResult)) {
                        RECT rcWin;
                        GetWindowRect(target, &rcWin);
                        int relativeY = pt.x >= rcWin.left && pt.x <= rcWin.right ? (pt.y - rcWin.top) : 999;

                        // 判断规则：
                        // 1. 标准窗口返回 HTCAPTION
                        // 2. 资源管理器窗口，因为标签页原因会返回 HTCLIENT，但只要在顶部 45px 内且非最小/最大/关闭按钮，即认定为标题栏拖动！
                        bool isTitleBar = (hitResult == HTCAPTION) ||
                            (isExplorer && relativeY >= 0 && relativeY <= 45 &&
                                hitResult != HTCLOSE && hitResult != HTMINBUTTON && hitResult != HTMAXBUTTON);

                        if (isTitleBar) {
                            g_isTrackingDrag = true;
                            g_initialMousePt = pt;
                            g_activeTargetHwnd = target;
                        }
                    }
                }
            }
        }
        else if (wParam == WM_MOUSEMOVE) {
            POINT pt = pMouseStruct->pt;
            if (g_isTrackingDrag && !g_isEffectActive && g_activeTargetHwnd && IsWindow(g_activeTargetHwnd)) {
                int dx = abs(pt.x - g_initialMousePt.x); int dy = abs(pt.y - g_initialMousePt.y);
                if (dx > 8 || dy > 8) { g_isTrackingDrag = false; ActivateDWMClone(g_activeTargetHwnd, g_initialMousePt); }
            }
            else if (g_isEffectActive && g_activeHwndClone) {
                int deltaX = pt.x - g_initialMousePt.x; int deltaY = pt.y - g_initialMousePt.y;
                static int lastMouseX = pt.x; int vx = pt.x - lastMouseX; lastMouseX = pt.x;
                float targetSkew = (float)vx * -0.015f; targetSkew = std::clamp(targetSkew, -0.3f, 0.3f);
                g_currentSkew += (targetSkew - g_currentSkew) * 0.6f; UpdateVisualShear(g_currentSkew);
                int cloneNewX = g_initialWindowPos.x + deltaX - g_paddingX; int cloneNewY = g_initialWindowPos.y + deltaY - g_paddingY;
                SetWindowPos(g_activeHwndClone, NULL, cloneNewX, cloneNewY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
            }
        }
        else if (wParam == WM_LBUTTONUP) {
            if (g_isEffectActive && g_activeHwndClone) {
                POINT ptEnd = pMouseStruct->pt; int deltaX = ptEnd.x - g_initialMousePt.x; int deltaY = ptEnd.y - g_initialMousePt.y;
                int targetFinalX = g_initialWindowPos.x + deltaX; int targetFinalY = g_initialWindowPos.y + deltaY;
                SetWindowPos(g_activeTargetHwnd, NULL, targetFinalX, targetFinalY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                CleanupClone();
            }
            g_isTrackingDrag = false;
        }
    }
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK CloneWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        StringCchCopyW(g_nid.szTip, ARRAYSIZE(g_nid.szTip), L"Jelly Drag FX (果冻窗口守护程序)");
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit (退出程序)");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
        }
        break;

    case WM_DESTROY:
        if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"WindowFXMainClass";
    if (!RegisterClassW(&wc)) return 0;

    WNDCLASSW wcClone = {};
    wcClone.lpfnWndProc = CloneWndProc;
    wcClone.hInstance = hInstance;
    wcClone.lpszClassName = L"WindowFXCloneClass";
    wcClone.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcClone.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    RegisterClassW(&wcClone);

    g_hMainWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"WindowFXMainClass", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!g_hMainWnd) return 0;

    g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);
    if (!g_mouseHook) {
        DestroyWindow(g_hMainWnd);
        return 0;
    }

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_d2dContext) { g_d2dContext->Release(); g_d2dContext = nullptr; }
    if (g_d2dDevice) { g_d2dDevice->Release(); g_d2dDevice = nullptr; }
    if (g_d2dFactory) { g_d2dFactory->Release(); g_d2dFactory = nullptr; }
    if (g_dcompDevice) { g_dcompDevice->Release(); g_dcompDevice = nullptr; }
    if (g_d3dDevice) { g_d3dDevice->Release(); g_d3dDevice = nullptr; }

    return (int)msg.wParam;
}