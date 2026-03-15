#include "func.h"
#include <cctype>      // std::isdigit
#include <functional>  // std::function (已由 func.h 包含，但为明确保留)
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

// ==================== 编码转换 ====================
std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], len);
    return wstr;
}

std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string str(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, nullptr, nullptr);
    return str;
}

std::string ansiToUtf8(const char* ansiStr) {
    if (!ansiStr) return "";
    int wlen = MultiByteToWideChar(CP_ACP, 0, ansiStr, -1, nullptr, 0);
    if (wlen <= 1) return "";
    std::wstring wstr(wlen - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansiStr, -1, &wstr[0], wlen - 1);
    return wideToUtf8(wstr);
}

// ==================== 路径操作 ====================
bool fileExists(const std::string& path) {
    DWORD attr = GetFileAttributesW(utf8ToWide(path).c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool dirExists(const std::string& path) {
    DWORD attr = GetFileAttributesW(utf8ToWide(path).c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool pathExists(const std::string& path) {
    return GetFileAttributesW(utf8ToWide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool createDirRecursive(const std::string& path) {
    std::wstring wpath = utf8ToWide(path);
    wchar_t tmp[MAX_PATH];
    if (wpath.length() >= MAX_PATH) {
        std::cerr << u8"路径太长: " << path << std::endl;
        return false;
    }
    wcscpy_s(tmp, MAX_PATH, wpath.c_str());
    // 去除末尾分隔符
    size_t len = wcslen(tmp);
    while (len > 0 && (tmp[len - 1] == L'\\' || tmp[len - 1] == L'/')) {
        tmp[--len] = L'\0';
    }
    for (wchar_t* p = tmp + 1; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t old = *p;
            *p = L'\0';
            CreateDirectoryW(tmp, nullptr);
            *p = old;
        }
    }
    CreateDirectoryW(tmp, nullptr);
    return true;
}

std::vector<std::string> getDllFiles(const std::string& dir) {
    std::vector<std::string> files;
    std::wstring wdir = utf8ToWide(dir);
    // 规范化路径
    wchar_t absPath[MAX_PATH];
    if (GetFullPathNameW(wdir.c_str(), MAX_PATH, absPath, nullptr) == 0) {
        DWORD err = GetLastError();
        std::cerr << u8"获取绝对路径失败: " << dir << u8" 错误码: " << err << std::endl;
        return files;
    }
    wdir = absPath;
    // 去除末尾分隔符
    while (!wdir.empty() && (wdir.back() == L'\\' || wdir.back() == L'/')) {
        wdir.pop_back();
    }

    std::wstring search = wdir + L"\\*.dll";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND) {
            std::cerr << u8"查找DLL失败，错误码: " << err << std::endl;
        }
        return files;
    }

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring full = wdir + L"\\" + findData.cFileName;
            files.push_back(wideToUtf8(full));
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    return files;
}

bool copyDir(const std::string& src, const std::string& dest) {
    std::wstring wsrc = utf8ToWide(src);
    std::wstring wdest = utf8ToWide(dest);

    if (!CreateDirectoryW(wdest.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        std::cerr << u8"创建目标目录失败: " << dest << std::endl;
        return false;
    }

    std::wstring search = wsrc + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool success = true;
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;

        std::wstring srcPath = wsrc + L"\\" + findData.cFileName;
        std::wstring destPath = wdest + L"\\" + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!copyDir(wideToUtf8(srcPath), wideToUtf8(destPath))) {
                success = false;
                break;
            }
        }
        else {
            if (!CopyFileW(srcPath.c_str(), destPath.c_str(), FALSE)) {
                success = false;
                break;
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
    return success;
}

bool deleteDir(const std::string& path) {
    std::wstring wpath = utf8ToWide(path);
    if (!pathExists(path)) {
        return true; // 不存在视为成功
    }

    // SHFileOperation 要求路径以双 null 结尾
    std::wstring from = wpath + L'\0' + L'\0';

    SHFILEOPSTRUCTW fo = { 0 };
    fo.wFunc = FO_DELETE;
    fo.pFrom = from.c_str();
    fo.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI;

    int result = SHFileOperationW(&fo);
    if (result != 0) {
        DWORD err = GetLastError();
        std::cerr << u8"SHFileOperation 失败，错误码: " << err << std::endl;
        return false;
    }
    return true;
}

std::string getCurrentExeDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return wideToUtf8(buffer);
}

void ensureDirs() {
    createDirRecursive("./.ass");
    createDirRecursive("./mods");
    createDirRecursive("./languages");
    createDirRecursive("./dlls");
}

// ==================== DLLManager ====================
DLLManager g_dllManager;

DLLManager::DLLManager() {
    try {
        if (!pathExists(dllDir_)) createDirRecursive(dllDir_);
    }
    catch (const std::exception& e) {
        logEvent('e', std::string(u8"DLLManager初始化异常: ") + e.what());
    }
    catch (...) {
        logEvent('e', u8"DLLManager初始化未知异常");
    }
}

DLLManager::~DLLManager() { unloadAll(); }

void DLLManager::loadAll(const std::string& dir) {
    unloadAll();
    auto files = getDllFiles(dir);
    for (const auto& f : files) loadSingle(f);
    logEvent('d', u8"DLL加载完成");
}

bool DLLManager::loadSingle(const std::string& dllPath) {
    std::wstring wpath = utf8ToWide(dllPath);
    if (wpath.empty()) {
        logEvent('e', u8"路径转换失败: " + dllPath);
        return false;
    }

    wchar_t fullPath[MAX_PATH];
    if (GetFullPathNameW(wpath.c_str(), MAX_PATH, fullPath, nullptr) > 0) {
        wpath = fullPath;
    }

    HMODULE hMod = LoadLibraryW(wpath.c_str());
    if (!hMod) {
        DWORD err = GetLastError();
        LPWSTR msgBuf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&msgBuf, 0, nullptr);
        std::wstring errMsg = msgBuf ? msgBuf : L"未知错误";
        LocalFree(msgBuf);
        logEvent('e', u8"无法加载DLL: " + dllPath + u8" 错误: " + wideToUtf8(errMsg));
        return false;
    }

    auto getName = (const char* (*)())GetProcAddress(hMod, "get_plugin_name");
    auto getInterface = (PluginCommand * (*)())GetProcAddress(hMod, "get_plugin_interface");
    if (!getName || !getInterface) {
        std::cerr << u8"DLL缺少必要导出函数: " << dllPath << std::endl;
        FreeLibrary(hMod);
        return false;
    }

    const char* name = getName();
    if (!name) {
        std::cerr << u8"DLL返回空名称" << std::endl;
        FreeLibrary(hMod);
        return false;
    }

    PluginCommand* cmds = getInterface();
    if (!cmds) {
        std::cerr << u8"DLL返回空接口" << std::endl;
        FreeLibrary(hMod);
        return false;
    }

    PluginInfo info;
    info.handle = hMod;
    info.name = ansiToUtf8(name);          // 插件名转 UTF-8
    info.dllPath = dllPath;                 // 保存原始路径（已为 UTF-8）

    // 遍历原始命令，转换并存储
    for (PluginCommand* p = cmds; p->command != nullptr; ++p) {
        std::string cmdUtf8 = ansiToUtf8(p->command);
        std::string descUtf8 = ansiToUtf8(p->description);
        info.commands.emplace_back(cmdUtf8, p->function, descUtf8);
    }
    plugins_[info.name] = info;

    std::cout << u8"成功加载插件: " << info.name << u8" (" << dllPath << u8")" << std::endl;
    logEvent('d', u8"加载插件: " + info.name);
    return true;
}

void DLLManager::reload() { loadAll(dllDir_); }

bool DLLManager::unload(const std::string& pluginName) {
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        std::cerr << u8"未找到插件: " << pluginName << std::endl;
        return false;
    }
    if (FreeLibrary(it->second.handle)) {
        plugins_.erase(it);
        std::cout << u8"已卸载插件: " << pluginName << std::endl;
        logEvent('d', u8"卸载插件: " + pluginName);
        return true;
    }
    DWORD err = GetLastError();
    std::cerr << u8"卸载失败: " << pluginName << u8" 错误码: " << err << std::endl;
    logEvent('e', u8"卸载失败: " + pluginName);
    return false;
}

void DLLManager::unloadAll() {
    for (auto& [_, info] : plugins_) FreeLibrary(info.handle);
    plugins_.clear();
}

bool DLLManager::execute(const std::string& cmd) {
    for (const auto& [_, plugin] : plugins_) {
        for (const auto& tup : plugin.commands) {
            if (cmd == std::get<0>(tup)) {   // 比较命令名（UTF-8）
                std::get<1>(tup)();           // 执行函数
                return true;
            }
        }
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> DLLManager::commandHelp() const {
    std::vector<std::pair<std::string, std::string>> res;
    for (const auto& [_, plugin] : plugins_) {
        for (const auto& tup : plugin.commands) {
            res.emplace_back(std::get<0>(tup), std::get<2>(tup));
        }
    }
    return res;
}

// ==================== TaskScheduler ====================
TaskScheduler g_taskScheduler;

TaskScheduler::TaskScheduler() {
    try {
        worker_ = std::thread(&TaskScheduler::workerLoop, this);
    }
    catch (const std::system_error& e) {
        logEvent('e', std::string(u8"创建任务线程失败: ") + e.what());
    }
}

TaskScheduler::~TaskScheduler() {
    stop_ = true;
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void TaskScheduler::add(int delaySeconds, const std::string& module) {
    auto execTime = std::chrono::system_clock::now() + std::chrono::seconds(delaySeconds);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back({ nextId_++, execTime, module, false, false });
    }
    cv_.notify_one();
}

bool TaskScheduler::cancel(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& t : tasks_) {
        if (t.id == id) {
            t.cancelled = true;
            return true;
        }
    }
    return false;
}

void TaskScheduler::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty()) {
        std::cout << u8"当前没有计划任务" << std::endl;
        return;
    }
    auto now = std::chrono::system_clock::now();
    std::cout << u8"计划任务列表：" << std::endl;
    for (const auto& t : tasks_) {
        if (t.cancelled) continue;
        auto remain = std::chrono::duration_cast<std::chrono::seconds>(t.executeTime - now).count();
        if (remain < 0) remain = 0;
        std::cout << u8"ID: " << t.id << u8", 模块: " << t.module
            << u8", 剩余: " << remain << u8" 秒" << std::endl;
    }
}

