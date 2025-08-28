#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <algorithm>

struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };
struct Entity {
    Vector3 position_feet;
    Vector3 position_head;
    int health;
    int team;
};

namespace offsets {
    constexpr uintptr_t entityList = 0x18AC04;
    constexpr uintptr_t playerCount = 0x18AC0C;
    constexpr uintptr_t view_matrix = 0x17DFD0;
    constexpr uintptr_t health = 0xEC;
    constexpr uintptr_t team = 0x30C;
    constexpr uintptr_t pos_z_feet = 0x28;
    constexpr uintptr_t pos_x_feet = 0x2C;
    constexpr uintptr_t pos_y_feet = 0x30;
    constexpr uintptr_t pos_x_head = 0x4;
    constexpr uintptr_t pos_z_head = 0x8;
    constexpr uintptr_t pos_y_head = 0xC;
}

HWND GameWindow, OverlayWindow;
int windowWidth, windowHeight;
HDC hdc, memDC;
HBITMAP memBitmap;
HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
HPEN enemyPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));

DWORD GetProcId(const wchar_t* procName);
uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName);
bool WorldToScreen(Vector3 pos, Vector2& screen, const float matrix[16], int winWidth, int winHeight);
DWORD WINAPI EspThread(LPVOID lpParameter);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int main() {
    SetConsoleTitleA("ESP AssaultCube");
    DWORD procId = GetProcId(L"ac_client.exe");
    if (!procId) { std::cout << "Processo nao encontrado.\n"; system("pause"); return 1; }
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, procId);
    if (!hProcess) { std::cout << "Falha ao abrir processo.\n"; system("pause"); return 1; }
    GameWindow = FindWindowA(NULL, "AssaultCube");
    if (!GameWindow) { std::cout << "Janela do jogo nao encontrada.\n"; system("pause"); CloseHandle(hProcess); return 1; }
    RECT gameRect;
    GetClientRect(GameWindow, &gameRect);
    windowWidth = gameRect.right - gameRect.left;
    windowHeight = gameRect.bottom - gameRect.top;
    POINT topLeft = { gameRect.left, gameRect.top };
    ClientToScreen(GameWindow, &topLeft);
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_VREDRAW | CS_HREDRAW, WindowProc, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"ESPOverlay", NULL };
    RegisterClassEx(&wc);
    OverlayWindow = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT, wc.lpszClassName, L"ESP", WS_POPUP, topLeft.x, topLeft.y, windowWidth, windowHeight, NULL, NULL, wc.hInstance, NULL);
    SetLayeredWindowAttributes(OverlayWindow, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(OverlayWindow, SW_SHOW);
    hdc = GetDC(OverlayWindow);
    memDC = CreateCompatibleDC(hdc);
    memBitmap = CreateCompatibleBitmap(hdc, windowWidth, windowHeight);
    std::cout << "ESP Ativado! Pressione 'END' para fechar.\n";
    CreateThread(nullptr, 0, EspThread, hProcess, 0, nullptr);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    DeleteObject(enemyPen);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(OverlayWindow, hdc);
    DestroyWindow(OverlayWindow);
    CloseHandle(hProcess);
    return 0;
}

