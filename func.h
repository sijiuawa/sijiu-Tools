#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <system_error>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <stack>
#include <cctype>      // std::isdigit
#include <functional>  // std::function

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#endif

// ==================== 常量定义 ====================
namespace ConstPath {
    const std::string ASS_DIR = "./.ass/";
    const std::string MOD_LIST_FILE = ASS_DIR + "mod_list.ini";
    const std::string CONFIG_FILE = ASS_DIR + "config.ini";
    const std::string LOG_FILE = "log.txt";
    const std::string DLL_DIR = "./dlls";
    const std::string MODS_DIR = "./mods";
    const std::string LANG_DIR = "./languages";
}

// ==================== 插件接口 ====================
struct PluginCommand {
    const char* command;      // 命令名称（假设为 UTF-8）
    void (*function)();       // 命令函数
    const char* description;  // 命令描述（UTF-8）
};

// 命令条目（替代 tuple，更清晰）
struct CommandEntry {
    std::string name;
    void (*func)() = nullptr;
    std::string description;
};

struct PluginInfo {
    HMODULE handle = nullptr;
    std::string name;                 // 插件名（UTF-8）
    std::string dllPath;               // DLL 完整路径（UTF-8）
    std::vector<CommandEntry> commands; // 转换后的命令列表
};

// ==================== DLL 管理器 ====================
class DLLManager {
public:
    DLLManager();
    ~DLLManager();

    void loadAll(const std::string& directory);   // 加载目录下所有 DLL
    void reload();                                 // 重新加载
    bool unload(const std::string& pluginName);    // 卸载指定插件
    bool execute(const std::string& command);      // 执行插件命令
    const std::map<std::string, PluginInfo>& plugins() const { return plugins_; }
    std::vector<std::pair<std::string, std::string>> commandHelp() const;

private:
    bool loadSingle(const std::string& dllPath);
    void unloadAll();

    std::string dllDir_{ ConstPath::DLL_DIR };
    std::map<std::string, PluginInfo> plugins_;
};

extern DLLManager g_dllManager;

// ==================== 任务调度器 ====================
class TaskScheduler {
public:
    TaskScheduler();
    ~TaskScheduler();

    void add(int delaySeconds, const std::string& module);
    bool cancel(int id);
    void list() const;

private:
    struct Task {
        int id;
        std::chrono::system_clock::time_point executeTime;
        std::string module;
        bool cancelled = false;
        bool done = false;
    };

    std::vector<Task> tasks_;
    int nextId_ = 1;
    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> stop_{ false };
    std::condition_variable cv_;

    void workerLoop();
};

extern TaskScheduler g_taskScheduler;

// ==================== 全局 debug 标志 ====================
extern bool g_debugMode;

// ==================== 全局辅助函数 ====================
std::wstring utf8ToWide(const std::string& utf8);
std::string wideToUtf8(const std::wstring& wstr);
std::string ansiToUtf8(const char* ansiStr);

bool fileExists(const std::string& path);
bool dirExists(const std::string& path);
bool pathExists(const std::string& path);
bool createDirRecursive(const std::string& path);
std::vector<std::string> getDllFiles(const std::string& directory);
bool copyDir(const std::string& src, const std::string& dest, int depth, std::function<void(size_t, size_t)> progressCb = nullptr);
bool deleteDir(const std::string& path, std::function<void(size_t, size_t)> progressCb = nullptr);
std::string getCurrentExeDir();
void ensureDirs();

// 统计目录下文件总数（包括子目录）
size_t countFilesInDir(const std::string& dir);

// ==================== 模块管理函数 ====================
bool launchModule(const std::string& identifier);
bool deleteModule(const std::string& identifier);
bool errHandler(char errType, const std::string& modName);
std::string extractModuleName(const std::string& line);
std::vector<std::string> parseCommand(const std::string& cmd);
short executeCommand(const std::string& cmdLine);

// ==================== 内置命令 ====================
void cmdInm();
void cmdInm(const std::vector<std::string>& args);
void cmdHelp();
void cmdHelp(const std::vector<std::string>& args);
void cmdStart();
void cmdStart(const std::vector<std::string>& args);
void cmdReg();
void cmdReg(const std::vector<std::string>& args, const std::vector<std::string>& opts);
void cmdDebug(const std::vector<std::string>& args);
bool cmdAdm();
void logEvent(char type, const std::string& msg = "");
std::string readFileContent(const std::string& filename, const std::string& startLine, const std::string& endLine);
std::string readLanguage(const std::string& key);
std::string formatSystemTime(const SYSTEMTIME& st);
inline std::string intToStr(int val) { return std::to_string(val); }