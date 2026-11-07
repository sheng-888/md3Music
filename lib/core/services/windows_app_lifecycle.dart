import 'dart:io' show Directory, File, Platform;

import 'package:flutter/services.dart';

import 'diagnostic_logger.dart';

/// 临时诊断（退出卡顿定位）：把退出各阶段的时间戳追加到
/// `%TEMP%/md3music_shutdown.log`，与原生侧 ShutdownTrace 共用同一时钟
/// （epoch 毫秒），一次复现即可看出是哪一步吃掉了时间。定位后删除。
void traceExitPhase(String tag) {
  try {
    final String path =
        '${Directory.systemTemp.path}${Platform.pathSeparator}'
        'md3music_shutdown.log';
    File(path).writeAsStringSync(
      '${DateTime.now().millisecondsSinceEpoch} $tag\n',
      mode: FileMode.append,
      flush: true,
    );
  } catch (_) {
    // 打点失败不能影响退出。
  }
}

/// Windows「真退出」握手尾步：释放 WinRT 播放器并通知原生销毁窗口。
///
/// Windows 托盘可用时，`SystemNavigator.pop` 只会隐藏窗口而留下进程；
/// 必须走 `finishExit` 原生握手，更新器才能等到应用进程真正结束后替换文件。
Future<void> finishWindowsAppExit() async {
  traceExitPhase('D2-before-dispose-players');
  try {
    await const MethodChannel('com.ryanheise.just_audio.methods')
        .invokeMethod<void>('disposeAllPlayers', <String, dynamic>{});
  } catch (error, stack) {
    DiagnosticLogger.instance.e('退出时释放 Windows 播放器失败: $error\n$stack');
  }
  traceExitPhase('D3-players-disposed');
  await const MethodChannel('md3music/window_tray').invokeMethod<void>(
    'finishExit',
  );
  traceExitPhase('D4-finish-exit-returned');
}
