## 🚀 快速开始
### 1. 准备依赖
从[jsmpeg-vnc官方Release页](https://github.com/phoboslab/jsmpeg-vnc/releases)下载最新版本的`jsmpeg-vnc-win.zip`，解压后将`jsmpeg-vnc.exe`与其同目录文件复制到上述`Bin`目录中。（默认搭载如下版本）
jsmpeg-vnc © Dominic Szablewski (phoboslab)
https://github.com/phoboslab/jsmpeg-vnc
License: GPLv3
Bundled version: v0.2

### 2. 运行GUI
直接双击`jsmpeg-vnc-gui.exe`启动，配置参数后点击「启动」即可：
- **基础推流**：端口填`8080`，窗口名填`desktop`，浏览器访问`http://localhost:8080`
- **游戏推流（鼠标锁定）**：勾选「URL添加?mouselock」，窗口名填游戏窗口标题，访问`http://localhost:8080/?mouselock`
- **低带宽适配**：比特率填`1000`，尺寸填`854x480`，帧率填`24`

### 3. 停止服务
点击GUI内「停止」按钮，或直接关闭GUI窗口，所有资源会自动释放。

## 🛠️ 编译指南（Visual Studio）
1. 新建「Windows桌面向导」→ 选择「桌面应用程序」、「空项目」
2. 将本项目的`main.cpp`添加到项目中
3. 项目属性 → 高级 → 字符集选择「使用Unicode字符集」（或保持默认多字节均可）
4. 直接编译生成，无需额外链接第三方库
> ⚠️ 调试运行时需注意：Visual Studio默认工作目录为项目目录，请确保`Bin`目录位于项目根目录下，或修改调试属性的「工作目录」为`$(TargetDir)`

## ❓ 常见问题
### Q：浏览器访问提示404？
A：检查`Bin/client/index.html`是否存在，若缺失请重新下载jsmpeg-vnc完整包。

### Q：提示端口被占用？
A：GUI启动时会自动释放残留端口，若仍报错可手动更换端口（如8081），或等待TCP的TIME_WAIT状态结束（约1分钟）。

### Q：任务管理器结束GUI后，jsmpeg-vnc仍在运行？
A：正常情况下Job Object机制会自动清理，若出现极端情况，可手动结束`jsmpeg-vnc.exe`进程，不影响后续使用。

## 📜 合规说明
- **本GUI项目**：源代码采用MIT许可证，你可自由修改、分发、商用，无需开源你的修改。
- **jsmpeg-vnc依赖**：© Dominic Szablewski (phoboslab)，遵循[GPLv3许可证](https://www.gnu.org/licenses/gpl-3.0.html)。本项目仅为独立进程调用，不构成派生作品，因此MIT许可证与GPLv3互不冲突。
- ** redistribution要求**：若你需要打包分发包含jsmpeg-vnc的成品，请务必保留jsmpeg-vnc的LICENSE文件及原作者署名。

## 🙏 致谢
本项目基于Dominic Szablewski开发的[jsmpeg-vnc](https://github.com/phoboslab/jsmpeg-vnc)实现，感谢其开源贡献。

---
如果觉得本项目有用，欢迎Star⭐！