void TaskScheduler::workerLoop() {
    while (!stop_) {
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(1));
            if (stop_) break;

            auto now = std::chrono::system_clock::now();
            std::vector<Task> ready;
            for (auto& t : tasks_) {
                if (!t.cancelled && !t.done && t.executeTime <= now) {
                    ready.push_back(t);
                    t.done = true;
                }
            }
            lock.unlock();

            for (const auto& t : ready) {
                if (!t.cancelled) {
                    std::cout << u8"执行计划任务: " << t.module << std::endl;
                    launchModule(t.module);
                }
            }

            lock.lock();
            tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                [](const Task& t) { return t.done || t.cancelled; }),
                tasks_.end());
        }
        catch (const std::exception& e) {
            logEvent('e', std::string(u8"任务线程异常: ") + e.what());
        }
        catch (...) {
            logEvent('e', u8"任务线程未知异常");
        }
    }
}

// ==================== 模块列表操作 ====================
static bool insertModuleToList(const std::string& name, const std::string& path, const std::string& exe, bool useModsDir) {
    const std::string listFile = "./.ass/mod_list.ini";
    std::vector<std::string> lines;
    std::string line;

    std::ifstream inFile(listFile);
    if (!inFile.is_open()) {
        std::cerr << u8"无法打开模块列表文件" << std::endl;
        return false;
    }

    while (std::getline(inFile, line)) lines.push_back(line);
    inFile.close();

    auto endIt = std::find_if(lines.begin(), lines.end(),
        [](const std::string& s) { return s == "end"; });
    if (endIt == lines.end()) {
        std::cerr << u8"模块列表缺少 end 标记" << std::endl;
        return false;
    }

    // 计算最大ID
    int maxId = 0;
    for (const auto& l : lines) {
        if (l.empty() || l == "end") continue;
        size_t dash = l.find('-');
        if (dash != std::string::npos) {
            try {
                int id = std::stoi(l.substr(0, dash));
                if (id > maxId) maxId = id;
            }
            catch (...) {}
        }
    }

    std::string entryPath = useModsDir ? ("./mods/" + name + "/" + exe) : (path + "/" + exe);
    if (!pathExists(entryPath)) {
        std::cout << u8"路径无效，安装失败" << std::endl;
        logEvent('r', u8"安装失败：路径无效");
        return false;
    }

    std::string newEntry = std::to_string(maxId + 1) + "-[" + name + "]   \"" + entryPath + "\"";
    lines.insert(endIt, newEntry);

    std::ofstream outFile(listFile, std::ios::trunc);
    if (!outFile.is_open()) {
        std::cerr << u8"无法写入模块列表文件" << std::endl;
        return false;
    }
    for (const auto& l : lines) outFile << l << std::endl;
    outFile.close();
    return true;
}

