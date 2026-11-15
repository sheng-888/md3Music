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
// 为什么是 0：播放页歌词是整屏重绘，Impeller-on-Windows 在这个场景会把光栅
// 线程压满（实测播放时 GPU 70%+、窗口缩小即恢复流畅、CPU 仍有富余）。等引擎
// 侧把这层缓存补上，把这里改回 1 即可恢复默认。
#define MD3MUSIC_USE_IMPELLER 0

#if MD3MUSIC_USE_IMPELLER
#define MD3MUSIC_RENDER_ENGINE "impeller"
#else
#define MD3MUSIC_RENDER_ENGINE "skia"
#endif

#endif  // RUNNER_RENDER_ENGINE_H_
