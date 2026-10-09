const { spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');

const POWERSHELL_SCRIPT = String.raw`
$ErrorActionPreference = 'Stop'

$nativeCode = @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class IcontraFullscreenNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MONITORINFO
    {
        public int Size;
        public RECT Monitor;
        public RECT WorkArea;
        public uint Flags;
    }

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsIconic(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsZoomed(IntPtr window);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr window, StringBuilder className, int maximumCount);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
    public static extern int GetWindowLong(IntPtr window, int index);

    [DllImport("user32.dll")]
    public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO information);
}
'@

Add-Type -TypeDefinition $nativeCode
$ownerProcessId = [uint32]$env:ICONTRA_OWNER_PROCESS_ID
$parentProcessId = [int]$env:ICONTRA_PARENT_PROCESS_ID
$parentProcess = Get-Process -Id $parentProcessId -ErrorAction Stop
$tolerance = 8
$captionMask = 0x00C00000
$thickFrameMask = 0x00040000

while ($true) {
    $parentProcess.Refresh()
    if ($parentProcess.HasExited) { break }

    $result = '0'
    $window = [IcontraFullscreenNative]::GetForegroundWindow()
    if ($window -ne [IntPtr]::Zero -and
        [IcontraFullscreenNative]::IsWindowVisible($window) -and
        -not [IcontraFullscreenNative]::IsIconic($window)) {
        [uint32]$windowProcessId = 0
        [void][IcontraFullscreenNative]::GetWindowThreadProcessId(
            $window,
            [ref]$windowProcessId
        )

        $classNameBuffer = [System.Text.StringBuilder]::new(256)
        [void][IcontraFullscreenNative]::GetClassName(
            $window,
            $classNameBuffer,
            $classNameBuffer.Capacity
        )
        $className = $classNameBuffer.ToString()
        $isShellWindow = $className -in @(
            'Progman',
            'WorkerW',
            'Shell_TrayWnd',
            'Shell_SecondaryTrayWnd'
        )

        if ($windowProcessId -ne $ownerProcessId -and -not $isShellWindow) {
            $windowRectangle = [IcontraFullscreenNative+RECT]::new()
            $monitor = [IcontraFullscreenNative]::MonitorFromWindow($window, 2)
            $monitorInformation = [IcontraFullscreenNative+MONITORINFO]::new()
            $monitorInformation.Size = [Runtime.InteropServices.Marshal]::SizeOf(
                [type][IcontraFullscreenNative+MONITORINFO]
            )

            if ([IcontraFullscreenNative]::GetWindowRect($window, [ref]$windowRectangle) -and
                $monitor -ne [IntPtr]::Zero -and
                [IcontraFullscreenNative]::GetMonitorInfo(
                    $monitor,
                    [ref]$monitorInformation
                )) {
                $monitorRectangle = $monitorInformation.Monitor
                $coversMonitor =
                    [Math]::Abs($windowRectangle.Left - $monitorRectangle.Left) -le $tolerance -and
                    [Math]::Abs($windowRectangle.Top - $monitorRectangle.Top) -le $tolerance -and
                    [Math]::Abs($windowRectangle.Right - $monitorRectangle.Right) -le $tolerance -and
                    [Math]::Abs($windowRectangle.Bottom - $monitorRectangle.Bottom) -le $tolerance

                $style = [IcontraFullscreenNative]::GetWindowLong($window, -16)
                $hasNormalFrame =
                    ($style -band $captionMask) -ne 0 -or
                    ($style -band $thickFrameMask) -ne 0
                $ordinaryMaximizedWindow =
                    [IcontraFullscreenNative]::IsZoomed($window) -and $hasNormalFrame

                if ($coversMonitor -and -not $ordinaryMaximizedWindow) {
                    $result = '1|{0}|{1}|{2}|{3}' -f
                        $monitorRectangle.Left,
                        $monitorRectangle.Top,
                        $monitorRectangle.Right,
                        $monitorRectangle.Bottom
                }
            }
        }
    }

    [Console]::Out.WriteLine($result)
    [Console]::Out.Flush()
    Start-Sleep -Milliseconds 350
}
`;

const ENCODED_SCRIPT = Buffer.from(POWERSHELL_SCRIPT, 'utf16le').toString(
  'base64',
);

function parseFullscreenLine(line) {
  const parts = line.trim().split('|');
  if (parts[0] === '0') return { active: false };
  if (parts[0] !== '1' || parts.length !== 5) return null;

  const coordinates = parts.slice(1).map(Number);
  if (!coordinates.every(Number.isFinite)) return null;
  const [left, top, right, bottom] = coordinates;
  if (right <= left || bottom <= top) return null;
  return { active: true, left, top, right, bottom };
}

function fullscreenCoversBounds(fullscreen, bounds) {
  if (!fullscreen?.active || !bounds) return false;
  const centerX = bounds.x + bounds.width / 2;
  const centerY = bounds.y + bounds.height / 2;
  return (
    centerX >= fullscreen.left &&
    centerX < fullscreen.right &&
    centerY >= fullscreen.top &&
    centerY < fullscreen.bottom
  );
}

class WindowsFullscreenMonitor extends EventEmitter {
  constructor(ownerProcessId = process.pid) {
    super();
    this.ownerProcessId = ownerProcessId;
    this.child = null;
    this.buffer = '';
    this.stopping = false;
  }

  start() {
    if (process.platform !== 'win32' || this.child) return;
    this.stopping = false;
    this.child = spawn(
      'powershell.exe',
      [
        '-NoLogo',
        '-NoProfile',
        '-NonInteractive',
        '-WindowStyle',
        'Hidden',
        '-EncodedCommand',
        ENCODED_SCRIPT,
      ],
      {
        windowsHide: true,
        stdio: ['ignore', 'pipe', 'pipe'],
        env: {
          ...process.env,
          ICONTRA_OWNER_PROCESS_ID: String(this.ownerProcessId),
          ICONTRA_PARENT_PROCESS_ID: String(process.pid),
        },
      },
    );

    this.child.stdout.setEncoding('utf8');
    this.child.stdout.on('data', (chunk) => this.consume(chunk));
    // Windows PowerShell writes an informational CLIXML header to stderr for
    // encoded commands. Actual monitor failures are reported by the exit event.
    this.child.stderr.resume();
    this.child.on('error', (error) => this.emit('warning', error.message));
    this.child.on('exit', () => {
      this.child = null;
      this.buffer = '';
      if (!this.stopping) this.emit('warning', '全屏检测进程意外退出。');
    });
  }

  consume(chunk) {
    this.buffer += chunk;
    const lines = this.buffer.split(/\r?\n/);
    this.buffer = lines.pop() || '';
    for (const line of lines) {
      const state = parseFullscreenLine(line);
      if (state) this.emit('change', state);
    }
  }

  stop() {
    this.stopping = true;
    if (this.child) this.child.kill();
    this.child = null;
    this.buffer = '';
  }
}

module.exports = {
  WindowsFullscreenMonitor,
  fullscreenCoversBounds,
  parseFullscreenLine,
};
