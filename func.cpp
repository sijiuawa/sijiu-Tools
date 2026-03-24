#include "func.h"
#include <cctype>
#include <functional>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

// ==================== 编码转换 ====================
std::wstring utf8ToWide(const std::string & utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wstr[0], len);
    return wstr;
}

std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string str(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], len, nullptr, nullptr);
    return str;
}

std::string ansiToUtf8(const char* ansiStr) {
    if (!ansiStr) return "";
    int wlen = MultiByteToWideChar(CP_ACP, 0, ansiStr, -1, nullptr, 0);
    if (wlen <= 1) return "";
    int len = MultiByteToWideChar(CP_ACP, 0, ansiStr, wlen - 1, nullptr, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansiStr, wlen - 1, &wstr[0], len);
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
    while (!wpath.empty() && (wpath.back() == L'\\' || wpath.back() == L'/'))
        wpath.pop_back();
    if (wpath.empty()) return false;

    size_t pos = 0;
    while ((pos = wpath.find_first_of(L"\\/", pos + 1)) != std::wstring::npos) {
        std::wstring sub = wpath.substr(0, pos);
        CreateDirectoryW(sub.c_str(), nullptr);
    }
    CreateDirectoryW(wpath.c_str(), nullptr);
    return true;
}

std::vector<std::string> getDllFiles(const std::string& dir) {
    std::vector<std::string> files;
    std::wstring wdir = utf8ToWide(dir);

    wchar_t absPath[MAX_PATH];
    if (GetFullPathNameW(wdir.c_str(), MAX_PATH, absPath, nullptr) == 0) {
        DWORD err = GetLastError();
        std::cerr << u8"获取绝对路径失败: " << dir << u8" 错误码 " << err << std::endl;
        return files;
    }
    wdir = absPath;
    while (!wdir.empty() && (wdir.back() == L'\\' || wdir.back() == L'/'))
        wdir.pop_back();

    std::wstring search = wdir + L"\\*.dll";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND)
            std::cerr << u8"查找DLL失败，错误码: " << err << std::endl;
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

