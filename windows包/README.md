# RecvtConsole — Qt 6 / Revit 2025

本地运行包位于 `windows包`，运行其中的 `RecvtConsole.exe`。升级时同时更新下列三个 Python 文件。

## v24 构件浏览

- 每次建模成功或点击“重新导出 / 刷新预览”后，导出当前项目的完整快照（IFC、分构件 OBJ 和 `elements.json`）。连续建模不再只显示最后新增的部分，修改已有构件也会进入快照。
- 按“楼层 → 类别 → 构件”浏览和搜索；单击模型选中最近的可见构件，高亮并同步构件树和属性面板。点击空白处取消选择；左键拖动仍用于旋转。
- 详情包含构件 ID、UniqueId、名称、类别、族、类型、楼层、材质、实例参数和类型参数。参数为只读快照，使用 Revit 项目的显示单位。
- 勾选树节点控制可见性；“隐藏”“隔离”“定位”支持单个构件、整个类别或楼层。“全部显示”恢复可见性；搜索只过滤构件树，不隐藏模型。
- 材质保留导出面上的颜色和透明度；未能读取的材质使用默认色。不包含 Revit 的完整贴图、渲染外观和全部视图效果。
- 导出范围为当前文档可获取的模型几何；二维注释以及链接文件内部构件的独立浏览不在本版范围内。没有明确标高的构件归入“未关联楼层”。
- 空项目会清空旧预览。几何导出部分失败时提示不完整；网格和属性不匹配时拒绝加载。属性读取失败的构件仍尽量保留几何，并在详情中提示。
- 兼容旧版不分组 OBJ，但无法从旧文件恢复构件身份。必须更新监听器并重新导出，才能使用完整构件详情。

隐藏、隔离和定位仅改变前端预览，不修改 Revit 模型。当前为任务结束后的全量快照，并非实时双向同步；大型项目的导出和加载耗时会随模型规模增加。

## 更新 Revit 监听器

1. 从运行包取出 `script.py`、`recvt_bridge.py`、`recvt_model.py`，一起覆盖到：
   `D:\pyMyTools.extension\MyTools.tab\AI_Panel.panel\AI_Listener.pushbutton`
2. 重启 Revit，打开建筑项目，点击按钮，看到 `Recvt Listener v24 / Qt 6 started`。
3. 默认通信目录为 `D:\revit_ai_script`。若需修改，同时修改 script.py 的 `SCRIPTS_DIR` 和 Qt 的“脚本目录”。
4. 先点 Qt 的“重新导出 / 刷新预览”，再测试聊天和建模。不要运行之前的 Qt 6 测试程序。



本工程使用 Qt 6。界面用 Qt Widgets，三维预览用 Qt3D。
普通聊天不会执行代码；明确建模请求由 AI 返回脚本，Revit 监听器执行，导出 IFC 和 OBJ。
Qt 显示分构件 OBJ 网格，并从 `elements.json` 读取属性与材质，IFC 用于交付；不直接读取任意 IFC，不自动保存 RVT。

## 编译

本机已验证 Qt 6.11.1 MSVC2022 64bit：`D:/qt6/6.11.1/msvc2022_64`，包含 Qt3D 模块。
在 Qt Creator 中选择对应 Qt 6 套件打开 CMakeLists.txt 即可。
在 CLion 中使用与 Qt 套件匹配的 MSVC 工具链，不要使用 MinGW 编译 MSVC 版 Qt。

