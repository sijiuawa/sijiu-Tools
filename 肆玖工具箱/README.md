# 肆玖工具箱
# 文档：`func.cpp`

概述
- 文件位置：`func.cpp`
- 功能：提供模块（模组）安装/注册/删除、模块启动、插件（DLL）加载与命令分发、日志与简单计划任务（延迟启动）等功能。
- 依赖：C++17 的 `std::filesystem`（以别名 `fs` 使用）、Windows API（`CreateDirectoryW`, `GetModuleFileNameA`, `LoadLibraryA`, `GetProcAddress`, `FreeLibrary` 等），线程/条件变量/互斥量用于计划任务。

主要类型与函数（摘要）

1. 类 `errcla`
- 目的：对文件/路径存在性做简易检查，屏蔽 `std::filesystem` 的异常并打印错误信息。
- 方法：
  - `bool pathExists(const std::string& path)`  
    返回 `fs::exists(path)`，用于判断路径或文件是否存在。
  - `bool isValidPath(const std::string& path)`  
    尝试调用 `fs::status(path)`，若异常则打印错误并返回 `false`。用于更严格的路径可访问性检查。
  - `bool fileExists(const std::string& filePath)`  
    检查路径存在且为常规文件（`fs::is_regular_file`），捕获异常并返回 `false`。

2. 类 `outfile`
- 目的：处理模组列表写入、文件夹创建与字符编码转换等安装相关的小工具。
- 成员：
  - 私有：`std::string nametemp`（临时保存目标 ini 路径）
- 方法：
  - `bool hasSpace(const std::string& str)` — 检查字符串是否含空格。
  - `bool insertModule(const std::string& moduleName, const std::string& modulePath, std::string& exe)`  
    将新的模组条目插入 `./.ass/mod_list.ini`：  
    步骤概述：读取整个文件到 `lines` → 找到 `end` 标记位置 → 计算最大的序号并生成新序号 → 将 `exe` 转成绝对路径以验证存在 → 构造 `newEntry` 并插入到 `end` 前 → 覆写文件。  
    返回：成功 `true`，失败 `false`（并在失败时输出错误信息与写日志）。
    注意：该函数会修改传入的 `exe` 参数为文件系统拼接后的路径字符串（副作用）。
  - `bool CreateFolderRelative(const wchar_t* relativePath)`  
    使用 `CreateDirectoryW` 创建文件夹；若已存在视为成功。
  - `std::wstring ManualConvert(const char* utf8Str)`  
    将 UTF-8 C 字符串转换为 `std::wstring`（使用 `MultiByteToWideChar`）。

3. `DLLManager`（文件内实现）
- 全局实例：`DLLManager dll_manager;`
- 目的：在运行时加载 `./dlls` 下的插件 DLL，读取导出插件名与命令接口，管理卸载与命令分发。
- 关键成员（在头文件声明）与行为：
  - 构造：保证 `./dlls` 存在并调用 `load_all_dlls`。
  - 析构：调用 `unload_all`。
  - `void load_all_dlls(const std::string& directory)`  
    枚举目录下 `.dll` 文件并调用 `load_dll`。错误会写入日志。
  - `bool load_dll(const std::string& dll_path)`  
    使用 `LoadLibraryA` 加载 DLL，要求导出两个函数（约定）：
      - `get_plugin_name()` 返回 `const char*` 插件名
      - `get_plugin_interface()` 返回 `PluginCommand*`（以 null 结束的命令数组）  
    成功时收集 `PluginCommand`（结构含 `command`, `description`, `function`）并保存到 `plugins` 映射。
    返回加载成功/失败。
  - `void reload_dlls()`、`bool unload_dll(const std::string&)`、`void unload_all()`：分别重新加载、单个卸载（调用 `FreeLibrary` 并从映射移除）、全部卸载。
  - `bool execute_command(const std::string& command)`  
    在已加载插件的命令集中查找并执行匹配命令（通过函数指针），找到并执行则返回 `true`。
  - `const std::map<std::string, PluginInfo>& get_plugins() const`、`get_command_help()`：用于列出插件和其命令帮助文本。