// ==================== 命令处理 ====================
std::vector<std::string> parseCommand(const std::string& cmd) {
    std::vector<std::string> tokens;
    std::stringstream ss(cmd);
    std::string token;
    while (ss >> token) tokens.push_back(token);
    return tokens;
}

short executeCommand(const std::string& cmdLine) {
    auto tokens = parseCommand(cmdLine);
    if (tokens.empty()) return 0;

    const std::string& cmd = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    if (cmd == "Was ist das?") {
        std::cout << u8"Bewege nicht den untersten Teil dieses Berges!" << std::endl;
        return 0;
    }
    else if (cmd == "Hello" || cmd == "hello") {
        std::cout << u8":)" << std::endl;
        return 0;
    }
    else if (cmd == "Creeper?") {
        std::cout << u8"Oh,Man!\n";
        return 0;
    }
    //彩蛋

    // 内置命令映射
    static const std::map<std::string, std::function<void(const std::vector<std::string>&)>> builtin = {
        {"inm", [](const auto& a) { if (a.empty()) cmdInm(); else cmdInm(a); }},
        {"help", [](const auto& a) { if (a.empty()) cmdHelp(); else cmdHelp(a); }},
        {"Adm", [](const auto&) { std::cout << (cmdAdm() ? u8"有管理员权限" : u8"无管理员权限") << std::endl; }},
        {"start", [](const auto& a) { if (a.empty()) cmdStart(); else cmdStart(a); }},
        {"reg", [](const auto& a) {
            if (!cmdAdm()) {
                std::cerr << u8"需要管理员权限" << std::endl;
                logEvent('r', u8"无权限安装");
                return;
            }
            if (a.empty()) cmdReg();
            else {
                std::vector<std::string> posArgs, opts;
                for (const auto& t : a) {
                    if (t == "-n") opts.push_back(t);
                    else if (t.rfind("name=",0) == 0 || t.rfind("path=",0) == 0 || t.rfind("exe=",0) == 0)
                        opts.push_back(t);
                    else posArgs.push_back(t);
                }
                cmdReg(posArgs, opts);
            }
        }},
        {"redll", [](const auto&) { g_dllManager.reload(); }},
        {"deldll", [](const auto& a) {
            // 解析参数
            bool unloadOnly = false;
            std::string identifier;
            for (const auto& arg : a) {
                if (arg == "-u") {
                    unloadOnly = true;
                }
         else if (identifier.empty()) {
          identifier = arg;   // 第一个非选项参数作为目标
      }
else {
 std::cerr << u8"多余参数，请只指定一个插件名或文件名" << std::endl;
 return;
}
}

            // 如果没有输入目标，交互式询问
            if (identifier.empty()) {
                std::cout << u8"请输入要处理的插件名或DLL文件名: ";
                std::getline(std::cin, identifier);
                if (identifier.empty()) return;
            }

            // 查找匹配的插件（精确匹配插件名，或文件名不含扩展名）
            std::string targetPluginName;
            std::string targetDllPath;
            const auto& plugins = g_dllManager.plugins();

            // 1. 精确匹配插件名
            auto it = plugins.find(identifier);
            if (it != plugins.end()) {
                targetPluginName = it->first;
                targetDllPath = it->second.dllPath;
            }
         else {
                // 2. 尝试按文件名匹配（不区分大小写，不含路径和扩展名）
                std::string lowerId = identifier;
                std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);
                std::vector<std::pair<std::string, std::string>> matches; // 插件名, dll路径

                for (const auto& [name, info] : plugins) {
                    // 从 dllPath 提取文件名（不含扩展名）
                    std::string dllName = info.dllPath;
                    size_t pos = dllName.find_last_of("/\\");
                    if (pos != std::string::npos) dllName = dllName.substr(pos + 1);
                    // 去掉扩展名
                    size_t dot = dllName.rfind('.');
                    if (dot != std::string::npos) dllName = dllName.substr(0, dot);
                    std::transform(dllName.begin(), dllName.end(), dllName.begin(), ::tolower);

                    if (dllName == lowerId) {
                        matches.emplace_back(name, info.dllPath);
                    }
                }

                if (matches.empty()) {
                    std::cerr << u8"未找到插件: " << identifier << std::endl;
                    return;
                }
         else if (matches.size() > 1) {
          std::cerr << u8"找到多个匹配，请使用精确的插件名：" << std::endl;
          for (const auto& m : matches) {
              std::cerr << u8"  " << m.first << u8" (" << m.second << u8")" << std::endl;
          }
          return;
      }
else {
 targetPluginName = matches[0].first;
 targetDllPath = matches[0].second;
}
}

            // 执行卸载
            if (!g_dllManager.unload(targetPluginName)) {
                std::cerr << u8"卸载插件失败" << std::endl;
                return;
            }

            // 如果不是仅卸载，则删除 DLL 文件
            if (!unloadOnly) {
                std::wstring wpath = utf8ToWide(targetDllPath);
                if (DeleteFileW(wpath.c_str())) {
                    std::cout << u8"DLL文件已删除" << std::endl;
                    logEvent('d', u8"删除DLL文件: " + targetDllPath);
                }
         else {
          DWORD err = GetLastError();
          std::cerr << u8"删除DLL文件失败，错误码: " << err << std::endl;
          logEvent('e', u8"删除DLL文件失败: " + targetDllPath);
      }
  }
else {
 std::cout << u8"插件已卸载，文件保留" << std::endl;
}
}},
        {"listdll", [](const auto&) {
            std::cout << u8"\n已加载插件:" << std::endl;
            for (const auto& [name, _] : g_dllManager.plugins())
                std::cout << u8"- " << name << std::endl;
            std::cout << std::endl;
        }},
        {"tasks", [](const auto&) { g_taskScheduler.list(); }},
        {"canceltask", [](const auto& a) {
            if (a.empty()) {
                std::cerr << u8"需要指定任务ID" << std::endl;
                return;
            }
            try {
                int id = std::stoi(a[0]);
                if (g_taskScheduler.cancel(id))
                    std::cout << u8"任务已取消" << std::endl;
                else
                    std::cerr << u8"未找到任务ID " << id << std::endl;
            }
 catch (...) {
  std::cerr << u8"任务ID必须为数字" << std::endl;
}
}},
{"exit", [](const auto&) { /* 在外部处理退出 */ }}
    };

    auto it = builtin.find(cmd);
    if (it != builtin.end()) {
        if (cmd == "exit") return 1;
        it->second(args);
        return 0;
    }

    if (g_dllManager.execute(cmdLine)) return 0;

    std::cout << u8"未知指令: " << cmdLine << std::endl;
    logEvent('e', u8"无效命令: " + cmdLine);
    return 0;
}