// 统计目录下文件总数
size_t countFilesInDir(const std::string& dir) {
    std::wstring wdir = utf8ToWide(dir);
    size_t count = 0;
    std::stack<std::wstring> dirs;
    dirs.push(wdir);
    while (!dirs.empty()) {
        std::wstring cur = dirs.top();
        dirs.pop();
        std::wstring search = cur + L"\\*";
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        do {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
                continue;
            std::wstring full = cur + L"\\" + findData.cFileName;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dirs.push(full);
            }
            else {
                ++count;
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
    return count;
}

// 带进度回调的 copyDir
bool copyDir(const std::string& src, const std::string& dest, int depth, std::function<void(size_t, size_t)> progressCb) {
    const int MAX_DEPTH = 100;
    if (depth > MAX_DEPTH) {
        std::cerr << u8"递归深度过大，可能陷入循环，停止复制" << std::endl;
        return false;
    }

    wchar_t absSrc[MAX_PATH], absDest[MAX_PATH];
    if (GetFullPathNameW(utf8ToWide(src).c_str(), MAX_PATH, absSrc, nullptr) == 0 ||
        GetFullPathNameW(utf8ToWide(dest).c_str(), MAX_PATH, absDest, nullptr) == 0) {
        std::cerr << u8"获取绝对路径失败" << std::endl;
        return false;
    }

    auto trimBackslash = [](wchar_t* path) {
        size_t len = wcslen(path);
        while (len > 0 && (path[len - 1] == L'\\' || path[len - 1] == L'/'))
            path[--len] = L'\0';
        };
    trimBackslash(absSrc);
    trimBackslash(absDest);

    if (wcscmp(absSrc, absDest) == 0)
        return true;

    size_t srcLen = wcslen(absSrc);
    size_t destLen = wcslen(absDest);
    if (destLen > srcLen && wcsncmp(absDest, absSrc, srcLen) == 0 &&
        (absDest[srcLen] == L'\\' || absDest[srcLen] == L'/')) {
        std::cerr << u8"目标目录是源目录的子目录，无法复制（防止无限递归）" << std::endl;
        return false;
    }

    std::wstring wsrc = absSrc;
    std::wstring wdest = absDest;

    if (!CreateDirectoryW(wdest.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        std::cerr << u8"创建目标目录失败: " << dest << std::endl;
        return false;
    }

    std::wstring search = wsrc + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    bool success = true;
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;

        std::wstring srcPath = wsrc + L"\\" + findData.cFileName;
        std::wstring destPath = wdest + L"\\" + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wchar_t absSrcSub[MAX_PATH], absDestSub[MAX_PATH];
            if (GetFullPathNameW(srcPath.c_str(), MAX_PATH, absSrcSub, nullptr) == 0 ||
                GetFullPathNameW(destPath.c_str(), MAX_PATH, absDestSub, nullptr) == 0) {
                success = false;
                break;
            }
            trimBackslash(absSrcSub);
            trimBackslash(absDestSub);

            size_t srcSubLen = wcslen(absSrcSub);
            size_t destSubLen = wcslen(absDestSub);
            if (destSubLen > srcSubLen && wcsncmp(absDestSub, absSrcSub, srcSubLen) == 0 &&
                (absDestSub[srcSubLen] == L'\\' || absDestSub[srcSubLen] == L'/')) {
                continue;
            }

            if (!copyDir(wideToUtf8(srcPath), wideToUtf8(destPath), depth + 1, progressCb)) {
                success = false;
                break;
            }
        }
        else {
            if (!CopyFileW(srcPath.c_str(), destPath.c_str(), FALSE)) {
                success = false;
                break;
            }
            if (progressCb) progressCb(1, 0); // 回调通知完成一个文件
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
    return success;
}

// 带进度回调的 deleteDir
bool deleteDir(const std::string& path, std::function<void(size_t, size_t)> progressCb) {
    if (!pathExists(path)) return true;

    // 递归删除每个文件以便进度回调
    std::function<bool(const std::wstring&)> removeRecursive;
    removeRecursive = [&](const std::wstring& cur) -> bool {
        std::wstring search = cur + L"\\*";
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(search.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) return true;
        bool ok = true;
        do {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
                continue;
            std::wstring full = cur + L"\\" + findData.cFileName;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!removeRecursive(full)) {
                    ok = false;
                    break;
                }
            }
            else {
                if (!DeleteFileW(full.c_str())) {
                    ok = false;
                    break;
                }
                if (progressCb) progressCb(1, 0);
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
        if (ok && !RemoveDirectoryW(cur.c_str())) {
            ok = false;
        }
        return ok;
        };

    bool result = removeRecursive(utf8ToWide(path));
    if (result && progressCb) progressCb(0, 0); // 完成回调
    return result;
}

std::string getCurrentExeDir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return wideToUtf8(buffer);
}

void ensureDirs() {
    createDirRecursive(ConstPath::ASS_DIR);
    createDirRecursive(ConstPath::MODS_DIR);
    createDirRecursive(ConstPath::LANG_DIR);
    createDirRecursive(ConstPath::DLL_DIR);
}

// ==================== DLLManager ====================
DLLManager g_dllManager;

DLLManager::DLLManager() {
    try {
        if (!pathExists(dllDir_)) createDirRecursive(dllDir_);
    }
    catch (const std::exception& e) {
        logEvent('e', std::string(u8"DLLManager初始化异常 ") + e.what());
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
    info.name = ansiToUtf8(name);
    info.dllPath = dllPath;

    for (PluginCommand* p = cmds; p->command != nullptr; ++p) {
        CommandEntry entry;
        entry.name = ansiToUtf8(p->command);
        entry.func = p->function;
        entry.description = ansiToUtf8(p->description);
        info.commands.push_back(std::move(entry));
    }

    std::string pluginName = info.name;
    plugins_[pluginName] = std::move(info);

    std::cout << u8"成功加载插件: " << pluginName << u8" (" << dllPath << u8")" << std::endl;
    logEvent('d', u8"加载插件: " + pluginName);
    return true;
}

void DLLManager::reload() { loadAll(dllDir_); }

bool DLLManager::unload(const std::string& pluginName) {
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        std::cerr << u8"未找到插件 " << pluginName << std::endl;
        return false;
    }
    if (FreeLibrary(it->second.handle)) {
        plugins_.erase(it);
        std::cout << u8"已卸载插件 " << pluginName << std::endl;
        logEvent('d', u8"卸载插件: " + pluginName);
        return true;
    }
    DWORD err = GetLastError();
    std::cerr << u8"卸载失败: " << pluginName << u8" 错误码 " << err << std::endl;
    logEvent('e', u8"卸载失败: " + pluginName);
    return false;
}

void DLLManager::unloadAll() {
    for (auto& [_, info] : plugins_) FreeLibrary(info.handle);
    plugins_.clear();
}

bool DLLManager::execute(const std::string& cmd) {
    for (const auto& [_, plugin] : plugins_) {
        for (const auto& entry : plugin.commands) {
            if (cmd == entry.name) {
                entry.func();
                return true;
            }
        }
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> DLLManager::commandHelp() const {
    std::vector<std::pair<std::string, std::string>> res;
    for (const auto& [_, plugin] : plugins_)
        for (const auto& entry : plugin.commands)
            res.emplace_back(entry.name, entry.description);
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
static bool insertModuleToList(const std::string& name, const std::string& path,
    const std::string& exe, bool useModsDir) {
    const std::string& listFile = ConstPath::MOD_LIST_FILE;
    std::vector<std::string> lines;
    std::string line;

    std::ifstream inFile(listFile);
    if (!inFile.is_open()) {
        std::ofstream outFile(listFile);
        if (!outFile.is_open()) {
            std::cerr << u8"无法创建模块列表文件" << std::endl;
            return false;
        }
        outFile << "end" << std::endl;
        outFile.close();
        inFile.open(listFile);
        if (!inFile.is_open()) {
            std::cerr << u8"无法打开模块列表文件" << std::endl;
            return false;
        }
    }

    while (std::getline(inFile, line)) lines.push_back(line);
    inFile.close();

    auto endIt = std::find_if(lines.begin(), lines.end(),
        [](const std::string& s) { return s == "end"; });
    if (endIt == lines.end()) {
        std::cerr << u8"模块列表缺少 end 标记" << std::endl;
        return false;
    }

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

    std::string entryPath = useModsDir ? (ConstPath::MODS_DIR + "/" + name + "/" + exe) : (path + "/" + exe);
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
    std::string token;
    bool inQuote = false;

    for (char c : cmd) {
        if (c == '"') {
            inQuote = !inQuote;
        }
        else if (c == ' ' && !inQuote) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        }
        else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

// 进度条显示函数
static void showProgressBar(size_t current, size_t total) {
    if (total == 0) return;
    int barWidth = 50;
    float progress = (float)current / total;
    int pos = (int)(barWidth * progress);
    std::cout << "\r[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << "%" << std::flush;
    if (current == total) {
        std::cout << "\n" << u8"操作完成" << std::endl;
    }
}

// 辅助函数：去除字符串首尾空白和引号
static std::string trimQuotes(const std::string& s) {
    size_t start = 0, end = s.size();
    if (s.empty()) return s;
    if (s.front() == '"') start = 1;
    if (s.back() == '"') end = s.size() - 1;
    if (start >= end) return "";
    std::string res = s.substr(start, end - start);
    // 再去掉首尾空白
    size_t l = res.find_first_not_of(" \t");
    size_t r = res.find_last_not_of(" \t");
    if (l == std::string::npos) return "";
    return res.substr(l, r - l + 1);
}

// 核心安装函数
static bool performInstall(const std::string& name, const std::string& path,
    const std::string& exe, bool noCopy) {
    if (name.empty() || path.empty() || exe.empty()) {
        std::cerr << u8"模块名、路径或可执行文件为空" << std::endl;
        return false;
    }
    if (name.find_first_of("\\/:?\"<>|*") != std::string::npos) {
        std::cerr << u8"模块名非法" << std::endl;
        logEvent('r', u8"模块名非法");
        return false;
    }
    if (!pathExists(path)) {
        std::cerr << u8"路径无效" << std::endl;
        logEvent('r', u8"路径无效");
        return false;
    }

    std::string fullExe = path + "/" + exe;
    if (!fileExists(fullExe)) {
        std::cerr << u8"可执行文件不存在" << std::endl;
        logEvent('r', u8"文件不存在");
        return false;
    }

    if (noCopy) {
        if (insertModuleToList(name, path, exe, false))
            std::cout << u8"注册成功" << std::endl;
        else
            std::cerr << u8"列表更新失败" << std::endl;
        return true;
    }

    std::string target = ConstPath::MODS_DIR + "/" + name;
    auto normalizePath = [](std::string p) -> std::string {
        while (!p.empty() && (p.back() == '\\' || p.back() == '/'))
            p.pop_back();
        return p;
        };
    std::string curDir = normalizePath(getCurrentExeDir());
    std::string normPath = normalizePath(path);

    if (curDir == normPath) {
        std::cout << u8"复制失败？" << std::endl;
        Sleep(100);
        std::cout << u8"复制失败，但程序告诉你一个秘密：\n\n你永远无法复刻一个正在思考的自己。\n\n";
        return false;
    }

    // 统计文件总数
    size_t totalFiles = countFilesInDir(path);
    size_t copied = 0;
    auto progressCb = [&copied, totalFiles](size_t inc, size_t) {
        copied += inc;
        showProgressBar(copied, totalFiles);
        if (g_debugMode) {
            std::cout << u8" [Debug] 已复制 " << copied << "/" << totalFiles << std::endl;
        }
        };

    if (!copyDir(path, target, 0, progressCb)) {
        std::cerr << u8"复制失败" << std::endl;
        deleteDir(target);
        logEvent('r', u8"复制失败");
        return false;
    }

    if (insertModuleToList(name, path, exe, true)) {
        std::cout << u8"安装成功" << std::endl;
        logEvent('r', u8"安装成功: " + name);
        return true;
    }
    else {
        std::cerr << u8"列表更新失败，清理已复制文件" << std::endl;
        deleteDir(target);
        return false;
    }
}

// 交互式补全缺失参数
static void promptMissingParams(std::string& name, std::string& path, std::string& exe) {
    if (name.empty() || name == "*") {
        while (true) {
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
            // 检查重复
            std::ifstream f(ConstPath::MOD_LIST_FILE);
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
            break;
        }
    }

    if (path.empty() || path == "*") {
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
    }

    if (exe.empty() || exe == "*") {
        while (true) {
            std::cout << u8"请输入可执行文件名(如 app.exe): ";
            std::getline(std::cin, exe);
            if (std::cin.eof()) return;
            std::string full = path + "/" + exe;
            if (!fileExists(full)) {
                std::cerr << u8"文件不存在" << std::endl;
                continue;
            }
            break;
        }
    }
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
            if (a.empty()) {
                cmdReg();  // 交互模式
                return;
            }

            // 参数解析
            std::vector<std::string> opts;
            std::string name, path, exe;
            bool hasName = false, hasPath = false, hasExe = false;

            for (const auto& arg : a) {
                if (arg == "-n") {
                    opts.push_back("-n");
                    continue;
                }
                // 键值对 name=...
                if (arg.rfind("name=", 0) == 0) {
                    std::string val = arg.substr(5);
                    name = trimQuotes(val);
                    hasName = true;
                }
                else if (arg.rfind("path=", 0) == 0) {
                    std::string val = arg.substr(5);
                    path = trimQuotes(val);
                    hasPath = true;
                }
                else if (arg.rfind("exe=", 0) == 0) {
                    std::string val = arg.substr(4);
                    exe = trimQuotes(val);
                    hasExe = true;
                }
                else {
                    // 位置参数
                    if (!hasName && name.empty()) {
                        name = trimQuotes(arg);
                        hasName = true;
                    }
                    else if (!hasPath && path.empty()) {
                        path = trimQuotes(arg);
                        hasPath = true;
                    }
                    else if (!hasExe && exe.empty()) {
                        exe = trimQuotes(arg);
                        hasExe = true;
                    }
                    else {
                        std::cerr << u8"多余参数: " << arg << std::endl;
                    }
                }
            }

            bool noCopy = std::find(opts.begin(), opts.end(), "-n") != opts.end();

            // 如果有缺失，使用 "*" 占位交互补全
            if (!hasName) name = "*";
            if (!hasPath) path = "*";
            if (!hasExe) exe = "*";

            if (g_debugMode) {
                std::cout << u8"[Debug] 解析结果: name=" << name
                    << u8", path=" << path
                    << u8", exe=" << exe
                    << u8", noCopy=" << (noCopy ? "true" : "false") << std::endl;
            }

            // 补全缺失项
            promptMissingParams(name, path, exe);

            if (name.empty() || path.empty() || exe.empty()) {
                std::cerr << u8"参数不完整，安装取消" << std::endl;
                return;
            }

            performInstall(name, path, exe, noCopy);
        }},
        {"debug", [](const auto& a) { cmdDebug(a); }},
        {"redll", [](const auto&) { g_dllManager.reload(); }},
        {"deldll", [](const auto& a) {
            bool unloadOnly = false;
            std::string identifier;
            for (const auto& arg : a) {
                if (arg == "-u") unloadOnly = true;
                else if (identifier.empty()) identifier = arg;
                else {
                    std::cerr << u8"多余参数，请只指定一个插件名或文件名" << std::endl;
                    return;
                }
            }
            if (identifier.empty()) {
                std::cout << u8"请输入要处理的插件名或DLL文件名： ";
                std::getline(std::cin, identifier);
                if (identifier.empty()) return;
            }

            std::string targetPluginName;
            std::string targetDllPath;
            const auto& plugins = g_dllManager.plugins();

            auto it = plugins.find(identifier);
            if (it != plugins.end()) {
                targetPluginName = it->first;
                targetDllPath = it->second.dllPath;
            }
            else {
                std::string lowerId = identifier;
                std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);
                std::vector<std::pair<std::string, std::string>> matches;
                for (const auto& [name, info] : plugins) {
                    std::string dllName = info.dllPath;
                    size_t pos = dllName.find_last_of("/\\");
                    if (pos != std::string::npos) dllName = dllName.substr(pos + 1);
                    size_t dot = dllName.rfind('.');
                    if (dot != std::string::npos) dllName = dllName.substr(0, dot);
                    std::transform(dllName.begin(), dllName.end(), dllName.begin(), ::tolower);
                    if (dllName == lowerId) matches.emplace_back(name, info.dllPath);
                }
                if (matches.empty()) {
                    std::cerr << u8"未找到插件 " << identifier << std::endl;
                    return;
                }
                else if (matches.size() > 1) {
                    std::cerr << u8"找到多个匹配，请使用精确的插件名：" << std::endl;
                    for (const auto& m : matches)
                        std::cerr << u8"  " << m.first << u8" (" << m.second << u8")" << std::endl;
                    return;
                }
                else {
                    targetPluginName = matches[0].first;
                    targetDllPath = matches[0].second;
                }
            }

            if (!g_dllManager.unload(targetPluginName)) {
                std::cerr << u8"卸载插件失败" << std::endl;
                return;
            }

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
            std::cout << u8"\n已加载插件：" << std::endl;
            for (const auto& [name, _] : g_dllManager.plugins())
                std::cout << u8"- " << name << std::endl;
            std::cout << std::endl;
        }},
        { "del", [](const auto& a) {
            bool fileOnly = false;
            bool unloadOnly = false;
            std::string identifier;

            for (const auto& arg : a) {
                if (arg == "-f") fileOnly = true;
                else if (arg == "-u") unloadOnly = true;
                else if (identifier.empty()) identifier = arg;
                else {
                    std::cerr << u8"多余参数，请只指定一个目标或使用 -f / -u 标志" << std::endl;
                    return;
                }
            }

            if (identifier.empty()) {
                std::cout << u8"请输入要删除的模块名/编号/插件名或文件路径： ";
                std::getline(std::cin, identifier);
                if (identifier.empty()) return;
            }

            // 仅删除文件路径
            if (fileOnly) {
                std::wstring wpath = utf8ToWide(identifier);
                if (DeleteFileW(wpath.c_str())) {
                    std::cout << u8"文件已删除" << std::endl;
                    logEvent('d', u8"删除文件: " + identifier);
                }
                else {
                    DWORD err = GetLastError();
                    std::cerr << u8"删除文件失败，错误码: " << err << std::endl;
                    logEvent('e', u8"删除文件失败: " + identifier);
                }
                return;
            }

            // 优先尝试删除模组条目（支持编号或名称）
            if (deleteModule(identifier)) {
                std::cout << u8"模组已删除" << std::endl;
                return;
            }

            // 若未能作为模组删除，则尝试作为已加载的插件来处理（与 deldll 行为一致）
            std::string targetPluginName;
            std::string targetDllPath;
            const auto& plugins = g_dllManager.plugins();

            auto it = plugins.find(identifier);
            if (it != plugins.end()) {
                targetPluginName = it->first;
                targetDllPath = it->second.dllPath;
            }
            else {
                std::string lowerId = identifier;
                std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);
                std::vector<std::pair<std::string, std::string>> matches;
                for (const auto& [name, info] : plugins) {
                    std::string dllName = info.dllPath;
                    size_t pos = dllName.find_last_of("/\\");
                    if (pos != std::string::npos) dllName = dllName.substr(pos + 1);
                    size_t dot = dllName.rfind('.');
                    if (dot != std::string::npos) dllName = dllName.substr(0, dot);
                    std::transform(dllName.begin(), dllName.end(), dllName.begin(), ::tolower);
                    if (dllName == lowerId) matches.emplace_back(name, info.dllPath);
                }
                if (matches.empty()) {
                    std::cerr << u8"既非模组也未找到插件：" << identifier << std::endl;
                    return;
                }
                else if (matches.size() > 1) {
                    std::cerr << u8"找到多个匹配插件，请使用精确插件名：" << std::endl;
                    for (const auto& m : matches)
                        std::cerr << u8"  " << m.first << u8" (" << m.second << u8")" << std::endl;
                    return;
                }
                else {
                    targetPluginName = matches[0].first;
                    targetDllPath = matches[0].second;
                }
            }

            if (!g_dllManager.unload(targetPluginName)) {
                std::cerr << u8"卸载插件失败" << std::endl;
                return;
            }

            if (!unloadOnly) {
                std::wstring wpath = utf8ToWide(targetDllPath);
                if (DeleteFileW(wpath.c_str())) {
                    std::cout << u8"DLL 文件已删除" << std::endl;
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
        } },
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
        {"exit", [](const auto&) { /* 在外部处理退出*/ }}
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
    std::cout << std::endl << u8"------------------------------------------" << std::endl;
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
    std::ifstream f(ConstPath::CONFIG_FILE);
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
            std::cout << u8"已添加" << modules.size() << u8" 个计划任务，延迟 " << delay << u8" 秒" << std::endl;
        }
        else {
            g_taskScheduler.add(delay, modules[0]);
            std::cout << u8"已添加任务，ID 可查 tasks" << std::endl;
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
    // 交互模式
    std::string name, path, exe;
    promptMissingParams(name, path, exe);
    if (name.empty() || path.empty() || exe.empty()) {
        std::cerr << u8"参数不完整，安装取消" << std::endl;
        return;
    }
    performInstall(name, path, exe, false);
}

void cmdReg(const std::vector<std::string>& args, const std::vector<std::string>& opts) {
    // 保留原有接口，但实际已不再使用，仅用于兼容旧调用
    if (args.size() < 3) {
        std::cerr << u8"需要三个参数: 模块名 路径 可执行文件" << std::endl;
        return;
    }
    bool noCopy = std::find(opts.begin(), opts.end(), "-n") != opts.end();
    performInstall(args[0], args[1], args[2], noCopy);
}

// debug 命令实现
void cmdDebug(const std::vector<std::string>& args) {
    std::ifstream cfg(ConstPath::CONFIG_FILE);
    std::string line;
    std::vector<std::string> lines;
    bool found = false;
    while (std::getline(cfg, line)) {
        lines.push_back(line);
        if (line.find("debug=") == 0) {
            found = true;
        }
    }
    cfg.close();

    if (args.empty()) {
        // 显示当前状态
        if (found) {
            for (const auto& l : lines) {
                if (l.find("debug=") == 0) {
                    std::string val = l.substr(6);
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t") + 1);
                    g_debugMode = (val == "1" || val == "on" || val == "true");
                    std::cout << u8"Debug模式当前为: " << (g_debugMode ? u8"开启" : u8"关闭") << std::endl;
                    break;
                }
            }
        }
        else {
            std::cout << u8"config.ini 中未找到 debug= 项，默认为关闭" << std::endl;
            g_debugMode = false;
        }
    }
    else if (args.size() == 1) {
        std::string param = args[0];
        if (param == "on") {
            // 开启 debug
            bool updated = false;
            for (auto& l : lines) {
                if (l.find("debug=") == 0) {
                    l = "debug=1";
                    updated = true;
                    break;
                }
            }
            if (!updated) lines.push_back("debug=1");
            std::ofstream out(ConstPath::CONFIG_FILE);
            if (out.is_open()) {
                for (const auto& l : lines) out << l << std::endl;
                out.close();
                g_debugMode = true;
                std::cout << u8"Debug模式已开启" << std::endl;
                logEvent('d', u8"Debug模式开启");
            }
            else {
                std::cerr << u8"无法写入 config.ini" << std::endl;
            }
        }
        else if (param == "off") {
            // 关闭 debug
            bool updated = false;
            for (auto& l : lines) {
                if (l.find("debug=") == 0) {
                    l = "debug=0";
                    updated = true;
                    break;
                }
            }
            if (!updated) lines.push_back("debug=0");
            std::ofstream out(ConstPath::CONFIG_FILE);
            if (out.is_open()) {
                for (const auto& l : lines) out << l << std::endl;
                out.close();
                g_debugMode = false;
                std::cout << u8"Debug模式已关闭" << std::endl;
                logEvent('d', u8"Debug模式关闭");
            }
            else {
                std::cerr << u8"无法写入 config.ini" << std::endl;
            }
        }
        else {
            std::cerr << u8"无效参数，使用 on 或 off" << std::endl;
        }
    }
    else {
        std::cerr << u8"参数过多，只接受 on 或 off" << std::endl;
    }
}

bool deleteModule(const std::string& identifier) {
    try {
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

            std::ifstream f(ConstPath::MOD_LIST_FILE);
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
                        logEvent('w', std::string(u8"解析行失败: ") + e.what());
                    }
                }
            }
            f.close();

            if (modName.empty()) {
                std::cerr << u8"未找到ID " << targetId << u8" 或模块名为空" << std::endl;
                return false;
            }
        }
        else {
            modName = identifier;
        }

        if (modName.empty()) {
            std::cerr << u8"模块名为空，操作终止" << std::endl;
            logEvent('e', u8"模块名为空");
            return false;
        }
        if (modName.find_first_of("\\/:*?\"<>|") != std::string::npos) {
            std::cerr << u8"模块名包含非法字符，操作终止" << std::endl;
            logEvent('e', u8"模块名非法 " + modName);
            return false;
        }

        std::ifstream in(ConstPath::MOD_LIST_FILE);
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
                continue;
            }
            lines.push_back(line);
        }
        in.close();

        if (!found) {
            std::cerr << u8"未找到模块" << modName << std::endl;
            return false;
        }

        int newId = 1;
        for (auto& l : lines) {
            if (l.empty() || l == "end") continue;
            size_t dash = l.find('-');
            if (dash != std::string::npos) {
                l = std::to_string(newId++) + l.substr(dash);
            }
        }

        std::ofstream out(ConstPath::MOD_LIST_FILE, std::ios::trunc);
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

        std::string modDir = ConstPath::MODS_DIR + "/" + modName;
        if (pathExists(modDir)) {
            // 统计文件总数并显示进度条
            size_t totalFiles = countFilesInDir(modDir);
            size_t deleted = 0;
            auto progressCb = [&deleted, totalFiles](size_t inc, size_t) {
                deleted += inc;
                showProgressBar(deleted, totalFiles);
                if (g_debugMode) {
                    std::cout << u8" [Debug] 已删除 " << deleted << "/" << totalFiles << std::endl;
                }
                };
            if (!deleteDir(modDir, progressCb)) {
                std::cerr << u8"警告：删除文件夹失败，请手动删除: " << modDir << std::endl;
                logEvent('w', u8"删除文件夹失败 " + modDir);
            }
            else {
                logEvent('d', u8"删除文件夹成功 " + modDir);
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
    std::ifstream f(ConstPath::MOD_LIST_FILE);
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
        if (idx < 0 || idx >= static_cast<int>(entries.size())) {
            std::cerr << u8"无效编号" << std::endl;
            return false;
        }
        size_t q1 = entries[idx].find('\"'), q2 = entries[idx].rfind('\"');
        if (q1 == std::string::npos || q2 == std::string::npos) return false;
        modulePath = entries[idx].substr(q1 + 1, q2 - q1 - 1);
    }
    else {
        for (const auto& e : entries) {
            if (extractModuleName(e) == identifier) {
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
        logEvent('e', u8"启动失败，返回码: " + std::to_string(ret));
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
    std::ofstream log(ConstPath::LOG_FILE, std::ios::app);
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
    std::ifstream f(ConstPath::ASS_DIR + filename);
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
    std::ifstream f(ConstPath::LANG_DIR + "/zh-ch.txt");
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

// 全局 debug 标志
bool g_debugMode = false;