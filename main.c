#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include "hardware_monitor.h"

#define ID_TIMER 1
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001

#define WINDOW_WIDTH 450
#define WINDOW_HEIGHT 380

NOTIFYICONDATA nid;

void AddTrayIcon(HWND hwnd) {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
    strcpy(nid.szTip, "Hardware Monitor Overlay");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, "Salir");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void DrawGraph(HDC hdc, RECT rect, double* history, int index, int count, COLORREF color) {
    if (count == 0) return;
    
    HPEN hPen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    
    double step_x = (double)(rect.right - rect.left) / (HISTORY_SIZE - 1);
    
    int start_idx = (index - count + 1 + HISTORY_SIZE) % HISTORY_SIZE;
    
    for (int i = 0; i < count; i++) {
        int arr_idx = (start_idx + i) % HISTORY_SIZE;
        double val = history[arr_idx];
        
        int x = rect.left + (int)(i * step_x);
        int y = rect.bottom - (int)((val / 100.0) * (rect.bottom - rect.top));
        
        if (i == 0) {
            MoveToEx(hdc, x, y, NULL);
        } else {
            LineTo(hdc, x, y);
        }
    }
    
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);
}

void DrawColoredText(HDC hdc, int x, int y, const char* label, COLORREF labelColor, const char* value1, const char* value2, COLORREF valColor) {
    SetTextColor(hdc, labelColor);
    TextOutA(hdc, x, y, label, strlen(label));
    
    SetTextColor(hdc, valColor);
    if (value1) {
        TextOutA(hdc, x + 120, y, value1, strlen(value1));
    }
    if (value2) {
        TextOutA(hdc, x + 190, y, value2, strlen(value2));
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, ID_TIMER, 1000, NULL);
            StartHardwareMonitor();
            AddTrayIcon(hwnd);
            break;

        case WM_TIMER:
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowContextMenu(hwnd);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            HDC hMemDC = CreateCompatibleDC(hdc);
            HBITMAP hMemBmp = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);

            HBRUSH bgBrush = CreateSolidBrush(RGB(255, 0, 255));
            FillRect(hMemDC, &clientRect, bgBrush);
            DeleteObject(bgBrush);

            HardwareMetrics metrics = GetLatestMetrics();
            SystemSpecs specs = GetSystemSpecs();

            SetBkMode(hMemDC, TRANSPARENT);
            
            HFONT hFont = CreateFont(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
            HFONT oldFont = (HFONT)SelectObject(hMemDC, hFont);

            int yOffset = 10;
            int xOffset = 10;
            char buf1[64], buf2[64];

            SetTextColor(hMemDC, RGB(180, 180, 180)); 
            TextOutA(hMemDC, xOffset, yOffset, specs.cpu_name, strlen(specs.cpu_name)); yOffset += 24;
            TextOutA(hMemDC, xOffset, yOffset, specs.gpu_name, strlen(specs.gpu_name)); yOffset += 30;

            COLORREF colorOrange = RGB(255, 120, 0);
            COLORREF colorCyan = RGB(0, 200, 255);
            COLORREF colorGreen = RGB(0, 255, 100);
            COLORREF colorPink = RGB(255, 100, 150);

            sprintf(buf1, "%.0f %%", metrics.gpu_usage_percent);
            if (metrics.gpu_temp > 0) {
                sprintf(buf2, "%.0f C", metrics.gpu_temp);
                DrawColoredText(hMemDC, xOffset, yOffset, "GPU", colorGreen, buf1, buf2, colorOrange);
            } else {
                DrawColoredText(hMemDC, xOffset, yOffset, "GPU", colorGreen, buf1, NULL, colorOrange);
            }
            yOffset += 26;

            sprintf(buf1, "%.0f%%", metrics.cpu_usage_percent);
            sprintf(buf2, "%.0f C", metrics.cpu_temp);
            DrawColoredText(hMemDC, xOffset, yOffset, "CPU", colorCyan, buf1, buf2, colorOrange);
            yOffset += 26;

            sprintf(buf1, "%.1f GB", metrics.ram_used_gb);
            sprintf(buf2, "%.0f %%", metrics.ram_usage_percent);
            DrawColoredText(hMemDC, xOffset, yOffset, "RAM", colorPink, buf1, buf2, colorOrange);
            yOffset += 26;

            if (metrics.is_gaming_fullscreen && metrics.fps > 0.0) {
                sprintf(buf1, "%.0f", metrics.fps);
                DrawColoredText(hMemDC, xOffset, yOffset, "FPS", colorPink, buf1, NULL, RGB(255, 255, 255));
                yOffset += 40;
            } else {
                yOffset += 40; 
            }

            RECT graphRectCPU = { xOffset, yOffset, clientRect.right - 10, yOffset + 40 };
            DrawGraph(hMemDC, graphRectCPU, metrics.cpu_history, metrics.history_index, metrics.history_count, colorCyan);
            yOffset += 50;

            RECT graphRectGPU = { xOffset, yOffset, clientRect.right - 10, yOffset + 40 };
            DrawGraph(hMemDC, graphRectGPU, metrics.gpu_history, metrics.history_index, metrics.history_count, colorGreen);

            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hMemDC, 0, 0, SRCCOPY);

            SelectObject(hMemDC, oldFont);
            DeleteObject(hFont);
            SelectObject(hMemDC, hOldBmp);
            DeleteObject(hMemBmp);
            DeleteDC(hMemDC);

            EndPaint(hwnd, &ps);
            break;
        }

        case WM_DESTROY:
            RemoveTrayIcon();
            KillTimer(hwnd, ID_TIMER);
            StopHardwareMonitor();
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    const char CLASS_NAME[] = "HwMonitorOverlayClass";

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = NULL; 
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(101));

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        CLASS_NAME,                     
        "Hardware Analyzer Overlay",            
        WS_POPUP,
        10, 10, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL        
    );

    if (hwnd == NULL) {
        return 0;
    }

    SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
