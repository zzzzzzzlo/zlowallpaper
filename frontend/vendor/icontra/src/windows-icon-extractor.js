const { execFile } = require('node:child_process');
const { promisify } = require('node:util');

const execFileAsync = promisify(execFile);

const POWERSHELL_SCRIPT = String.raw`
$ErrorActionPreference = 'Stop'

$nativeCode = @'
using System;
using System.Runtime.InteropServices;

public static class IcontraNativeIcons
{
    [StructLayout(LayoutKind.Sequential)]
    public struct SIZE
    {
        public int Width;
        public int Height;
    }

    [Flags]
    public enum ShellImageFlags : uint
    {
        BiggerSizeOk = 0x1,
        IconOnly = 0x4,
        ScaleUp = 0x100
    }

    [ComImport]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b")]
    private interface IShellItemImageFactory
    {
        [PreserveSig]
        int GetImage(SIZE size, ShellImageFlags flags, out IntPtr bitmapHandle);
    }

    [ComImport]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("000214F9-0000-0000-C000-000000000046")]
    private interface IShellLinkW
    {
        void GetPath(IntPtr filePath, int characterCount, IntPtr findData, uint flags);
        void GetIDList(out IntPtr itemIdList);
    }

    [ComImport]
    [Guid("00021401-0000-0000-C000-000000000046")]
    private class ShellLink
    {
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
    private static extern int SHCreateItemFromParsingName(
        string path,
        IntPtr bindingContext,
        ref Guid interfaceId,
        [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory imageFactory
    );

    [DllImport("shell32.dll", PreserveSig = true)]
    private static extern int SHCreateItemFromIDList(
        IntPtr itemIdList,
        ref Guid interfaceId,
        [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory imageFactory
    );

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern uint PrivateExtractIcons(
        string fileName,
        int iconIndex,
        int width,
        int height,
        IntPtr[] iconHandles,
        uint[] iconIds,
        uint iconCount,
        uint flags
    );

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool DestroyIcon(IntPtr iconHandle);

    [DllImport("gdi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool DeleteObject(IntPtr objectHandle);

    private static IntPtr GetShellItemImage(IShellItemImageFactory imageFactory, int size)
    {
        try
        {
            SIZE requestedSize = new SIZE { Width = size, Height = size };
            IntPtr bitmapHandle;
            int result = imageFactory.GetImage(
                requestedSize,
                ShellImageFlags.IconOnly |
                ShellImageFlags.BiggerSizeOk |
                ShellImageFlags.ScaleUp,
                out bitmapHandle
            );
            if (result != 0) Marshal.ThrowExceptionForHR(result);
            return bitmapHandle;
        }
        finally
        {
            Marshal.FinalReleaseComObject(imageFactory);
        }
    }

    public static IntPtr GetShellItemBitmap(string path, int size)
    {
        Guid interfaceId = typeof(IShellItemImageFactory).GUID;
        IShellItemImageFactory factory;
        int result = SHCreateItemFromParsingName(
            path,
            IntPtr.Zero,
            ref interfaceId,
            out factory
        );
        if (result != 0) Marshal.ThrowExceptionForHR(result);

        return GetShellItemImage(factory, size);
    }

    public static IntPtr GetShellLinkBitmap(string shortcutPath, int size)
    {
        IShellLinkW link = (IShellLinkW)new ShellLink();
        IntPtr itemIdList = IntPtr.Zero;
        try
        {
            var persistFile = (System.Runtime.InteropServices.ComTypes.IPersistFile)link;
            persistFile.Load(shortcutPath, 0);
            link.GetIDList(out itemIdList);
            if (itemIdList == IntPtr.Zero) {
                throw new InvalidOperationException("The shortcut does not contain a Shell item ID list.");
            }

            Guid interfaceId = typeof(IShellItemImageFactory).GUID;
            IShellItemImageFactory factory;
            int result = SHCreateItemFromIDList(itemIdList, ref interfaceId, out factory);
            if (result != 0) Marshal.ThrowExceptionForHR(result);
            return GetShellItemImage(factory, size);
        }
        finally
        {
            if (itemIdList != IntPtr.Zero) {
                Marshal.FreeCoTaskMem(itemIdList);
            }
            Marshal.FinalReleaseComObject(link);
        }
    }
}
'@

Add-Type -TypeDefinition $nativeCode
$sourcePath = [System.Text.Encoding]::Unicode.GetString(
    [Convert]::FromBase64String($env:ICONTRA_ICON_PATH_BASE64)
)
$mode = $env:ICONTRA_ICON_MODE
$stream = $null

if ($mode -eq 'packaged-app') {
    $shell = New-Object -ComObject Shell.Application
    $folder = $shell.Namespace((Split-Path -LiteralPath $sourcePath))
    $item = $folder.ParseName((Split-Path -Leaf $sourcePath))
    $appUserModelId = $item.ExtendedProperty('System.Link.TargetParsingPath')
    if (-not $appUserModelId -or -not $appUserModelId.Contains('!')) {
        throw "The shortcut does not point to a packaged Windows app: $sourcePath ($appUserModelId)"
    }

    $parts = $appUserModelId.Split('!', 2)
    $packageFamily = $parts[0]
    $applicationId = $parts[1]
    $package = Get-AppxPackage | Where-Object {
        $_.PackageFamilyName -eq $packageFamily
    } | Select-Object -First 1
    if ($null -eq $package) { throw 'The app package is not installed.' }

    $manifestPath = Join-Path $package.InstallLocation 'AppxManifest.xml'
    [xml]$manifest = Get-Content -LiteralPath $manifestPath -Encoding UTF8
    $namespaces = [System.Xml.XmlNamespaceManager]::new($manifest.NameTable)
    $namespaces.AddNamespace('m', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    $namespaces.AddNamespace('uap', 'http://schemas.microsoft.com/appx/manifest/uap/windows10')
    $application = $manifest.SelectSingleNode(
        "//m:Application[@Id='$applicationId']",
        $namespaces
    )
    if ($null -eq $application) {
        $application = $manifest.SelectSingleNode('//m:Application', $namespaces)
    }
    $visualElements = $application.SelectSingleNode('uap:VisualElements', $namespaces)
    if ($null -eq $visualElements) { throw 'The app manifest has no visual elements.' }

    $logo = $visualElements.GetAttribute('Square44x44Logo')
    if (-not $logo) { $logo = $visualElements.GetAttribute('Square150x150Logo') }
    if (-not $logo) { throw 'The app manifest has no logo.' }

    $baseLogoPath = Join-Path $package.InstallLocation ($logo -replace '/', '\\')
    $logoDirectory = Split-Path -LiteralPath $baseLogoPath
    $logoStem = [System.IO.Path]::GetFileNameWithoutExtension($baseLogoPath)
    $logoCandidates = Get-ChildItem -LiteralPath $logoDirectory -File | Where-Object {
        $_.Extension -ieq '.png' -and $_.BaseName.StartsWith($logoStem)
    } | ForEach-Object {
        $score = 100
        if ($_.Name -match 'targetsize-(\d+)') {
            $score = 1000 + [int]$Matches[1]
        } elseif ($_.Name -match 'scale-(\d+)') {
            $score = 500 + [int]$Matches[1]
        }
        if ($_.Name -match 'altform-unplated') { $score += 500 }
        if ($_.Name -match 'lightunplated') { $score -= 250 }
        [PSCustomObject]@{ File = $_; Score = $score }
    } | Sort-Object Score -Descending

    $selectedLogo = $logoCandidates | Select-Object -First 1
    if ($null -eq $selectedLogo) { throw 'No app logo resource was found.' }
    [Console]::Out.Write(
        [Convert]::ToBase64String(
            [System.IO.File]::ReadAllBytes($selectedLogo.File.FullName)
        )
    )
    exit 0
}

if ($mode -eq 'shell-item' -or $mode -eq 'shell-link') {
    Add-Type -AssemblyName PresentationCore
    $bitmapHandle = [IntPtr]::Zero
    try {
        $bitmapHandle = if ($mode -eq 'shell-link') {
            [IcontraNativeIcons]::GetShellLinkBitmap($sourcePath, 64)
        } else {
            [IcontraNativeIcons]::GetShellItemBitmap($sourcePath, 64)
        }
        $bitmapSource = [System.Windows.Interop.Imaging]::CreateBitmapSourceFromHBitmap(
            $bitmapHandle,
            [IntPtr]::Zero,
            [System.Windows.Int32Rect]::Empty,
            [System.Windows.Media.Imaging.BitmapSizeOptions]::FromEmptyOptions()
        )
        $encoder = [System.Windows.Media.Imaging.PngBitmapEncoder]::new()
        $encoder.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bitmapSource))
        $stream = [System.IO.MemoryStream]::new()
        $encoder.Save($stream)
        [Console]::Out.Write([Convert]::ToBase64String($stream.ToArray()))
    }
    finally {
        if ($bitmapHandle -ne [IntPtr]::Zero) {
            [void][IcontraNativeIcons]::DeleteObject($bitmapHandle)
        }
        if ($null -ne $stream) { $stream.Dispose() }
    }
    exit 0
}

Add-Type -AssemblyName System.Drawing
$index = [int]$env:ICONTRA_ICON_INDEX
$handles = [IntPtr[]]::new(1)
$ids = [uint32[]]::new(1)
$icon = $null
$bitmap = $null

try {
    $count = [IcontraNativeIcons]::PrivateExtractIcons(
        $sourcePath,
        $index,
        64,
        64,
        $handles,
        $ids,
        1,
        0
    )

    if ($count -gt 0 -and $handles[0] -ne [IntPtr]::Zero) {
        $borrowedIcon = [System.Drawing.Icon]::FromHandle($handles[0])
        $icon = $borrowedIcon.Clone()
        [void][IcontraNativeIcons]::DestroyIcon($handles[0])
        $handles[0] = [IntPtr]::Zero
    } else {
        $icon = [System.Drawing.Icon]::ExtractAssociatedIcon($sourcePath)
    }

    if ($null -eq $icon) { throw 'Windows returned no icon.' }
    $bitmap = $icon.ToBitmap()
    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    [Console]::Out.Write([Convert]::ToBase64String($stream.ToArray()))
}
finally {
    if ($handles[0] -ne [IntPtr]::Zero) {
        [void][IcontraNativeIcons]::DestroyIcon($handles[0])
    }
    if ($null -ne $stream) { $stream.Dispose() }
    if ($null -ne $bitmap) { $bitmap.Dispose() }
    if ($null -ne $icon) { $icon.Dispose() }
}
`;