// ==================== 内置命令实现 ====================
void cmdInm() {
    logEvent('i', u8"读取模块列表");
    std::cout << u8"--------------可用模块-------------------" << std::endl;
    readFileContent("mod_list.ini", " ", "end");
    std::cout << std::endl<<u8"------------------------------------------" << std::endl;
}
void cmdInm(const std::vector<std::string>&) { cmdInm(); }

void cmdHelp() {
    logEvent('h', u8"读取帮助");
    std::cout << u8"--------------帮助列表-------------------" << std::endl;
    readFileContent("config.ini", "helpl", "helpend");

    auto dllCmds = g_dllManager.commandHelp();
    if (!dllCmds.empty()) {
        std::cout << u8"\nDLL插件命令:" << std::endl;
        for (const auto& [cmd, desc] : dllCmds) {
            std::cout << cmd << u8" - " << desc << std::endl;
        }
    }
    std::cout << u8"------------------------------------------" << std::endl;
}
void cmdHelp(const std::vector<std::string>&) { cmdHelp(); }

bool cmdAdm() {
    logEvent('a', u8"权限检查");
    std::ifstream f("./.ass/config.ini");
    if (!f.is_open()) {
        logEvent('e', u8"无法打开 config.ini");
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("Administrator=") == 0) {
            std::string val = line.substr(14);
            val.erase(0, val.find_first_not_of(" \t"));
            val.erase(val.find_last_not_of(" \t") + 1);
            return (val == "1");
        }
    }
    return false;
}

