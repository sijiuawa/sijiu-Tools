#include <iostream>
#include <windows.h>
#include "func.h"

int main() {
#ifdef WIN32

    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"肆玖工具箱_Instance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      
        MessageBoxW(nullptr, L"程序已在运行中", L"提示", MB_OK | MB_ICONINFORMATION);
        
        // 查找已有实例的窗口
        HWND hWnd = FindWindowW(L"ConsoleWindowClass", L"肆玖工具箱");  // 按类名和标题查找
        if (hWnd == nullptr) {
            // 若未找到，尝试仅按标题查找（兼容不同Windows版本）
            hWnd = FindWindowW(nullptr, L"肆玖工具箱");
        }
        
        if (hWnd != nullptr) {
            // 如果窗口最小化，则还原
            if (IsIconic(hWnd)) {
                ShowWindow(hWnd, SW_RESTORE);
            }
            // 尝试将窗口置前（处理前台锁限制）
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
        return 0;
    }

#endif
    // 设置控制台为 UTF-8
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
    return 0;
}