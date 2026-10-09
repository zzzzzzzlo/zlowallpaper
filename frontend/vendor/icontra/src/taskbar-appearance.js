const { execFile } = require('node:child_process');
const { promisify } = require('node:util');

const execFileAsync = promisify(execFile);

function buildTaskbarAppearanceScript(transparent) {
  const accentState = transparent ? 1 : 0;

  return String.raw`
$ErrorActionPreference = 'Stop'
$accentState = ${accentState}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class IcontraTaskbarAppearance {
  private const int WCA_ACCENT_POLICY = 19;

  [StructLayout(LayoutKind.Sequential)]
  private struct ACCENT_POLICY {
    public int AccentState;
    public int AccentFlags;
    public int GradientColor;
    public int AnimationId;
  }

  [StructLayout(LayoutKind.Sequential)]
  private struct WINDOWCOMPOSITIONATTRIBDATA {
    public int Attrib;
    public IntPtr pvData;
    public int cbData;
  }

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr FindWindow(string className, string windowName);

  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern IntPtr FindWindowEx(
    IntPtr parent,
    IntPtr childAfter,
    string className,
    string windowName
  );

  [DllImport("user32.dll")]
  private static extern int SetWindowCompositionAttribute(
    IntPtr window,
    ref WINDOWCOMPOSITIONATTRIBDATA data
  );

  [DllImport("user32.dll")]
  private static extern IntPtr SendMessage(
    IntPtr window,
    UInt32 message,
    IntPtr wParam,
    IntPtr lParam
  );

  public static bool Apply(IntPtr taskbar, int accentState) {
    var accent = new ACCENT_POLICY {
      AccentState = accentState,
      AccentFlags = accentState == 0 ? 0 : 2,
      GradientColor = 0,
      AnimationId = 0
    };
    IntPtr accentPointer = Marshal.AllocHGlobal(Marshal.SizeOf(accent));
    try {
      Marshal.StructureToPtr(accent, accentPointer, false);
      var data = new WINDOWCOMPOSITIONATTRIBDATA {
        Attrib = WCA_ACCENT_POLICY,
        pvData = accentPointer,
        cbData = Marshal.SizeOf(accent)
      };
      bool applied = SetWindowCompositionAttribute(taskbar, ref data) != 0;
      if (applied && accentState == 0) {
        SendMessage(taskbar, 0x031E, new IntPtr(1), IntPtr.Zero);
      }
      return applied;
    } finally {
      Marshal.FreeHGlobal(accentPointer);
    }
  }
}
'@

$taskbars = @()
$primaryTaskbar = [IcontraTaskbarAppearance]::FindWindow('Shell_TrayWnd', $null)
if ($primaryTaskbar -ne [IntPtr]::Zero) {
  $taskbars += $primaryTaskbar
}

$secondaryTaskbar = [IntPtr]::Zero
while ($true) {
  $secondaryTaskbar = [IcontraTaskbarAppearance]::FindWindowEx(
    [IntPtr]::Zero,
    $secondaryTaskbar,
    'Shell_SecondaryTrayWnd',
    $null
  )
  if ($secondaryTaskbar -eq [IntPtr]::Zero) { break }
  $taskbars += $secondaryTaskbar
}

if ($taskbars.Count -eq 0) { throw 'Could not find the Windows taskbar.' }

$applied = 0
foreach ($taskbar in $taskbars) {
  if ([IcontraTaskbarAppearance]::Apply($taskbar, $accentState)) {
    $applied += 1
  }
}

if ($applied -eq 0) { throw 'Windows did not accept the taskbar appearance update.' }

[PSCustomObject]@{
  transparent = ($accentState -eq 1)
  taskbarCount = $applied
} | ConvertTo-Json -Compress
`;
}

function parseTaskbarAppearance(output) {
  const parsed = JSON.parse(String(output).trim());
  if (
    !parsed ||
    typeof parsed.transparent !== 'boolean' ||
    !Number.isInteger(parsed.taskbarCount) ||
    parsed.taskbarCount < 1
  ) {
    throw new Error('Windows did not return the taskbar appearance state.');
  }
  return parsed;
}

async function setTaskbarTransparent(transparent) {
  if (process.platform !== 'win32') {
    return { transparent: Boolean(transparent), supported: false, taskbarCount: 0 };
  }

  const { stdout } = await execFileAsync(
    'powershell.exe',
    [
      '-NoProfile',
      '-NonInteractive',
      '-ExecutionPolicy',
      'Bypass',
      '-Command',
      buildTaskbarAppearanceScript(Boolean(transparent)),
    ],
    {
      windowsHide: true,
      timeout: 6000,
      maxBuffer: 1024 * 1024,
    },
  );
  return { ...parseTaskbarAppearance(stdout), supported: true };
}

module.exports = {
  buildTaskbarAppearanceScript,
  parseTaskbarAppearance,
  setTaskbarTransparent,
};
