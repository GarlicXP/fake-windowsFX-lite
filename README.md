# WindowsFX Lite

把 [Echooff3/wobbly-windows](https://github.com/Echooff3/wobbly-windows) 浏览器里那套 **弹簧跟手 + 速度倾斜 + 速度拉伸** 做到 Windows 全局：按住任意普通窗口的标题栏拖动，窗口会像那页 demo 一样滞后、倾斜、拉长，松手后再回弹落位。

这不是 Compiz 网格果冻。原仓库用的是 CSS `translate` + `skew` + `scale`，本工具按同一组公式做仿射变换。

## 使用

1. 运行 `WindowsFXLite.exe`（托盘常驻，不必管理员）
2. 拖任意窗口标题栏
3. 右键托盘图标可开关；`Ctrl+Alt+W` 总开关；拖动中 `Esc` 取消并回到原位

## 编译

```powershell
powershell -ExecutionPolicy Bypass -File setup_tools.ps1
.\build.bat
```

## 调参

`wobbly.ini` 默认值与原作 `script.js` 一致：

| 键 | 原作常量 | 作用 |
|---|---|---|
| `spring_k` | `SPRING_STIFFNESS = 0.12` | 跟手刚度 |
| `spring_damp` | `SPRING_DAMPING = 0.6` | 阻尼 |
| `wobble_factor` | `WOBBLE_FACTOR = 0.08` | 倾斜量 |
| `max_skew` | `maxSkew = 20` | 最大倾角（度） |

## 限制

- 管理员窗口（UAC）普通进程抓不到，会走系统原生拖拽
- 独占全屏游戏通常收不到低级钩子
- 拖动期间显示的是窗口快照，网页/视频不会在拖动中继续播放
- 纯黑像素在形变边缘可能透出桌面（分层窗口 alpha 限制）

## 原理

1. `WH_MOUSE_LL` 发现标题栏拖动后接管
2. `PrintWindow` 抓快照，把真窗口移出屏幕（任务栏按钮仍在）
3. 置顶分层幽灵窗口按原作弹簧积分，再用 `PlgBlt` 画 skew+scale
4. 收敛后把真窗口放到目标位置并销毁幽灵窗口

## 性能优化（核显 / 高分屏）

| 优化 | 说明 |
|---|---|
| 光栅线程池 | 像素数 >= 128K 时按行带并行光栅（线程数 = 核心数-1，上限 7） |
| 4 像素展开 + 64 位定点 | 内层采样展开 4 像素，累加用 64 位防溢出 |
| 仅平移跳光栅 | 形状收敛后只更新位置，复用上帧像素，匀速拖动 CPU 开销趋近 0 |
| 自适应内部分辨率 | 连续 5 帧超预算降 0.8x，余量充足 15 帧回升；降到下限后用 CPU 放大回全分辨率（替代 GDI StretchBlt，核显上快约 3 倍） |
| 高精度帧节拍 | 按平滑实测耗时定节拍，不再 60fps/30fps 来回腰斩 |

`wobbly.ini` 新增两个可调参数：

| 键 | 默认 | 作用 |
|---|---|---|
| `render_scale_min` | `0.5` | 自适应分辨率下限，`0.25~1.0`；机器跟不上时自动降内部分辨率保帧率，`1.0`=永不降 |
| `render_threads` | `0` | 光栅线程总数（含主线程），`0`=自动（核心数-1，最多 7 个） |

真机实测（Intel UHD 630 核显，1604x912 幽灵窗口）：全分辨率平均 3.1ms/帧、峰值 5.7ms/帧，60fps 预算 14.2ms/帧，约 4.5 倍余量。