void cmdStart() {
    cmdInm();
    std::cout << u8"请输入模块编号或名称: ";
    std::string input;
    std::getline(std::cin, input);
    if (std::cin.eof()) {
        std::cin.clear();
        std::cout << u8"已取消" << std::endl;
        return;
    }
    launchModule(input);
}
void cmdStart(const std::vector<std::string>& args) {
    // 解析参数：支持 -t time=数字 -pl -a -m/-s
    bool hasT = false, hasPl = false, hasA = false, unitMin = false;
    int timeVal = -1;
    std::vector<std::string> modules;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "-t") {
            hasT = true;
            if (i + 1 < args.size() && args[i + 1].rfind("time=", 0) == 0) {
                std::string num = args[++i].substr(5);
                try { timeVal = std::stoi(num); }
                catch (...) { std::cerr << u8"无效时间值" << std::endl; return; }
            }
            else {
                std::cerr << u8"-t 后需要 time=数字" << std::endl;
                return;
            }
        }
        else if (a == "-pl") hasPl = true;
        else if (a == "-a") hasA = true;
        else if (a == "-m") unitMin = true;
        else if (a == "-s") unitMin = false;
        else modules.push_back(a);
    }

    if (modules.empty()) {
        std::cerr << u8"未指定模块" << std::endl;
        return;
    }

    if (hasT) {
        int delay = timeVal;
        if (unitMin) delay *= 60;
        if (hasPl) {
            for (const auto& m : modules) g_taskScheduler.add(delay, m);
            std::cout << u8"已添加 " << modules.size() << u8" 个计划任务，延迟 " << delay << u8" 秒" << std::endl;
        }
        else {
            g_taskScheduler.add(delay, modules[0]);
            std::cout << u8"已添加任务，ID 可查看 tasks" << std::endl;
        }
    }
    else {
        if (hasPl) {
            for (const auto& m : modules) {
                std::cout << u8"启动: " << m << std::endl;
                if (!launchModule(m)) std::cerr << u8"失败: " << m << std::endl;
            }
        }
        else {
            launchModule(modules[0]);
        }
    }
    if (hasA) std::cout << u8"管理员标志 -a 已忽略" << std::endl;
}

