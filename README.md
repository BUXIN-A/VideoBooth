# VideoBooth 高性能视频展台

面向学校低配教学一体机的全屏视频展台程序。没有花哨的功能，专注在**流畅**与**够用**：
摄像头实时画面、缩放/旋转/拖动、批注与擦除、拍照与相册管理、触摸手势与鼠标操作。

- 语言：C++17
- 平台：Windows 10 及以上（推荐 x64）
- 第三方依赖：**无**（仅使用系统组件：OpenGL、Media Foundation、WIC、Win32 / COM / Shell）
- 当前版本：1.0.0

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
---

## 功能特性

### 画面
- 全屏无边框窗口，画面按比例完整适配窗口（不拉伸）
- 左下角预览框：显示画面全景，蓝色虚线框表示当前屏幕可见区域，可直接拖动虚线框平移画面
- 画面元素：功能栏（底部 / 两侧可配置）、提示条（toast）、各种浮层面板

### 工具模式
| 模式 | 说明 |
|---|---|
| 选择（默认） | 拖动画面 |
| 批注 | 书写笔迹；「更多」面板可选颜色（10 色）与画笔粗细（1–30，默认 5） |
| 橡皮 | 擦除笔迹；「更多」面板可选橡皮大小（5 档）与「全部清除」 |

### 画面操作
- **旋转**：每次 90°，笔迹与位移同步旋转
- **锁定**：定格当前画面并**停止拉流**（保留设备句柄，解锁后迅速恢复），锁定期间仍可批注与拍照
- **拍照**：后台线程异步保存 JPG（质量 85%），不阻塞界面；保存后画面全屏白闪一下作为反馈
- **相册**：缩略图列表（滚轮横向滚动 / 拖动滚动），支持删除、单个另存为、全部导出、导入照片、全屏查看

### 输入
- 鼠标：左键 / 中键 / 滚轮
- 触摸：单指、双指捏合缩放、三指及以上平移（触摸与鼠标消息自动去重，一次触摸只响应一次）

---

## 操作说明

### 鼠标
| 模式 | 左键 | 中键 | 滚轮 |
|---|---|---|---|
| 选择 | 拖动画面 | 拖动画面 | 以光标为锚点缩放 |
| 批注 | 书写 | 拖动画面 | 以光标为锚点缩放 |
| 橡皮 | 擦除（显示圆形范围指示） | 拖动画面 | 以光标为锚点缩放 |

### 触摸
| 模式 | 单指 | 双指 | 三指及以上 |
|---|---|---|---|
| 选择 | 拖动画面 | 捏合缩放（锚点为两指中心） | 平移画面 |
| 批注 | 书写 | 捏合缩放 | 平移画面 |
| 橡皮 | 擦除 | 捏合缩放 | 平移画面 |

- 书写过程中加入第二指：当前笔画立即结束，不会误画
- 三指手势一旦确认，抬起一指后仍继续平移，避免中途跳变

### 快捷键
- `Esc`：退出程序（设置面板打开时为关闭面板）
- `Enter`：设置面板中保存并关闭

### 面板
- **「更多」面板**：批注/橡皮模式下出现「更多」标签，点击展开；点击面板与其标签、功能栏以外的空白区域可收起
- **相册面板**：宽度固定为窗口宽度的 60%，标题行提供「合成笔迹」选择框与「导入照片」「保存照片」按钮；点击卡片可删除 / 保存 / 全屏展示
- **设置面板**：直接在展台窗口内绘制（全屏窗口下独立窗口会被压在画面之下）；打开期间暂停采集，仅在面板需要重绘时出帧

---

## 构建

### 环境要求
- Windows 10 或更高版本
- Visual Studio（MSVC，需支持 C++17）
- CMake 3.20 或更高版本
- 无需任何第三方库

