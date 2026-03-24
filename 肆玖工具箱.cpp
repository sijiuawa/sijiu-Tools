#include <iostream>
#include <windows.h>
#include "func.h"

int main() {
#ifdef WIN32
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"肆玖工具箱_Instance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"程序已在运行中", L"提示", MB_OK | MB_ICONINFORMATION);

        HWND hWnd = FindWindowW(L"ConsoleWindowClass", L"肆玖工具箱");
        if (hWnd == nullptr)
            hWnd = FindWindowW(nullptr, L"肆玖工具箱");

        if (hWnd != nullptr) {
            if (IsIconic(hWnd)) ShowWindow(hWnd, SW_RESTORE);
            DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            DWORD targetThread = GetWindowThreadProcessId(hWnd, nullptr);
            if (foregroundThread != targetThread) {
                AttachThreadInput(foregroundThread, targetThread, TRUE);
                SetForegroundWindow(hWnd);
                AttachThreadInput(foregroundThread, targetThread, FALSE);
            }
            else {
                SetForegroundWindow(hWnd);
            }
        }
        CloseHandle(hMutex);
        return 0;
    }
#endif

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::ios::sync_with_stdio(false);

    try {
        ensureDirs();
        logEvent('p', u8"程序启动");
        std::cout << u8"[肆玖工具箱] 版本1.00.0" << std::endl;

        HWND hwnd = GetConsoleWindow();
        SetWindowTextW(hwnd, L"肆玖工具箱");

        std::cout << u8"加载DLL插件..." << std::endl;
        g_dllManager.loadAll("./dlls");

        bool running = true;
        while (running) {

            if (!std::cin) {
                std::cin.clear();
                std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            }

            std::cout << u8"请输入指令> ";
            std::cout.flush();

            if (!std::cin) {
                std::cin.clear();
                std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            }

            std::string cmd;
            if (!std::getline(std::cin, cmd)) {
                if (std::cin.eof()) {
                    std::cin.clear();
                    std::cout << u8"输入结束，退出。" << std::endl;
                    break;
                }
                logEvent('e', u8"输入流错误");
                break;
            }
            if (cmd.empty()) continue;

            if (cmd == "exit") {
                running = false;
                continue;
            }
            executeCommand(cmd);
        }

        logEvent('p', u8"程序正常退出");
        std::cout << u8"按任意键继续...";
        std::cin.get();
    }
    catch (const std::exception& e) {
        logEvent('e', std::string(u8"致命异常: ") + e.what());
        std::cerr << u8"程序异常终止: " << e.what() << std::endl;
    }
    catch (...) {
        logEvent('e', u8"未知致命异常");
        std::cerr << u8"未知错误" << std::endl;
    }

#ifdef WIN32
    CloseHandle(hMutex);
#endif
    return 0;
}