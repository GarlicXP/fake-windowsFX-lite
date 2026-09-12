<img width="400" height="225" alt="屏幕录制 2026-09-12 103758" src="https://github.com/user-attachments/assets/7699cbdf-bb07-439e-b674-16617f52cee8" />
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