命令行示例（相应的 MSVC 开发者终端，修改成你的 Qt 路径）：

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="D:/qt6/6.11.1/msvc2022_64"
cmake --build build --config Release
```

复制 exe 时，用相同 Qt 套件的 `windeployqt --release <exe路径>` 部署 Qt DLL/插件。
`RecvtConsole-Qt6-windows.zip` 已部署 Qt DLL、插件和 MSVC 运行库，解压整个目录运行即可。
HTTPS 使用 Qt 6 TLS 插件；运行包包含 Windows Schannel 后端，不依赖旧 Qt5 的 OpenSSL 1.1 DLL。

本机已用 Qt 6.11.1 MSVC2022 x64 编译并通过自动检查。在 CLion 中选择 Visual Studio / MSVC 工具链，并重新加载 CMake；旧 Qt5 缓存应重置或更换构建目录。

Qt Creator 若在编译器检测阶段失败，且日志中 JOM 的中文构建路径变成乱码：改用 Ninja 生成器，并把构建目录设为 `%LOCALAPPDATA%/RecvtConsole/build/msvc-release`（调试用 msvc-debug）。已有 CMakePresets.json 提供对应配置。双击或在终端运行 build_msvc.cmd 可自动加载 MSVC 环境并完成 Release 配置和构建；项目源码仍保留原位置。

CLion 自带 MinGW 工具链也已验证：CMake 选项改为 `-DCMAKE_PREFIX_PATH=D:/qt6/6.11.1/mingw_64`，不能混用 msvc2022_64。修改后执行 Reload CMake，先成功生成 build.ninja 再构建。Windows 构建完成后默认自动部署 Qt DLL、插件及匹配编译器的 MinGW 运行库，方便直接启动；可用 RECVT_DEPLOY_RUNTIME=OFF 禁用。MinGW moc 的临时输出放到系统临时目录，以兼容中文项目路径。


如果 Qt 与 Revit 在不同电脑，两端必须访问同一个共享通信目录。返回的模型路径为相对路径。

## 三维预览操作

- 左键按住拖动：绕模型旋转。
- 左键单击：选中构件并查看详情；空白处取消选择。
- 右键或中键按住拖动：平移模型视角。
- 滚轮：缩放。
- 双击：恢复加载时的完整模型视角。

鼠标操作直接由嵌入的三维窗口处理，不再依赖 Qt3D 的默认轨道输入控制器。

## Revit 警告与确认弹框

自动处理仅在本程序的一次建模/导出任务期间启用，结束后取消事件订阅。

- 常规建模 Warning 自动关闭，内容仍写入 result.json 的 warnings，并显示在 Qt 中。
- 无法提交的建模 Error 自动回滚，错误反馈给 AI；不会选择删除构件之类的修复动作。
- 已配置的确认弹框自动选择指定按钮。默认配置包含 Autodesk 文档中的临时隐藏/隔离导出确认，选择“保留模式并导出”。
- 其他确认框记录到通信目录的 `dialogs.log`。截图没有展示具体弹框，因此不能确定其他弹框应该选择哪个按钮。

如需增加自动确认规则，在通信目录创建 `dialog_rules.json`，键为日志中的精确 DialogId，值为该弹框对应的按钮结果编号。
例如默认导出规则是：

```json
{
  "TaskDialog_Really_Print_Or_Export_Temp_View_Modes": 1002
}
```

不同弹框的编号含义不同，不能把所有弹框都设成 1。未知弹框仍需手动处理；可以把截图与对应日志提供给维护者配置。
参考：[Autodesk DialogBoxShowing 示例](https://help.autodesk.com/cloudhelp/2026/ENU/Revit-API-MainReference/files/html/cb46ea4c-2b80-0ec2-063f-dda6f662948a.htm)、[FailuresProcessing](https://help.autodesk.com/cloudhelp/2026/ENU/Revit-API-MainReference/files/html/446796ca-d8f5-0f8a-7b82-a4ec6e5aa7a0.htm)。

## 输出与排错

```text
D:\revit_ai_script\
  ai_gen.py
  result.json
  revit_context.json
  ai_error_memory_v3.json
  dialogs.log
  dialog_rules.json            可选，自定义确认规则
  tasks\<任务ID>.json          执行记录，避免重复建模
  output\<任务ID>\model.ifc
  output\<任务ID>\model.obj
  output\<任务ID>\elements.json  构件身份、类型参数和材质
```

IFC/OBJ 导出失败不会重做已经成功的模型。使用“重新导出 / 刷新预览”重试导出。
新建会话不会删除 Revit 模型；停止按钮只取消 AI 请求，不能中断 Revit 的建模操作。
不要删除任务记录后重放旧任务，否则可能重复创建建筑。

保留问题发生时的 Qt 错误内容、pyRevit 输出、result.json；涉及确认框时再提供 dialogs.log。
项目快照中的 64 位元素 ID 已转换为字符串，修复 IronPython JSON 的 `355L is not JSON serializable` 问题。

## 自动检查

```powershell
python -m unittest discover -s . -p "test_*.py" -v
cmake -S . -B build-tests -DRECVT_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH="你的Qt6套件路径"
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

