#ifndef RUNNER_RENDER_ENGINE_H_
#define RUNNER_RENDER_ENGINE_H_

// 渲染引擎的**唯一事实来源**。
//
// 只改下面这个数字：main.cpp 的引擎开关，与「关于」页 / 诊断导出上报的名字
// 会一起变化，不会出现「实际跑一个后端、界面显示另一个」的情况。
//
//   1 = Impeller（Flutter 3.47 起 Windows 的默认后端）
//   0 = Skia
//
// 为什么是 1：
// 6.3.0 时期播放页歌词是整屏逐帧重绘，Impeller-on-Windows 会把光栅线程压满
// （实测播放时 GPU 70%+、窗口缩小即恢复流畅、CPU 仍有富余），当时把这里改成 0
// 退回 Skia 作为迂回。6.4.0 上游重写了歌词渲染（三层 RepaintBoundary、字符
// 离屏光栅化、「性能优先」成为 Windows 默认预设），该瓶颈已消失；而 Skia 在
// Flutter 3.50 上已是待移除的 opt-out 路径，新版歌词那套离屏光栅化在 Skia 下
// 反而会阻塞 UI 线程——切歌 / 拖动进度条时整个界面冻结数秒，音频线程独立所以
// 声音照常。
// A/B 实测：① 上游原版（Impeller）不卡；② 我们代码 + Impeller 不卡；
// ③ 我们代码 + Skia（旧发布版）卡死。唯一变量就是引擎，故恢复默认。
#define MD3MUSIC_USE_IMPELLER 1

#if MD3MUSIC_USE_IMPELLER
#define MD3MUSIC_RENDER_ENGINE "impeller"
#else
#define MD3MUSIC_RENDER_ENGINE "skia"
#endif

#endif  // RUNNER_RENDER_ENGINE_H_
