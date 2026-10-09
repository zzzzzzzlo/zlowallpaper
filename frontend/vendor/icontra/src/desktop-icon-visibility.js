const { execFile } = require('node:child_process');
const { promisify } = require('node:util');

const execFileAsync = promisify(execFile);

function buildDesktopIconVisibilityScript(hidden) {
  const desiredState =
    typeof hidden === 'boolean' ? (hidden ? '1' : '0') : '$null';

  return String.raw`
$ErrorActionPreference = 'Stop'
$desiredState = ${desiredState}
$advancedPath = 'Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class IcontraDesktopView {
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
  public static extern IntPtr SendMessage(
    IntPtr window,
    UInt32 message,
    IntPtr wParam,
    IntPtr lParam
  );

  public static IntPtr FindDesktopView() {
    IntPtr progman = FindWindow("Progman", null);
    IntPtr view = FindWindowEx(progman, IntPtr.Zero, "SHELLDLL_DefView", null);
    if (view != IntPtr.Zero) return view;

    IntPtr worker = IntPtr.Zero;
    while (true) {
      worker = FindWindowEx(IntPtr.Zero, worker, "WorkerW", null);
      if (worker == IntPtr.Zero) break;
      view = FindWindowEx(worker, IntPtr.Zero, "SHELLDLL_DefView", null);
      if (view != IntPtr.Zero) return view;
    }
    return IntPtr.Zero;
  }
}
'@

$advancedKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($advancedPath, $false)
if ($null -eq $advancedKey) { throw 'Could not open the Explorer advanced settings key.' }

$currentHidden = [int]$advancedKey.GetValue('HideIcons', 0)
if ($null -ne $desiredState -and $currentHidden -ne $desiredState) {
  $desktopView = [IcontraDesktopView]::FindDesktopView()
  if ($desktopView -eq [IntPtr]::Zero) { throw 'Could not find the Explorer desktop view.' }

  [void][IcontraDesktopView]::SendMessage(
    $desktopView,
    [uint32]0x0111,
    [IntPtr]0x7402,
    [IntPtr]::Zero
  )
  Start-Sleep -Milliseconds 100
  $currentHidden = [int]$advancedKey.GetValue('HideIcons', 0)
}

[PSCustomObject]@{ hidden = ($currentHidden -eq 1) } | ConvertTo-Json -Compress
`;
}

function parseDesktopIconVisibility(output) {
  const parsed = JSON.parse(String(output).trim());
  if (!parsed || typeof parsed.hidden !== 'boolean') {
    throw new Error('Explorer did not return the desktop icon visibility state.');
  }
  return parsed;
}

async function runDesktopIconVisibilityCommand(hidden) {
  if (process.platform !== 'win32') {
    return { hidden: Boolean(hidden), supported: false };
  }

  const { stdout } = await execFileAsync(
    'powershell.exe',
    [
      '-NoProfile',
      '-NonInteractive',
      '-ExecutionPolicy',
      'Bypass',
      '-Command',
      buildDesktopIconVisibilityScript(hidden),
    ],
    {
      windowsHide: true,
      timeout: 6000,
      maxBuffer: 1024 * 1024,
    },
  );
  return { ...parseDesktopIconVisibility(stdout), supported: true };
}

function getDesktopIconsHidden() {
  return runDesktopIconVisibilityCommand();
}

function setDesktopIconsHidden(hidden) {
  return runDesktopIconVisibilityCommand(Boolean(hidden));
}

module.exports = {
  buildDesktopIconVisibilityScript,
  getDesktopIconsHidden,
  parseDesktopIconVisibility,
  setDesktopIconsHidden,
};
