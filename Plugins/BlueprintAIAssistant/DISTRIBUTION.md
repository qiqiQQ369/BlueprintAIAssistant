# Blueprint AI Assistant — 地编分发与安装说明

本文件随插件一并打包，供 **关卡/内容向地编** 在目标工程内安装与使用。

## 1. 环境与版本

- **Unreal Engine**：与开发方一致，当前为 **5.6**（若版本不一致，请使用同一套引擎再编译，否则 C++ 插件会编不过或无法加载）。
- **平台**：当前工程与预编译以 **Win64 编辑器** 为主。
- **预编译包（zip 内已含 `Binaries/Win64`）**：与开发方使用 **同一套 UE 5.6（Win64 编辑器）** 时，地编一般 **不需要安装 Visual Studio、不需要本机编译**；把插件放进 `Plugins` 后启动项目即可（若编辑器仍提示重编，多为引擎或工具链不一致，需向程序侧索取匹配包或改用语义版一致的源码包自行编译）。
- **源码包（无预编译或仅含 `Source`）**：地编机需安装 **Visual Studio 2022** 并勾选 **「使用 C++ 的游戏开发」**，以便首次打开工程时编译插件。

## 2. 安装到项目

1. 将压缩包内 **`BlueprintAIAssistant` 整文件夹** 解压/复制到目标项目的：
   - `你的项目/Plugins/BlueprintAIAssistant/`
2. 若 `Plugins` 目录不存在，可手动新建 `Plugins`。
3. 在资源管理器中 **双击** 项目的 `*.uproject`；若提示需要重建模块，选 **是** 或 **是 (Yes)**。
4. **仅在使用源码包、且未自动编过插件时**：在工程目录 **右键 `.uproject` → Generate Visual Studio project files**，用 VS 打开 `*.sln`，**编译 Editor**（或命令行用团队约定的 `Build.bat` 流程）。**预编译包**在引擎版本一致时通常可跳过本步。

> **不要** 把任意机器上拷来的 `Binaries` 与 **不同引擎/不同 VS 工具链** 混用。团队内发的 **正式 with-binaries 包** 与 **UE 5.6 Win64** 一致即可；拿不准时优先要 **官方打的预编译 zip**，而不是自己从别的项目里抠 `Binaries`。

## 3. 在编辑器里启用

1. 打开编辑器后，进入 **Edit → Plugins**，搜索 `Blueprint AI Assistant`。
2. 勾选 **Enabled**（若被禁用），按提示 **重启编辑器**。

## 4. API 与项目设置

1. 菜单 **Edit → Project Settings → Plugins → Blueprint AI Assistant**（项目级插件设置）。
2. 选择提供方（OpenAI 兼容 / DeepSeek / 豆包 / Gemini 等），填写 **Endpoint、Model、API Key** 等。  
3. 可调整 **超时**、是否启用 **流式 (SSE)**（对 OpenAI 兼容且网关支持时有利于长回复）。

> **安全**：API Key 属于本机/本仓库敏感信息，**不要** 把带密钥的 `Config` 或 `Saved` 写进要分享给他人的压缩包。每位地编单独在自己工程或本机里配置。

## 5. 打开面板

- 菜单 **Window（窗口）** 中打开 **Blueprint AI Assistant**（若主菜单是英文则为 **Window** 下同名项，具体以团队工程菜单定制为准；默认注册在 **LevelEditor.MainMenu.Window**）。

## 6. 地编可关注的本地目录

- 使用日志、反馈、HTTP 脱敏导出等可能写在：
  - `你的项目/Saved/BlueprintAIAssistant/`
- 出问题时将上述目录下相关文件提供给程序/工具同学即可（注意不要附带密钥文本）。

## 7. 打包方（组内发版前）建议自检

- 在 **UE 5.6** 下对宿主工程 **Development Editor** 能满编通过。
- （可选）用仓库内脚本 `scripts/run-blueprint-ai-smoke.ps1` 做一轮自动化冒烟。
- 使用 `scripts/package-blueprint-ai-assistant.ps1` 生成 zip（默认 **不含** `Tests` 与 `Binaries`，体小且干净）。
- 需要附带本机已编 DLL 时，再加 `-IncludeBinaries`，并确认 **接收方引擎与 Win64 一致**。

---

**版本**（以 `BlueprintAIAssistant.uplugin` 内 `VersionName` 为准。）