4. 日志与时间格式化
- `std::string FormatSystemTimeToString(SYSTEMTIME systemTime)`  
  将 `SYSTEMTIME` 转为本地时间并格式化为 `YYYY-MM-DD HH:MM:SS`。
- `void log_er(char log)` 与 `void log_er(char log, const std::string& message)`  
  将日志追加写入 `log.txt`。按 `log` 字符选择标签（例如 `'s'` → `[启动模组]`、`'e'` → `[错误]` 等）。函数在无法打开日志文件时会打印到标准错误。

5. 命令解析与分发
- `std::stack<std::string> decomposeco(std::string comm)`  
  将命令字符串按空白拆分为 token，逆序压入栈（便于按 LIFO 弹出参数）。
- `short cm_judge(std::string cm)`  
  主命令调度器，支持的内建命令（数组 `injudge`）：
  - `inm`, `help`, `Adm`, `start`, `reg`, `del`, `redll`, `deldll`, `listdll`, `exit`, `tasks`, `canceltask`  
  根据命令调用对应函数，或在未匹配内建命令时尝试 `dll_manager.execute_command(cm)`；若仍未匹配则记录错误并返回。
  返回值说明：`1` 表示 `exit` 命令请求退出；其它情况一般返回 `0`。

6. 与用户交互与文件读取
- `void inm()` / `void inm(std::stack<std::string>)`  
  打印 `mod_list.ini` 中 `startline` 和 `end` 之间的条目。
- `void help()` / `void help(std::stack<std::string>)`  
  显示帮助（读取 `./.ass/config.ini` 的帮助段）并追加已加载 DLL 插件的命令帮助。
- `bool Adm()`  
  读取 `./.ass/config.ini` 中 `Administrator=` 字段判断是否为管理员权限（`"1"` 为是）。
- `std::string in(...)` （重载，三种签名）  
  简单的键-值或区段读取辅助函数：一是从 `./languages/zh-ch.txt` 中按键取值；二是按键从 `./.ass/<filename>` 取值；三是按行输出 `./.ass/<filename>` 指定区段（startline..endline）。

7. 模组安装 / 注册 / 启动 / 删除
- `void reg()`（交互式）与 `void reg(std::stack<std::string>, std::stack<std::string>)`（参数化）  
  支持交互或命令行参数方式注册模组（需要管理员权限）。参数形式示例：`name=XXX path=C:\path\ exe=run.exe` 或通过交互输入。可选参数 `-n` 表示只更新列表不复制文件。流程：
  - 验证模块名合法性（禁止 `\ / : * ? " < > |`）
  - 检查是否重复
  - 验证路径与可执行文件存在（使用 `errcla`）
  - 若非 `-n`，将模组目录复制到 `./mods/<name>`（使用 `fs::copy`），并调用 `outfile::insertModule` 更新 `mod_list.ini`。异常时回滚并写日志。
- `bool launchModule(const std::string& moduleIdentifier)`  
  支持按编号或按模块名启动：从 `./.ass/mod_list.ini` 读取条目，解析出程序路径（引号中的内容），再使用系统命令启动（Windows 上构造 `start "" "<path>"`）。返回启动成功/失败并写日志。
- `bool deleteModule(const std::string& moduleIdentifier)`  
  支持按编号或按名称删除模组：先定位模组名 → 从 `./.ass/mod_list.ini` 中移去对应行并重写序号 → 删除 `./mods/<modName>` 文件夹（`fs::remove_all`）。错误或不存在会写日志并返回 `false`。

8. 错误清理与辅助
- `bool errj(const char err, const std::string modname)`  
  错误处理分发，当前仅实现 `'r'`（安装失败时尝试删除错误安装数据）。