### 构建步骤

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel
```

如需显式指定生成器与架构：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

产物位于 `build/bin/`：

```
build/bin/
┝ VideoBooth.exe
┕ assets/            # 构建后自动从源码 assets/ 复制，必须与 exe 同级
```

### 运行与分发

直接运行 `build/bin/VideoBooth.exe`。分发时复制整个 `bin` 目录（`VideoBooth.exe` + `assets/`）即可。

> 注意：配置文件与日志写入 **exe 所在目录**，请放在有写权限的位置（不建议放入 `Program Files`）。

---

## 源码结构

```
src/
┝ main.cpp                  程序入口
┝ core/                     应用生命周期、配置、路径、界面状态、单实例
│  ┝ App.cpp/.h             启动流程、COM/MF 初始化、摄像头检测、主循环
│  ┝ AppState.h             界面状态机（工具模式 / 视图 / 画面来源）
│  ┝ Config.cpp/.h          配置读写、备份、字段级校验与修复
│  ┝ Paths.cpp/.h           路径与文件名规则
│  ┕ SingleInstance.cpp/.h  单实例互斥体与已有窗口激活
┝ capture/                  摄像头采集（Media Foundation SourceReader + 独立采集线程）
┝ annotation/               笔迹层（StrokeLayer）与笔迹文件读写（AnnotationFile）
┝ album/                    相册数据源（目录扫描、懒加载缩略图、删除、另存为）
┝ render/                   OpenGL 上下文（GlContext）与纹理/着色器渲染（GlRenderer）
┝ ui/                       窗口与界面
│  ┝ MainWindow.cpp/.h      主窗口：消息处理、渲染编排、输入与业务动作
│  ┝ OverlayCanvas.cpp/.h   自研 GDI 离屏画布（32 位预乘 BGRA，自上而下 DIB）
│  ┝ Toolbar / PreviewPanel / MorePanel / AlbumPanel / SettingsDialog
│  ┝ CameraSelectDialog / SplashWindow / FileDialog / TopmostScope
│  ┕ Resources.cpp/.h       assets 图标按需加载与缓存
┕ util/                     基础设施
   ┝ Image.cpp/.h           WIC 图像编解码（JPG / PNG）
   ┝ SaveQueue.cpp/.h       后台 JPG 保存队列（单工作线程）
   ┝ Json.cpp/.h            自研 JSON 解析与序列化
   ┝ Log.cpp/.h             日志（调试器输出 + 可选文件）
   ┕ Strings.h              字符串与格式化工具