const ENCODED_SCRIPT = Buffer.from(POWERSHELL_SCRIPT, 'utf16le').toString(
  'base64',
);

async function extractWindowsIcon(
  filePath,
  iconIndex = 0,
  { shellItem = false, shellLink = false, packagedApp = false } = {},
) {
  if (process.platform !== 'win32') return null;

  const { stdout } = await execFileAsync(
    'powershell.exe',
    [
      '-NoLogo',
      '-NoProfile',
      '-NonInteractive',
      '-Sta',
      '-WindowStyle',
      'Hidden',
      '-EncodedCommand',
      ENCODED_SCRIPT,
    ],
    {
      windowsHide: true,
      timeout: 10_000,
      maxBuffer: 5 * 1024 * 1024,
      env: {
        ...process.env,
        ICONTRA_ICON_PATH_BASE64: Buffer.from(filePath, 'utf16le').toString(
          'base64',
        ),
        ICONTRA_ICON_INDEX: String(iconIndex),
        ICONTRA_ICON_MODE: packagedApp
          ? 'packaged-app'
          : shellLink
            ? 'shell-link'
          : shellItem
            ? 'shell-item'
            : 'resource',
      },
    },
  );

  const base64 = stdout.trim();
  return base64 ? `data:image/png;base64,${base64}` : null;
}

module.exports = { extractWindowsIcon };