DWORD WINAPI EspThread(LPVOID lpParameter) {
    HANDLE hProcess = (HANDLE)lpParameter;
    uintptr_t baseAddr = GetModuleBaseAddress(GetProcessId(hProcess), L"ac_client.exe");
    while (!(GetAsyncKeyState(VK_END) & 1)) {
        RECT gameRect;
        GetClientRect(GameWindow, &gameRect);
        windowWidth = gameRect.right - gameRect.left;
        windowHeight = gameRect.bottom - gameRect.top;
        POINT topLeft = { gameRect.left, gameRect.top };
        ClientToScreen(GameWindow, &topLeft);
        MoveWindow(OverlayWindow, topLeft.x, topLeft.y, windowWidth, windowHeight, TRUE);
        uintptr_t entityListPtr = 0;
        ReadProcessMemory(hProcess, (LPCVOID)(baseAddr + offsets::entityList), &entityListPtr, sizeof(entityListPtr), nullptr);
        int numPlayers = 0;
        ReadProcessMemory(hProcess, (LPCVOID)(baseAddr + offsets::playerCount), &numPlayers, sizeof(numPlayers), nullptr);

        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);
        BitBlt(memDC, 0, 0, windowWidth, windowHeight, NULL, 0, 0, BLACKNESS);

        if (!entityListPtr || numPlayers <= 1) {
            BitBlt(hdc, 0, 0, windowWidth, windowHeight, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBitmap);
            Sleep(100);
            continue;
        }

        float viewMatrix[16] = {};
        ReadProcessMemory(hProcess, (LPCVOID)(baseAddr + offsets::view_matrix), &viewMatrix, sizeof(viewMatrix), nullptr);

        if (numPlayers > 1 && numPlayers < 32) {
            for (int i = 1; i < numPlayers; i++) {
                uintptr_t entityPtr = 0;
                ReadProcessMemory(hProcess, (LPCVOID)(entityListPtr + i * sizeof(uintptr_t)), &entityPtr, sizeof(entityPtr), nullptr);
                if (!entityPtr) continue;
                Entity e = {};
                ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + offsets::health), &e.health, sizeof(e.health), nullptr);
                if (e.health < 1 || e.health > 150) continue;

                ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + offsets::pos_x_feet), &e.position_feet.x, sizeof(float), nullptr);
                ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + offsets::pos_z_feet), &e.position_feet.y, sizeof(float), nullptr);
                ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + offsets::pos_y_feet), &e.position_feet.z, sizeof(float), nullptr);

                ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + offsets::pos_x_head), &e.position_head.x, sizeof(float), nullptr);
                ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + offsets::pos_z_head), &e.position_head.y, sizeof(float), nullptr);
                ReadProcessMemory(hProcess, (LPCVOID)(entityPtr + offsets::pos_y_head), &e.position_head.z, sizeof(float), nullptr);

                Vector2 screen_feet, screen_head;
                if (WorldToScreen(e.position_feet, screen_feet, viewMatrix, windowWidth, windowHeight) && WorldToScreen(e.position_head, screen_head, viewMatrix, windowWidth, windowHeight)) {
                    SelectObject(memDC, enemyPen);
                    SelectObject(memDC, nullBrush);
                    int top = static_cast<int>(std::min(screen_head.y, screen_feet.y));
                    int bottom = static_cast<int>(std::max(screen_head.y, screen_feet.y));
                    float boxHeight = bottom - top;
                    float boxWidth = boxHeight / 2.0f;
                    int left = static_cast<int>(screen_head.x - boxWidth / 2);
                    int right = static_cast<int>(screen_head.x + boxWidth / 2);
                    Rectangle(memDC, left, top, right, bottom);
                }
            }
        }
        BitBlt(hdc, 0, 0, windowWidth, windowHeight, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        Sleep(5);
    }
    PostQuitMessage(0);
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

DWORD GetProcId(const wchar_t* procName) {
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 procEntry;
        procEntry.dwSize = sizeof(procEntry);
        if (Process32First(hSnap, &procEntry)) {
            do {
                if (!_wcsicmp(procEntry.szExeFile, procName)) {
                    procId = procEntry.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnap, &procEntry));
        }
    }
    CloseHandle(hSnap);
    return procId;
}

uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 modEntry;
        modEntry.dwSize = sizeof(modEntry);
        if (Module32First(hSnap, &modEntry)) {
            do {
                if (!_wcsicmp(modEntry.szModule, modName)) {
                    modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
                    break;
                }
            } while (Module32Next(hSnap, &modEntry));
        }
    }
    CloseHandle(hSnap);
    return modBaseAddr;
}

bool WorldToScreen(Vector3 pos, Vector2& screen, const float matrix[16], int winWidth, int winHeight) {
    float clipCoords[4];
    clipCoords[0] = pos.x * matrix[0] + pos.y * matrix[4] + pos.z * matrix[8] + matrix[12];
    clipCoords[1] = pos.x * matrix[1] + pos.y * matrix[5] + pos.z * matrix[9] + matrix[13];
    clipCoords[2] = pos.x * matrix[2] + pos.y * matrix[6] + pos.z * matrix[10] + matrix[14];
    clipCoords[3] = pos.x * matrix[3] + pos.y * matrix[7] + pos.z * matrix[11] + matrix[15];
    if (clipCoords[3] < 0.1f) return false;
    Vector3 NDC;
    NDC.x = clipCoords[0] / clipCoords[3];
    NDC.y = clipCoords[1] / clipCoords[3];
    screen.x = (winWidth / 2.0f * NDC.x) + (winWidth / 2.0f);
    screen.y = -(winHeight / 2.0f * NDC.y) + (winHeight / 2.0f);
    return true;
}