- `std::string join(const std::vector<std::string>& vec, const std::string& delimiter)`  
  将字符串向量按分隔符连接。
- `std::string extractModuleName(const std::string& line)`  
  从模组列表的表示行中提取方括号内的模组名（`[...]`）。

9. 计划任务（延迟启动）
- 类型：`planTask`（定义在文件中）
  - 成员：`std::vector<Task> tasks`、`std::mutex mtx`、`std::condition_variable cv`、后台 `std::thread worker`、`int nextId`、`bool stop` 等。
  - 构造：启动 `workerLoop` 线程。
  - 析构：设 `stop=true`、通知并 `join()` 线程。
  - `void AddTask(int delaySeconds, const std::string& module)`：添加延迟任务（任务结构含 id、执行时间、模块名、cancelled、done）。
  - `bool CancelTask(int id)`：按 id 标记取消。
  - `void ListTasks() const`：列出现有未取消任务与剩余时间。
  - `workerLoop()`：每秒唤醒检查到期任务并调用 `launchModule` 执行，之后清理已完成/取消任务。
- 全局实例：`planTask taskScheduler;`

10. 其它
- `std::string getCurrentExePath()`  
  使用 `GetModuleFileNameA` 获取当前可执行文件路径并返回父目录字符串。

使用示例（命令行）
- 注册模组（参数方式）：
  reg name=MyMod path=C:\MyMod\ exe=run.exe
  可选：在参数中加入 `-n` 表示只更新列表不复制文件。
- 交互式注册：在程序输入 `reg` 即进入交互流程。
- 启动模组：
  start 2              // 按列表编号启动（编号从 1 开始）
  start MyMod          // 按名称启动
  start -t time=10 MyMod    // 延迟 10 秒启动
  start -t -m time=1 MyMod  // 延迟 1 分钟启动（`-m` 表示分钟）
  start -pl MyMod1 MyMod2   // 同时启动多个模块（立即或配合 -t 加入计划）
- 列出模组：inm
- 删除模组（交互式）：del（随后输入名称或编号）
- 列出已加载 DLL 插件及其命令：listdll；查看帮助 help 会自动追加 DLL 命令帮助。

注意事项与已知限制
- 本实现对路径/文件存在判断会捕获 `std::filesystem` 异常并返回失败，但不会对所有边界条件做细粒度处理。
- `outfile::insertModule` 会修改传入的 `exe` 参数为绝对路径（注意副作用）。
- DLL 插件约定导出 `get_plugin_name` 与 `get_plugin_interface`；`PluginCommand` 数组以 `command == nullptr` 结束。
- `DLLManager`、大多数 I/O 操作与命令分发未做跨线程同步（除 `planTask`），若从多个线程同时操作插件、模组相关文件，可能存在竞态。
- 启动模块依赖系统命令（Windows 使用 `start`）；路径中需正确引用引号以避免空格问题。
- 日志写入 `log.txt`，若日志文件无法打开将输出错误到标准错误但不会中断主流程。

建议（可选改进）
- 将对 `mod_list.ini` 的并发访问进行锁保护或使用原子/临时文件替换策略，以降低并发修改风险。
- `outfile::insertModule` 不应直接修改 `exe` 入参；改为返回新的路径或使用输出参数明确表示。
- 对 `LoadLibrary` / `GetProcAddress` 的字符串匹配与 ABI 约定可增加版本与符号校验，避免插件崩溃传播到宿主程序。
- 改用更健壮的命令行解析库（如 `getopt`/`boost::program_options` 或自定义解析）能减少解析错误与复杂性。
- 增加单元测试覆盖注册/删除/加载流程（目前为手工交互驱动）。

结束
- 本文档为 `func.cpp` 中选中代码的概要说明与使用提示，便于理解主要流程、接口与副作用。如需将文档以内联注释形式插入到源文件或生成 API 文档（Doxygen / XML 注释），可继续指定目标格式与位置。