```

---

## 运行时文件

| 文件 | 位置 | 说明 |
|---|---|---|
| `config.json` | exe 同级 | 主配置 |
| `config.bak.json` | exe 同级 | 配置备份（配置损坏时用于恢复） |
| `VideoBooth.log` | exe 同级 | 运行日志，默认关闭；关闭时不创建文件，仅输出到调试器 |
| `IMG_yyyyMMdd_HHmmss_xxx.jpg` | `%TEMP%\VideoBooth\Photos` | 拍照照片（xxx 为毫秒） |
| `<照片文件名>.ann.png` / `_live.ann.png` | 同上 | 每张照片 / 实时画面各自的笔迹文件 |

退出程序时会清理临时照片与其对应的笔迹文件。

### 配置文件字段

> 基础
- `appVersion`：程序版本号
- `configVersion`：配置文件版本号
- `toolbarPosition`：功能栏位置，`bottom`（底部，默认）/ `sides`（两侧）
- `tempFolder`：临时文件夹（默认 `%TEMP%\VideoBooth\Photos`）
- `saveLog`：是否写入运行日志（默认 `false`）

> 画面（`camera`）
- `defaultCamera`：默认摄像头设备符号链接（避免多摄像头下画面错误）
- `fps`：采集刷新率
- `width` / `height`：采集分辨率
- `autoExposure`：自动曝光（默认 `false`）

> 渲染（`render`）
- `vsync`：垂直同步（默认 `false`）
- `doubleBuffer`：双缓冲（默认 `true`，需重启生效）
- `antialias`：多重采样抗锯齿（默认 `false`，需重启生效）

配置读取采用「主配置 → 备份 → 字段级校验 + 默认值合并」策略，发现缺失或非法值会回退到默认值并自动写回。

---

## 启动流程

1. 提升定时器精度，初始化日志（先预读配置以决定是否创建日志文件）
2. 初始化 COM 与 Media Foundation
3. 单实例检测：已有实例运行时，将其窗口置于前台并退出本次启动
4. 显示启动画面（至少展示 2 秒）
5. 加载配置与 `assets` 资源
6. 检测并打开摄像头：配置中的默认摄像头 → 仅有一个设备时直接采用 → 多个设备时弹出选择框 → 均不可用时临时使用任一可用设备
7. 创建并显示全屏展台窗口

无摄像头时显示预设占位图并提示「无摄像头可用」；运行期间每 2 秒巡检一次，支持摄像头热插拔自动重连。

---

## 技术实现

### 技术选型
| 方面 | 方案 |
|---|---|
| 摄像头采集 | Media Foundation `SourceReader`（内置视频处理器完成 MJPEG 解码、缩放与色彩转换） |
| 渲染 | OpenGL（通过 WGL 扩展创建上下文，运行时加载 GL 2.0 函数）+ GLSL 120 着色器 + 纹理上传 |
| 界面 | Win32 API，全部界面元素自绘 |
| 离屏画布 | 自研 `OverlayCanvas`（GDI DIB，32 位预乘 BGRA） |
| 图像编解码 | WIC（JPG / PNG） |
| 配置格式 | 自研 JSON |
| 输入 | `WM_POINTER`（触摸）+ 鼠标消息 |

### 像素格式约定
全流程统一为 **32 位预乘 BGRA、自上而下**，OpenGL 混合使用 `ONE, ONE_MINUS_SRC_ALPHA`，
因此纹理上传、笔迹合成、离屏画布之间无需任何格式转换。

### 线程模型
| 线程 | 职责 |
|---|---|
| 主线程 | Win32 消息循环、输入处理、OpenGL 渲染 |
| 采集线程 | `SourceReader` 读取 → 打包 BGRA 帧 → 放入「最新帧」槽（旧帧直接丢弃） |
| 保存线程 | `JpegSaveQueue` 串行 WIC 编码 JPG，完成后由主线程刷新相册 |

### 性能策略
- **按需出帧**：渲染由 5 ms 定时器驱动，但两次呈现间隔不小于 15 ms；无变化的场景（最小化、设置面板无重绘）不出帧
- **暂停拉流**：锁定画面、查看照片、打开设置、最小化时停止读取（保留设备句柄，恢复迅速）
- **按刷新率丢帧**：采集循环丢弃超出配置刷新率的冗余帧
- **异步保存**：拍照只做一次帧数据拷贝并提交队列，整帧 JPG 编码在后台完成
- **按需上传纹理**：画面/笔迹/面板各自维护 dirty 标记；笔迹仅上传脏矩形
- **缩略图懒加载**：相册只解码最长边 256 的缩略图，展示时才加载原图，解码失败的条目不再重试
- **相册面板优化**：卡片按横坐标排列，命中与渲染均可提前退出；按钮图标在循环外一次取出
- **触摸去重**：消费 `WM_POINTER` 的触摸指针，并按签名过滤系统合成出的鼠标消息，避免一次触摸触发两次
- **笔迹分离存储**：拍照只存原始画面，笔迹另存为 PNG（稀疏内容体积小），导出时按需合成

---

## 资源文件（`assets/`）

| 文件 | 用途 |
|---|---|
| `icon.ico` | 程序图标 |
| `logo.png` | 启动画面 |
| `error.png` | 无摄像头时的占位画面 |
| `pointer.png` / `pen.png` / `eraser.png` | 选择 / 批注 / 橡皮模式图标 |
| `spin.png` / `lock.png` / `shoot.png` / `album.png` | 旋转 / 锁定 / 拍照 / 相册图标 |
| `delete.png` / `save.png` / `show.png` | 相册卡片按钮（删除 / 保存 / 展示） |
| `save_picture.png` / `import_picture.png` | 相册标题行按钮（全部保存 / 导入照片） |
| `checkbox.png` / `checkbox_ok.png` | 「合成笔迹」选择框（未选 / 已选） |
| `more.png` | 「更多」标签 |
| `setting.png` / `minimize.png` / `exit.png` | 设置 / 最小化 / 退出图标 |
| `icon.png` / `slidebutton.png` | 当前未使用 |

---

## 说明与限制

- 触摸手势需要支持触摸的一体机（依赖 Windows 8 及以上引入的指针消息）
- 「双缓冲」与「抗锯齿」需重启程序生效
- 画面拖动边界：画面不小于窗口时可拖到「画面边缘越过窗口边缘约半个窗口」的位置；画面小于窗口时可贴边对齐，保证画面完整可见
- 无摄像头时占位图不可缩放、旋转、批注、拍照
- 退出不弹确认框，直接清理临时照片与笔迹文件后关闭