void cmdReg() {
    std::string name, path, exe;
    bool nameOk = false;
    while (!nameOk) {
        std::cout << u8"请输入模块名: ";
        std::getline(std::cin, name);
        if (std::cin.eof()) { std::cin.clear(); return; }
        if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            std::cerr << u8"包含非法字符" << std::endl;
            continue;
        }
        if (name.empty()) {
            std::cerr << u8"模块名不能为空" << std::endl;
            continue;
        }
        // 检查重名
        std::ifstream f("./.ass/mod_list.ini");
        if (f.is_open()) {
            std::string line;
            bool dup = false;
            while (std::getline(f, line)) {
                if (extractModuleName(line) == name) { dup = true; break; }
            }
            if (dup) {
                std::cerr << u8"名称已存在" << std::endl;
                continue;
            }
        }
        nameOk = true;
    }

    while (true) {
        std::cout << u8"请输入模块绝对路径: ";
        std::getline(std::cin, path);
        if (std::cin.eof()) return;
        if (path.size() < 3 || (path[1] != ':' && !(path[0] == '\\' && path[1] == '\\'))) {
            std::cerr << u8"需要绝对路径" << std::endl;
            continue;
        }
        if (!pathExists(path)) {
            std::cerr << u8"路径不存在" << std::endl;
            continue;
        }
        break;
    }

    while (true) {
        std::cout << u8"请输入可执行文件名 (如 app.exe): ";
        std::getline(std::cin, exe);
        if (std::cin.eof()) return;
        std::string full = path + "/" + exe;
        if (!fileExists(full)) {
            std::cerr << u8"文件不存在" << std::endl;
            continue;
        }
        break;
    }

    std::vector<std::string> pos{ name, path, exe };
    std::vector<std::string> opts;
    cmdReg(pos, opts);
}

void cmdReg(const std::vector<std::string>& args, const std::vector<std::string>& opts) {
    if (args.size() < 3) {
        std::cerr << u8"需要三个参数: 模块名 路径 可执行文件" << std::endl;
        return;
    }
    std::string name = args[0], path = args[1], exe = args[2];
    bool noCopy = std::find(opts.begin(), opts.end(), "-n") != opts.end();



    if (name.find_first_of("\\/:*?\"<>|") != std::string::npos ) {
        std::cerr << u8"模块名非法" << std::endl;
        logEvent('r', u8"模块名非法");
        return;
    }
    if (!pathExists(path)) {
        std::cerr << u8"路径无效" << std::endl;
        logEvent('r', u8"路径无效");
        return;
    }
    std::string fullExe = path + "/" + exe;
    if (!fileExists(fullExe)) {
        std::cerr << u8"可执行文件不存在" << std::endl;
        logEvent('r', u8"文件不存在");
        return;
    }

    if (noCopy) {
        if (insertModuleToList(name, path, exe, false))
            std::cout << u8"注册成功" << std::endl;
        else
            std::cerr << u8"列表更新失败" << std::endl;
        return;
    }

    std::string target = "./mods/" + name;
    if (getCurrentExeDir() == path) {
        std::cout << u8"复制失败！" << std::endl;
        Sleep(100);
        std::cout << u8"复制失败，但程序告诉你一个秘密：\n\n你永远无法复刻一个正在思考的自己。\n\n";
        return;
    }

    if (!copyDir(path, target)) {
        std::cerr << u8"复制失败" << std::endl;
        deleteDir(target);
        logEvent('r', u8"复制失败");
        return;
    }

    if (insertModuleToList(name, path, exe, true)) {
        std::cout << u8"安装成功" << std::endl;
        logEvent('r', u8"安装成功: " + name);
    }
    else {
        std::cerr << u8"列表更新失败，清理已复制文件" << std::endl;
        deleteDir(target);
    }
}