测试覆盖聊天/执行路由、文件任务协议、鼠标旋转平移缩放、失败回滚、确认规则匹配和事件注销。
Python 回归测试使用模拟 Revit API；真实 Revit 的建模/导出与各类弹框仍需目标电脑联调。

v24 新增 `test_model_export.py` 与 `recvt_model_browser` 测试，覆盖分组/材质/镜像实例导出、64 位 ID、全项目快照、空模型、最近构件拾取、选择高亮、属性联动、楼层隐藏/隔离、搜索和旧版兼容。已进行离线示例的实际三维渲染截图检查；这些检查不能替代真实 Revit 2025 联调。

可离线查看本程序导出的快照（同目录需有配套 `elements.json`）：

```powershell
.\RecvtConsole.exe --preview-model "D:\revit_ai_script\output\任务ID\model.obj"
```

诊断时可加 `--preview-element 构件ID` 预选构件，或 `--screenshot 截图路径.png` 生成含三维画面的界面截图后退出。该方式只读取快照，不向 Revit 发送任务。

## v24 预览数据协议

`result.json` 中 `export_scope=project`，`mesh_path` 和 `elements_path` 均为相对于通信目录的路径；`created_element_count` 仅统计本次新增数量，不再决定导出范围。

OBJ 使用 `o element_<十进制ID>` 标记构件，`usemtl material_<ID>` 标记面材质；顶点仍为 Revit 内部英尺、Z 轴向上。`elements.json` 的 `schema_version=1`，包含 `document`、`scope`、`length_unit`、`up_axis`、`elements` 数组和按 ID 索引的 `types`、`materials`。所有构件/类型 ID 都是字符串；ID 只在所属文档内有意义。

桌面端的 `model_scene.*` 负责数据校验、分组与射线拾取，`model_browser.*` 负责构件树、属性与三维显示；`preview_controller.*` 区分单击和拖动。两端协议需要一起更新。

可选无 AI 测试：在空建筑项目中，把 sample_two_storey.py 复制成通信目录的 ai_gen.py，等待结果后点击 Qt 的刷新预览。
该示例创建 10×8 米、两层各3米的建筑（8面墙、3块楼板），固定任务编号只执行一次。


v20：增加 building.element_name、roof、wall_at、opening_at，避免类型名称绑定、屋顶参数、LocationCurve.Point 和米/英尺混用错误。门窗优先使用本次创建的墙；找不到唯一宿主明确失败。监听器增加墙位置属性执行前检查。更新时关闭 Qt 并重启 Revit，避免旧辅助模块缓存。


v21 防错记忆：通信目录中的 ai_error_memory_v4.json 记录异常摘要、修复建议、首次/最近时间、累计次数及最近错误详情（最多 16KB），自动迁移旧 v3 清单。每次生成提供最近 12 类错误及建议，推送前拦截历史失败脚本（SHA-256，忽略空行、整行注释和行尾空白）及明确的已知错误 API 写法；监听器继续做 Python AST 墙宿主检查。最多保留 40 类错误和 128 个失败脚本，最后一次重试失败也保存。环境/网络/超时错误不会把脚本拉入失败黑名单，模型失败才记录指纹。QSaveFile 原子写入失败会提示。新建会话、重启不清除记忆，相同通信目录跨项目共享历史。需要重新验证历史失败脚本时，关闭 Qt 并备份后移走 v4 文件（旧 v3 仍在会再次迁移）。此机制不能保证 AI 永不犯错，未知错误仍依赖 Revit 事务回滚与后续修复。

v22 界面：统一卡片布局与控件样式，新增目录选择、恢复视角和 A− / A+ 字号按钮。窗口尺寸增大时，全部控件及已有对话字体同步增大（限制上下界），同时支持 Windows 高 DPI 缩放。输入框按 Enter 或 Ctrl+Enter 发送，Shift+Enter 换行，支持数字键盘回车；输入法正在组合文字时不触发发送，长按回车不重复发送。项目根目录仅保留源码、配置、说明、示例和测试文件，以及一个 RecvtConsole-Qt6-windows.zip 运行包；构建和打包中间文件放在临时目录。

v23 界面：深色品牌栏与浅色工作面板，连接配置收纳在顶部“模型与连接”按钮内。对话区增加阅读留白，输入区统一为独立卡片，示例按钮只填入文字、不自动发送。预览区增加空间参考网格，保留旋转、平移、缩放和恢复视角。字体缩放、Enter 发送和历史防错记忆继续保留。