bool deleteModule(const std::string& identifier) {
    try {
        // 1. 拒绝空标识
        if (identifier.empty()) {
            std::cerr << u8"模块标识不能为空" << std::endl;
            return false;
        }

        std::string modName;
        bool isNum = !identifier.empty() && std::all_of(identifier.begin(), identifier.end(), ::isdigit);

        if (isNum) {
            int targetId = 0;
            try {
                targetId = std::stoi(identifier);
            }
            catch (const std::exception& e) {
                std::cerr << u8"无效的数字ID: " << e.what() << std::endl;
                logEvent('e', std::string(u8"stoi失败: ") + e.what());
                return false;
            }

            std::ifstream f("./.ass/mod_list.ini");
            if (!f.is_open()) {
                std::cerr << u8"无法打开列表文件" << std::endl;
                logEvent('e', u8"无法打开 mod_list.ini");
                return false;
            }

            std::string line;
            while (std::getline(f, line)) {
                if (line.empty() || line == "end") continue;
                size_t dash = line.find('-');
                if (dash != std::string::npos) {
                    try {
                        int id = std::stoi(line.substr(0, dash));
                        if (id == targetId) {
                            modName = extractModuleName(line);
                            break;
                        }
                    }
                    catch (const std::exception& e) {
                        // 忽略解析错误，继续下一行
                        logEvent('w', std::string(u8"解析行失败: ") + e.what());
                    }
                }
            }
            f.close();

            if (modName.empty()) {
                std::cerr << u8"未找到ID " << targetId << u8" 或模块名为空（文件格式可能错误）" << std::endl;
                return false;
            }
        }
        else {
            modName = identifier;
        }

        // 2. 确保模块名非空且合法（不包含路径分隔符等，防止删除根目录）
        if (modName.empty()) {
            std::cerr << u8"模块名为空，操作终止" << std::endl;
            logEvent('e', u8"模块名为空");
            return false;
        }
        // 简单检查是否包含路径分隔符，防止恶意输入
        if (modName.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            std::cerr << u8"模块名包含非法字符，操作终止" << std::endl;
            logEvent('e', u8"模块名非法: " + modName);
            return false;
        }

        // 3. 读取所有行，过滤掉目标模块
        std::ifstream in("./.ass/mod_list.ini");
        if (!in.is_open()) {
            std::cerr << u8"无法打开列表文件" << std::endl;
            logEvent('e', u8"无法打开 mod_list.ini");
            return false;
        }

        std::vector<std::string> lines;
        std::string line;
        bool found = false;
        while (std::getline(in, line)) {
            if (line.empty() || line == "end") {
                lines.push_back(line);
                continue;
            }
            std::string curr = extractModuleName(line);
            if (curr == modName) {
                found = true;
                continue; // 跳过目标行
            }
            lines.push_back(line);
        }
        in.close();

        if (!found) {
            std::cerr << u8"未找到模块 " << modName << std::endl;
            return false;
        }

        // 4. 重新编号
        int newId = 1;
        for (auto& l : lines) {
            if (l.empty() || l == "end") continue;
            size_t dash = l.find('-');
            if (dash != std::string::npos) {
                l = std::to_string(newId++) + l.substr(dash);
            }
        }

        // 5. 写入更新后的列表
        std::ofstream out("./.ass/mod_list.ini", std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << u8"无法写入列表文件" << std::endl;
            logEvent('e', u8"无法写入 mod_list.ini");
            return false;
        }
        for (const auto& l : lines) {
            out << l << std::endl;
            if (!out) {
                std::cerr << u8"写入列表文件失败" << std::endl;
                logEvent('e', u8"写入 mod_list.ini 失败");
                return false;
            }
        }
        out.close();

        // 6. 删除对应的模块文件夹（如果存在）
        std::string modDir = "./mods/" + modName;
        if (pathExists(modDir)) {
            if (!deleteDir(modDir)) {
                std::cerr << u8"警告：删除文件夹失败，请手动删除: " << modDir << std::endl;
                logEvent('w', u8"删除文件夹失败: " + modDir);
                // 不返回 false，因为列表已更新，只是清理失败
            }
            else {
                logEvent('d', u8"删除文件夹成功: " + modDir);
            }
        }
        return true;

    }
    catch (const std::exception& e) {
        std::cerr << u8"删除模块时发生异常: " << e.what() << std::endl;
        logEvent('e', std::string(u8"deleteModule异常: ") + e.what());
        return false;
    }
    catch (...) {
        std::cerr << u8"删除模块时发生未知异常" << std::endl;
        logEvent('e', u8"deleteModule未知异常");
        return false;
    }
}

bool launchModule(const std::string& identifier) {
    bool isNum = !identifier.empty() && std::all_of(identifier.begin(), identifier.end(), ::isdigit);
    std::ifstream f("./.ass/mod_list.ini");
    if (!f.is_open()) {
        logEvent('e', u8"无法打开模块列表");
        return false;
    }

    std::vector<std::string> entries;
    std::string line;
    while (std::getline(f, line)) {
        if (line == "end") break;
        if (!line.empty()) entries.push_back(line);
    }
    f.close();

    std::string modulePath;
    if (isNum) {
        int idx = std::stoi(identifier) - 1;
        if (idx < 0 || idx >= entries.size()) {
            std::cerr << u8"无效编号" << std::endl;
            return false;
        }
        size_t q1 = entries[idx].find('\"'), q2 = entries[idx].rfind('\"');
        if (q1 == std::string::npos || q2 == std::string::npos) return false;
        modulePath = entries[idx].substr(q1 + 1, q2 - q1 - 1);
    }
    else {
        for (const auto& e : entries) {
            std::string name = extractModuleName(e);
            if (name == identifier) {
                size_t q1 = e.find('\"'), q2 = e.rfind('\"');
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    modulePath = e.substr(q1 + 1, q2 - q1 - 1);
                    break;
                }
            }
        }
        if (modulePath.empty()) {
            std::cerr << u8"未找到模块" << std::endl;
            return false;
        }
    }

    std::string cmd = "start \"\" \"" + modulePath + "\"";
    int ret = system(cmd.c_str());
    if (ret != 0) {
        logEvent('e', u8"启动失败，返回码: " + intToStr(ret));
        return false;
    }
    std::cout << u8"启动成功" << std::endl;
    return true;
}

// ==================== 日志和辅助 ====================
void logEvent(char type, const std::string& msg) {
    SYSTEMTIME st;
    GetSystemTime(&st);
    std::string timeStr = formatSystemTime(st);
    std::ofstream log("log.txt", std::ios::app);
    if (!log.is_open()) {
        std::cerr << u8"无法打开日志文件" << std::endl;
        return;
    }
    const char* typeStr = "";
    switch (type) {
    case 's': typeStr = u8"[启动模组] "; break;
    case 'i': typeStr = u8"[读取模组列表] "; break;
    case 'h': typeStr = u8"[读取帮助列表] "; break;
    case 'j': typeStr = u8"[逻辑判断] "; break;
    case 'a': typeStr = u8"[权限检查] "; break;
    case 'e': typeStr = u8"[错误] "; break;
    case 'p': typeStr = u8"[程序启动] "; break;
    case 'r': typeStr = u8"[安装模组] "; break;
    case 'd': typeStr = u8"[删除模组] "; break;
    default: typeStr = u8"[未知操作] "; break;
    }
    log << "[" << timeStr << "] " << typeStr << msg << std::endl;
}

std::string readFileContent(const std::string& filename, const std::string& startLine, const std::string& endLine) {
    std::ifstream f("./.ass/" + filename);
    if (!f.is_open()) {
        logEvent('e', u8"无法打开文件: " + filename);
        return "";
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line == startLine) continue;
        if (line == endLine) break;
        std::cout << line << std::endl;
    }
    return "";
}

std::string readLanguage(const std::string& key) {
    std::ifstream f("./languages/zh-ch.txt");
    if (!f.is_open()) {
        logEvent('e', u8"无法打开语言文件");
        return "";
    }
    std::string word, value;
    while (f >> word) {
        if (word == key) {
            f >> value;
            if (value != "end") return value;
            break;
        }
    }
    return "";
}

std::string formatSystemTime(const SYSTEMTIME& st) {
    FILETIME ft, lft;
    SystemTimeToFileTime(&st, &ft);
    FileTimeToLocalFileTime(&ft, &lft);
    SYSTEMTIME lst;
    FileTimeToSystemTime(&lft, &lst);
    std::tm tm = { 0 };
    tm.tm_year = lst.wYear - 1900;
    tm.tm_mon = lst.wMonth - 1;
    tm.tm_mday = lst.wDay;
    tm.tm_hour = lst.wHour;
    tm.tm_min = lst.wMinute;
    tm.tm_sec = lst.wSecond;
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string intToStr(int val) {
    return std::to_string(val);
}

std::string extractModuleName(const std::string& line) {
    size_t start = line.find('[');
    size_t end = line.find(']');
    if (start != std::string::npos && end != std::string::npos) {
        return line.substr(start + 1, end - start - 1);
    }
    return "";
}

bool errHandler(char errType, const std::string& modName) {
    if (errType == 'r') {
        std::cout << u8"清除错误安装数据" << std::endl;
        bool ok = deleteModule(modName);
        logEvent('r', u8"清除错误安装数据");
        return ok;
    }
    std::cerr << u8"未知错误类型: " << errType << std::endl;
    return false;
}