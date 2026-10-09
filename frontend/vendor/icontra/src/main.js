const path = require('node:path');
const fs = require('node:fs');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const {
  app,
  BrowserWindow,
  dialog,
  ipcMain,
  Menu,
  nativeImage,
  screen,
  shell,
  Tray,
} = require('electron');
// The Qt host supplies an independent profile before the single-instance lock
// and ConfigStore are created. Original Asterol/standalone Icontra stays untouched.
const zloProfile = process.argv.find((arg) => arg.startsWith('--zlo-user-data='));
if (zloProfile) {
  const directory = zloProfile.slice('--zlo-user-data='.length);
  if (!path.isAbsolute(directory)) throw new Error('ZloWallpaper Dock profile must be absolute');
  fs.mkdirSync(directory, { recursive: true });
  app.setName('ZloWallpaperDock');
  app.setPath('userData', directory);
}
const { ConfigStore } = require('./config-store');
const {
  getDesktopIconsHidden,
  setDesktopIconsHidden,
} = require('./desktop-icon-visibility');
const { setTaskbarTransparent } = require('./taskbar-appearance');
const { resolveIconSources } = require('./icon-resolver');
const { extractWindowsIcon } = require('./windows-icon-extractor');
const {
  WindowsFullscreenMonitor,
  fullscreenCoversBounds,
} = require('./windows-fullscreen-monitor');
const { HostIpc, PROTOCOL_VERSION } = require('./host-ipc');

const INITIAL_WIDTH = 360;
const INITIAL_HEIGHT = 176;
const DOCK_EDGE_INSET = 10;
const HORIZONTAL_DOCK_BOTTOM_INSET = 64;

let dockWindow;
let settingsWindow;
let tray;
let store;
let savePositionTimer;
let cursorMonitorTimer;
let fullscreenMonitor;
let fullscreenState = { active: false };
let dockVisibilityRequested = true;
let compactDockWindow = false;
let cursorWasInside = false;
let isQuitting = false;
let dockWindowReady = false;
let managedHostInitialized = false;
let snapToDefaultPositionAfterLayout = false;
const iconCache = new Map();
// `publicState` is requested by the renderer and the LightWallpaper host at
// almost the same time during startup.  Keep icon work shared and bounded so
// those requests cannot each start a new Windows Shell/PowerShell operation
// for every Dock item.
const iconLoadPromises = new Map();
const iconResolveQueue = [];
const persistentIconCache = new Map();
let activeIconResolutions = 0;
let iconCachePath = '';
let iconCacheWriteTimer;
const ICON_RESOLUTION_CONCURRENCY = 2;
const ICON_CACHE_VERSION = 2;
const hostIpc = new HostIpc();
let lastHostAppsSignature = '';

function clamp(value, minimum, maximum) {
  return Math.min(Math.max(value, minimum), maximum);
}

// Mirrors the Windows command-line quoting rules closely enough for shortcut
// arguments.  This is only used after ShellExecute (Electron's openPath)
// fails, so ordinary shortcut launches retain the native Windows behavior.
function splitWindowsCommandLine(commandLine) {
  const argumentsList = [];
  let current = '';
  let inQuotes = false;
  let backslashes = 0;
  let hasArgument = false;

  for (const character of String(commandLine || '')) {
    if (character === '\\') {
      backslashes += 1;
      continue;
    }

    if (character === '"') {
      current += '\\'.repeat(Math.floor(backslashes / 2));
      if (backslashes % 2 === 0) inQuotes = !inQuotes;
      else current += '"';
      backslashes = 0;
      hasArgument = true;
      continue;
    }

    if (/\s/.test(character) && !inQuotes) {
      current += '\\'.repeat(backslashes);
      backslashes = 0;
      if (hasArgument) {
        argumentsList.push(current);
        current = '';
        hasArgument = false;
      }
      continue;
    }

    current += '\\'.repeat(backslashes);
    backslashes = 0;
    current += character;
    hasArgument = true;
  }

  current += '\\'.repeat(backslashes);
  if (hasArgument) argumentsList.push(current);
  return argumentsList;
}

function reportLaunchFallback(entry, detail) {
  const message = `Dock launch fallback for ${entry.name}: ${detail}`;
  console.warn(message);
  if (hostIpc.enabled) hostIpc.send('warning', { message });
}

function launchShortcutTarget(entry, originalError) {
  if (process.platform !== 'win32' || path.extname(entry.path).toLowerCase() !== '.lnk') {
    return { ok: false, message: originalError };
  }

  let shortcut;
  try {
    shortcut = shell.readShortcutLink(entry.path);
  } catch (error) {
    reportLaunchFallback(entry, `could not read shortcut (${error.message})`);
    return { ok: false, message: originalError };
  }

  const target = String(shortcut.target || '').trim();
  if (!target || !fs.existsSync(target)) {
    reportLaunchFallback(entry, 'shortcut target is unavailable');
    return { ok: false, message: originalError };
  }

  const workingDirectory = String(shortcut.workingDirectory || '').trim();
  const cwd = fs.existsSync(workingDirectory) ? workingDirectory : path.dirname(target);
  try {
    const child = spawn(target, splitWindowsCommandLine(shortcut.args), {
      cwd,
      detached: true,
      stdio: 'ignore',
      windowsHide: true,
    });
    child.once('error', (error) => {
      reportLaunchFallback(entry, `direct target launch failed (${error.message})`);
    });
    child.unref();
    reportLaunchFallback(entry, `recovered from ShellExecute error: ${originalError}`);
    return { ok: true };
  } catch (error) {
    reportLaunchFallback(entry, `direct target launch threw (${error.message})`);
    return { ok: false, message: originalError };
  }
}

function defaultDockPosition(workArea, width, height, orientation) {
  if (orientation === 'vertical') {
    return {
      x: Math.round(workArea.x + workArea.width - width - DOCK_EDGE_INSET),
      y: Math.round(workArea.y + (workArea.height - height) / 2),
    };
  }

  return {
    x: Math.round(workArea.x + (workArea.width - width) / 2),
    // Keep the native transparent Dock window comfortably above the Windows
    // taskbar.  Its visual icons sit inside that window, so 10 px is not
    // enough to prevent the transparent lower area from covering taskbar UI.
    y: Math.round(
      workArea.y + workArea.height - height - HORIZONTAL_DOCK_BOTTOM_INSET,
    ),
  };
}

function initialBounds() {
  // The Dock has a deliberate layout anchor for each direction.  Reapply it
  // on every launch instead of reviving a stale pixel position from a prior
  // display setup or application version.
  const display = screen.getPrimaryDisplay();
  const { x, y, width, height } = display.workArea;

  return {
    ...defaultDockPosition(
      display.workArea,
      INITIAL_WIDTH,
      INITIAL_HEIGHT,
      store.data.orientation,
    ),
    width: INITIAL_WIDTH,
    height: INITIAL_HEIGHT,
  };
}

function iconCacheKey(entry) {
  return String(entry.path || '').toLowerCase();
}

function iconFingerprint(entry) {
  try {
    const stat = fs.statSync(entry.path);
    return `${stat.size}:${Math.floor(stat.mtimeMs)}`;
  } catch {
    return '';
  }
}

function loadPersistentIconCache() {
  iconCachePath = path.join(app.getPath('userData'), 'icon-cache-v1.json');
  try {
    const data = JSON.parse(fs.readFileSync(iconCachePath, 'utf8'));
    if (data.version !== ICON_CACHE_VERSION || !data.entries || typeof data.entries !== 'object') return;
    for (const [key, value] of Object.entries(data.entries)) {
      if (value && typeof value.icon === 'string' && value.icon.length <= 1024 * 1024) {
        persistentIconCache.set(key, value);
      }
    }
  } catch {
    // No cache on first launch, or a stale cache after an interrupted write.
  }
}

function flushPersistentIconCache() {
  clearTimeout(iconCacheWriteTimer);
  iconCacheWriteTimer = undefined;
  if (!iconCachePath) return;
  try {
    const entries = Object.fromEntries(persistentIconCache);
    const temporaryPath = `${iconCachePath}.tmp`;
    fs.writeFileSync(temporaryPath, JSON.stringify({ version: ICON_CACHE_VERSION, entries }), 'utf8');
    fs.rmSync(iconCachePath, { force: true });
    fs.renameSync(temporaryPath, iconCachePath);
  } catch (error) {
    console.warn('Could not persist Dock icon cache:', error.message);
  }
}

function schedulePersistentIconCacheWrite() {
  clearTimeout(iconCacheWriteTimer);
  iconCacheWriteTimer = setTimeout(flushPersistentIconCache, 1200);
}

function pumpIconResolutionQueue() {
  while (activeIconResolutions < ICON_RESOLUTION_CONCURRENCY && iconResolveQueue.length) {
    const next = iconResolveQueue.shift();
    activeIconResolutions += 1;
    Promise.resolve()
      .then(next.work)
      .then(next.resolve, next.reject)
      .finally(() => {
        activeIconResolutions -= 1;
        setImmediate(pumpIconResolutionQueue);
      });
  }
}

function queueIconResolution(work) {
  return new Promise((resolve, reject) => {
    iconResolveQueue.push({ work, resolve, reject });
    pumpIconResolutionQueue();
  });
}

function looksLikeGenericWindowsDocumentIcon(image) {
  if (image.isEmpty()) return false;
  const { width, height } = image.getSize();
  const bitmap = image.toBitmap();
  if (!width || !height || bitmap.length < width * height * 4) return false;

  let opaquePixels = 0;
  let whitePixels = 0;
  let bluePixels = 0;
  for (let offset = 0; offset < bitmap.length; offset += 4) {
    const blue = bitmap[offset];
    const green = bitmap[offset + 1];
    const red = bitmap[offset + 2];
    const alpha = bitmap[offset + 3];
    if (alpha < 96) continue;
    opaquePixels += 1;
    if (red > 225 && green > 225 && blue > 225) whitePixels += 1;
    if (blue > 130 && blue > red + 35 && blue > green + 15) bluePixels += 1;
  }
  if (opaquePixels < 64) return false;
  // Windows uses this mostly-white page + blue-panel artwork when it cannot
  // locate an executable's real resource.  Treat it as a failed lookup rather
  // than publishing the misleading placeholder to the Dock.
  return whitePixels / opaquePixels > 0.42 && bluePixels / opaquePixels > 0.025;
}

async function resolveApplicationIcon(entry) {
  let icon = null;
  const sources = resolveIconSources(entry.path, {
    readShortcutLink: (shortcutPath) => shell.readShortcutLink(shortcutPath),
  });

  for (const source of sources) {
    const candidate = source.path;
    try {
      const extension = path.extname(candidate).toLowerCase();
      const isBitmap = ['.ico', '.png', '.jpg', '.jpeg', '.webp'].includes(extension);
      const requiresShellExtraction = ['shell-item', 'shell-link', 'packaged-app'].includes(source.kind);
      const readNativeIcon = async () => {
        const image = isBitmap
          ? nativeImage.createFromPath(candidate)
          : await app.getFileIcon(candidate, { size: 'large' });
        return image.isEmpty()
          ? { icon: null, generic: false }
          : {
              icon: image.toDataURL(),
              generic: !isBitmap && looksLikeGenericWindowsDocumentIcon(image),
            };
      };
      const readShellIcon = async () => process.platform === 'win32' && !isBitmap
        ? extractWindowsIcon(candidate, source.iconIndex, {
          packagedApp: source.kind === 'packaged-app',
          shellItem: source.kind === 'shell-item',
          shellLink: source.kind === 'shell-link',
        })
        : null;

      // Electron's native shell extraction is inexpensive for ordinary .exe
      // and shortcut targets.  The PowerShell fallback is reserved for the
      // uncommon shell namespace / Store application cases that need it.
      if (requiresShellExtraction) {
        icon = await readShellIcon();
        if (!icon) icon = (await readNativeIcon()).icon;
      } else {
        const nativeResult = await readNativeIcon();
        icon = nativeResult.icon;
        // Do not launch PowerShell for ordinary icons.  Only the verified
        // Windows placeholder falls back to resource extraction, so a broken
        // EXE/shortcut gets its real embedded icon without reintroducing the
        // old startup burst.
        if (!icon || nativeResult.generic) {
          const shellIcon = await readShellIcon();
          if (shellIcon) icon = shellIcon;
        }
      }
      if (icon) break;
    } catch (error) {
      console.warn(`Could not load icon candidate ${candidate}:`, error.message);
    }
  }
  return icon;
}

async function appWithIcon(entry) {
  const key = iconCacheKey(entry);
  if (iconCache.has(key)) return { ...entry, icon: iconCache.get(key) };

  const fingerprint = iconFingerprint(entry);
  const saved = persistentIconCache.get(key);
  if (saved && saved.fingerprint === fingerprint) {
    iconCache.set(key, saved.icon);
    return { ...entry, icon: saved.icon };
  }

  if (!iconLoadPromises.has(key)) {
    const resolution = queueIconResolution(async () => {
      const icon = await resolveApplicationIcon(entry);
      iconCache.set(key, icon);
      if (icon && fingerprint) {
        persistentIconCache.set(key, { fingerprint, icon });
        schedulePersistentIconCacheWrite();
      }
      return icon;
    });
    iconLoadPromises.set(key, resolution);
    void resolution.finally(() => iconLoadPromises.delete(key)).catch(() => {});
  }
  return { ...entry, icon: await iconLoadPromises.get(key) };
}

async function publicState() {
  const apps = await Promise.all(store.data.apps.map(appWithIcon));
  return {
    apps,
    alwaysOnTop: store.data.alwaysOnTop,
    desktopIconButtonEnabled: store.data.desktopIconButtonEnabled,
    desktopIconsHidden: store.data.desktopIconsHidden,
    hideButtonEnabled: store.data.hideButtonEnabled,
    launchAtStartup: hostIpc.enabled ? false : store.data.launchAtStartup,
    managed: hostIpc.enabled,
    orientation: store.data.orientation,
    scale: store.data.scale,
    taskbarTransparent: store.data.taskbarTransparent,
    version: app.getVersion(),
  };
}

async function publicHostState() {
  const state = await publicState();
  const appSignature = state.apps
    .map((application) => `${application.id}\u001f${application.name}\u001f${application.path}`)
    .join('\u001e');
  const appsChanged = appSignature !== lastHostAppsSignature;
  lastHostAppsSignature = appSignature;
  const { apps, ...settings } = state;
  return {
    ...settings,
    appsChanged,
    // Icons are sent only when the application list actually changes. Slider
    // and checkbox updates stay lightweight and cannot reset the Qt list.
    ...(appsChanged ? { apps } : {}),
  };
}

async function publishHostState(requestId = '') {
  if (!hostIpc.enabled || !hostIpc.connected || !store) return;
  const state = await publicHostState();
  hostIpc.send(
    'stateChanged',
    {
      state,
      visible: Boolean(dockWindow && !dockWindow.isDestroyed() && dockWindow.isVisible()),
      visibilityRequested: dockVisibilityRequested,
      fullscreenActive: fullscreenState.active,
    },
    requestId,
  );
}

async function announceManagedReady(requestId = '') {
  if (
    !hostIpc.enabled ||
    !hostIpc.connected ||
    !managedHostInitialized ||
    !dockWindowReady
  ) {
    return;
  }
  const state = await publicState();
  hostIpc.send('ready', { state }, requestId);
  await publishHostState();
}

function stateWindows() {
  return [dockWindow, settingsWindow].filter(
    (window) => window && !window.isDestroyed(),
  );
}

async function broadcastState() {
  const state = await publicState();
  for (const window of stateWindows()) {
    window.webContents.send('dock:state-changed', state);
  }
  void publishHostState();
  return state;
}

function saveStore() {
  try {
    store.save();
  } catch (error) {
    console.error('Could not save Icontra configuration:', error);
  }
}

function rememberWindowPosition() {
  clearTimeout(savePositionTimer);
  savePositionTimer = setTimeout(() => {
    if (!dockWindow || dockWindow.isDestroyed() || compactDockWindow) return;
    const { x, y } = dockWindow.getBounds();
    store.data.window = { x, y, orientation: store.data.orientation };
    saveStore();
  }, 180);
}

function sendMagnificationReset() {
  if (!dockWindow || dockWindow.isDestroyed()) return;
  dockWindow.webContents.send('dock:reset-magnification');
}

function startCursorMonitor() {
  clearInterval(cursorMonitorTimer);
  cursorMonitorTimer = setInterval(() => {
    if (!dockWindow || dockWindow.isDestroyed() || !dockWindow.isVisible()) {
      return;
    }
    const point = screen.getCursorScreenPoint();
    const bounds = dockWindow.getBounds();
    const inside =
      point.x >= bounds.x &&
      point.x < bounds.x + bounds.width &&
      point.y >= bounds.y &&
      point.y < bounds.y + bounds.height;

    if (!inside && cursorWasInside) sendMagnificationReset();
    cursorWasInside = inside;
  }, 80);
}

// Resizing a frameless transparent BrowserWindow can cause Windows to place it
// back in the normal Z-order.  Keep this in one helper so the expanded Dock and
// its compact restore button always use the same explicit topmost level.
// `screen-saver` is intentionally only used for the opt-in always-on-top mode:
// it is the Electron/Win32 level that remains above a fullscreen window.
function applyDockAlwaysOnTop() {
  if (!dockWindow || dockWindow.isDestroyed()) return;

  const enabled = store.data.alwaysOnTop === true;
  dockWindow.setAlwaysOnTop(enabled, enabled ? 'screen-saver' : 'normal');

  // setAlwaysOnTop does not activate the window.  Reasserting the Z-order
  // after a resize keeps the collapsed control above the foreground fullscreen
  // window without stealing focus from it.
  if (enabled && dockWindow.isVisible()) {
    dockWindow.moveTop();
  }
}

function createDockWindow() {
  // The first renderer layout knows the exact content dimensions; snap again
  // then so the visible Dock, not merely its initial placeholder bounds, is
  // aligned to the requested default position.
  snapToDefaultPositionAfterLayout = true;
  dockWindow = new BrowserWindow({
    ...initialBounds(),
    title: 'Icontra',
    transparent: true,
    backgroundColor: '#00000000',
    frame: false,
    hasShadow: false,
    resizable: false,
    maximizable: false,
    fullscreenable: false,
    skipTaskbar: true,
    alwaysOnTop: store.data.alwaysOnTop,
    movable: true,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      webSecurity: true,
    },
  });

  dockWindow.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true });
  applyDockAlwaysOnTop();
  dockWindow.loadFile(path.join(__dirname, 'renderer', 'index.html'));
  dockWindow.once('ready-to-show', () => {
    dockWindowReady = true;
    syncDockVisibility();
    void announceManagedReady();
  });
  dockWindow.on('close', (event) => {
    if (isQuitting) return;
    event.preventDefault();
    hideDock();
  });
  dockWindow.on('move', () => {
    rememberWindowPosition();
    syncDockVisibility();
  });
  dockWindow.on('blur', sendMagnificationReset);
  startCursorMonitor();
}

function fullscreenHidesDock() {
  return (
    dockWindow &&
    !dockWindow.isDestroyed() &&
    fullscreenCoversBounds(fullscreenState, dockWindow.getBounds())
  );
}

function syncDockVisibility() {
  if (!dockWindow || dockWindow.isDestroyed()) return;
  const shouldShow = dockVisibilityRequested && !fullscreenHidesDock();
  let visibilityChanged = false;
  if (shouldShow && !dockWindow.isVisible()) {
    dockWindow.showInactive();
    applyDockAlwaysOnTop();
    visibilityChanged = true;
  }
  if (!shouldShow && dockWindow.isVisible()) {
    sendMagnificationReset();
    cursorWasInside = false;
    dockWindow.hide();
    visibilityChanged = true;
  }
  if (visibilityChanged) refreshTrayMenu();
  if (visibilityChanged) void publishHostState();
}

function showDock() {
  dockVisibilityRequested = true;
  syncDockVisibility();
  refreshTrayMenu();
}

function hideDock() {
  dockVisibilityRequested = false;
  syncDockVisibility();
  refreshTrayMenu();
}

function toggleDockVisibility() {
  if (!dockWindow || dockWindow.isDestroyed()) return;
  if (dockVisibilityRequested) hideDock();
  else showDock();
}

function startFullscreenMonitor() {
  if (process.platform !== 'win32') return;
  fullscreenMonitor = new WindowsFullscreenMonitor(process.pid);
  fullscreenMonitor.on('change', (nextState) => {
    fullscreenState = nextState;
    syncDockVisibility();
    if (hostIpc.enabled) {
      hostIpc.send('fullscreenChanged', { active: nextState.active, bounds: nextState });
      void publishHostState();
    }
  });
  fullscreenMonitor.on('warning', (message) => {
    console.warn('Fullscreen monitoring warning:', message);
  });
  fullscreenMonitor.start();
}

function quitIcontra() {
  if (hostIpc.enabled && !isQuitting) {
    hostIpc.send('exitRequested', { reason: 'dock-menu' });
    return;
  }
  isQuitting = true;
  app.quit();
}

function trayMenuTemplate() {
  return [
    {
      label: dockVisibilityRequested ? '隐藏图标栏' : '显示图标栏',
      click: toggleDockVisibility,
    },
    { label: '打开设置…', click: openSettings },
    { label: '添加应用…', click: chooseApplications },
    { type: 'separator' },
    {
      label: '开机自动启动',
      type: 'checkbox',
      checked: store.data.launchAtStartup,
      click: (item) => updateSettings({ launchAtStartup: item.checked }),
    },
    {
      label: '始终置顶',
      type: 'checkbox',
      checked: store.data.alwaysOnTop,
      click: (item) => updateSettings({ alwaysOnTop: item.checked }),
    },
    { type: 'separator' },
    { label: '退出 Icontra', click: quitIcontra },
  ];
}

function refreshTrayMenu() {
  if (!tray || tray.isDestroyed()) return;
  tray.setContextMenu(Menu.buildFromTemplate(trayMenuTemplate()));
}

async function createTray() {
  if (hostIpc.enabled) return;
  const iconSvg = [
    '<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">',
    '<rect x="2" y="2" width="28" height="28" rx="8" fill="#6257d9"/>',
    '<path d="M11 9h10v3h-3.2v8H21v3H11v-3h3.2v-8H11z" fill="white"/>',
    '</svg>',
  ].join('');
  let icon = nativeImage.createFromDataURL(
    `data:image/svg+xml;base64,${Buffer.from(iconSvg).toString('base64')}`,
  );
  if (icon.isEmpty()) {
    icon = await app.getFileIcon(process.execPath, { size: 'small' });
  } else {
    icon = icon.resize({ width: 16, height: 16 });
  }

  tray = new Tray(icon);
  tray.setToolTip('Icontra');
  tray.on('click', toggleDockVisibility);
  refreshTrayMenu();
}

function createSettingsWindow() {
  settingsWindow = new BrowserWindow({
    width: 780,
    height: 720,
    minWidth: 680,
    minHeight: 600,
    title: 'Icontra 设置',
    backgroundColor: '#f4f5f8',
    autoHideMenuBar: true,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      webSecurity: true,
    },
  });
  settingsWindow.loadFile(path.join(__dirname, 'settings', 'index.html'));
  settingsWindow.once('ready-to-show', () => settingsWindow.show());
  settingsWindow.on('closed', () => {
    settingsWindow = null;
  });
}

function openSettings() {
  sendMagnificationReset();
  if (settingsWindow && !settingsWindow.isDestroyed()) {
    if (settingsWindow.isMinimized()) settingsWindow.restore();
    settingsWindow.show();
    settingsWindow.focus();
    return;
  }
  createSettingsWindow();
}

function dialogParent() {
  if (settingsWindow && !settingsWindow.isDestroyed() && settingsWindow.isVisible()) {
    return settingsWindow;
  }
  if (dockWindow && !dockWindow.isDestroyed() && dockWindow.isVisible()) {
    return dockWindow;
  }
  return undefined;
}

async function addApplicationPaths(paths) {
  if (!Array.isArray(paths) || paths.length === 0) return publicState();
  const normalizedPaths = paths.filter(
    (candidate) => typeof candidate === 'string' && candidate.trim(),
  );
  if (!normalizedPaths.length) return publicState();
  const existingPaths = new Set(
    store.data.apps.map((entry) => entry.path.toLocaleLowerCase()),
  );
  const candidates = [];
  for (const filePath of normalizedPaths) {
    if (existingPaths.has(filePath.toLocaleLowerCase())) continue;
    const entry = {
      id: crypto.randomUUID(),
      name: path.parse(filePath).name,
      path: filePath,
    };
    iconCache.delete(iconCacheKey({ path: filePath }));
    existingPaths.add(filePath.toLocaleLowerCase());
    candidates.push(entry);
  }

  const checkedCandidates = await Promise.all(
    candidates.map(async (entry) => ({
      entry,
      icon: (await appWithIcon(entry)).icon,
    })),
  );
  const unresolved = checkedCandidates
    .filter((candidate) => !candidate.icon)
    .map((candidate) => candidate.entry);
  for (const candidate of checkedCandidates) {
    if (candidate.icon) store.data.apps.push(candidate.entry);
  }
  if (checkedCandidates.some((candidate) => candidate.icon)) saveStore();
  return { state: await broadcastState(), unresolved };
}

async function chooseApplications() {
  const options = {
    title: '添加应用到 Icontra',
    buttonLabel: '添加',
    properties: ['openFile', 'multiSelections'],
    filters: [
      {
        name: '应用与快捷方式',
        extensions: ['exe', 'lnk', 'url', 'appref-ms', 'bat', 'cmd'],
      },
      { name: '所有文件', extensions: ['*'] },
    ],
  };
  const parent = dialogParent();
  const result = parent
    ? await dialog.showOpenDialog(parent, options)
    : await dialog.showOpenDialog(options);

  if (result.canceled || result.filePaths.length === 0) return publicState();
  const imported = await addApplicationPaths(result.filePaths);
  const unresolved = imported.unresolved;

  if (unresolved.length) {
    const options = {
      type: 'warning',
      title: '未导入无法验证图标的项目',
      message: 'Icontra 无法从 Windows 读取以下项目的原始图标，因此没有将它们加入 Dock。',
      detail: unresolved.map((entry) => entry.path).join('\n'),
    };
    const dialogWindow = dialogParent();
    if (dialogWindow) await dialog.showMessageBox(dialogWindow, options);
    else await dialog.showMessageBox(options);
  }

  return imported.state;
}

async function removeApplication(id) {
  const index = store.data.apps.findIndex((entry) => entry.id === id);
  if (index === -1) return publicState();
  iconCache.delete(iconCacheKey(store.data.apps[index]));
  store.data.apps.splice(index, 1);
  saveStore();
  return broadcastState();
}

async function moveApplication(id, destinationIndex) {
  const from = store.data.apps.findIndex((entry) => entry.id === id);
  if (from < 0 || !Number.isInteger(destinationIndex)) return publicState();
  const destination = clamp(destinationIndex, 0, store.data.apps.length - 1);
  if (from === destination) return publicState();
  const [entry] = store.data.apps.splice(from, 1);
  store.data.apps.splice(destination, 0, entry);
  saveStore();
  return broadcastState();
}

function applicationMenu(id) {
  const entry = store.data.apps.find((candidate) => candidate.id === id);
  if (!entry) return;

  Menu.buildFromTemplate([
    { label: `打开 ${entry.name}`, click: () => launchApplication(id) },
    { label: '在文件夹中显示', click: () => shell.showItemInFolder(entry.path) },
    { type: 'separator' },
    { label: '从 Icontra 删除', click: () => removeApplication(id) },
    { type: 'separator' },
    { label: '设置…', click: openSettings },
  ]).popup({ window: dockWindow });
}

function dockMenu() {
  Menu.buildFromTemplate([
    { label: '打开设置…', click: openSettings },
    { label: '添加应用…', click: chooseApplications },
    { type: 'separator' },
    {
      label: '始终置顶',
      type: 'checkbox',
      checked: store.data.alwaysOnTop,
      click: (item) => updateSettings({ alwaysOnTop: item.checked }),
    },
    { type: 'separator' },
    { label: '隐藏图标栏', click: hideDock },
    { label: '退出 Icontra', click: quitIcontra },
  ]).popup({ window: dockWindow });
}

async function launchApplication(id) {
  const entry = store.data.apps.find((candidate) => candidate.id === id);
  if (!entry) return { ok: false, message: '没有找到这个应用。' };

  const errorMessage = await shell.openPath(entry.path);
  if (errorMessage) {
    // Electron maps this to ShellExecute.  Shell integration can temporarily
    // reject a .lnk while Explorer is still settling after sleep/resume.  A
    // resolved shortcut target gives us a safe, local fallback without
    // requiring users to remove and add otherwise valid Dock items.
    const fallback = launchShortcutTarget(entry, errorMessage);
    if (fallback.ok) return fallback;

    const options = {
      type: 'error',
      title: '无法启动应用',
      message: `无法启动 ${entry.name}`,
      detail: errorMessage,
    };
    const parent = dialogParent();
    if (parent) await dialog.showMessageBox(parent, options);
    else await dialog.showMessageBox(options);
    return { ok: false, message: errorMessage };
  }
  return { ok: true };
}

function loginExecutablePath() {
  return process.env.PORTABLE_EXECUTABLE_FILE || process.execPath;
}

function applyLaunchAtStartup() {
  if (hostIpc.enabled || process.platform !== 'win32' || !app.isPackaged) return;
  try {
    app.setLoginItemSettings({
      openAtLogin: store.data.launchAtStartup,
      path: loginExecutablePath(),
    });
  } catch (error) {
    console.warn('Could not update startup setting:', error.message);
  }
}

async function setDesktopIconVisibility(hidden, reportFailure = true) {
  try {
    const result = await setDesktopIconsHidden(hidden);
    if (!result.supported) {
      throw new Error('Desktop icon visibility is available only on Windows.');
    }
    store.data.desktopIconsHidden = result.hidden;
    return true;
  } catch (error) {
    console.warn('Could not update desktop icon visibility:', error.message);
    if (reportFailure) {
      const options = {
        type: 'error',
        title: '无法更新桌面图标',
        message: 'Icontra 无法更改桌面图标的显示状态。',
        detail: error.message,
      };
      const parent = dialogParent();
      if (parent) await dialog.showMessageBox(parent, options);
      else await dialog.showMessageBox(options);
    }
    return false;
  }
}

async function toggleDesktopIconVisibility() {
  let currentlyHidden = store.data.desktopIconsHidden;
  try {
    const result = await getDesktopIconsHidden();
    if (result.supported) currentlyHidden = result.hidden;
  } catch (error) {
    console.warn('Could not read desktop icon visibility:', error.message);
  }
  return updateSettings({ desktopIconsHidden: !currentlyHidden });
}

async function updateTaskbarAppearance(transparent, reportFailure = true) {
  try {
    const result = await setTaskbarTransparent(transparent);
    if (!result.supported) {
      throw new Error('Taskbar transparency is available only on Windows.');
    }
    store.data.taskbarTransparent = result.transparent;
    return true;
  } catch (error) {
    console.warn('Could not update taskbar appearance:', error.message);
    if (reportFailure) {
      const options = {
        type: 'error',
        title: '无法更新任务栏外观',
        message: 'Icontra 无法更改 Windows 任务栏的透明效果。',
        detail: error.message,
      };
      const parent = dialogParent();
      if (parent) await dialog.showMessageBox(parent, options);
      else await dialog.showMessageBox(options);
    }
    return false;
  }
}

async function updateSettings(patch) {
  if (!patch || typeof patch !== 'object') return publicState();
  if (typeof patch.alwaysOnTop === 'boolean') {
    store.data.alwaysOnTop = patch.alwaysOnTop;
    applyDockAlwaysOnTop();
  }
  if (typeof patch.launchAtStartup === 'boolean') {
    if (!hostIpc.enabled) {
      store.data.launchAtStartup = patch.launchAtStartup;
      applyLaunchAtStartup();
    }
  }
  if (typeof patch.hideButtonEnabled === 'boolean') {
    store.data.hideButtonEnabled = patch.hideButtonEnabled;
  }
  if (typeof patch.desktopIconButtonEnabled === 'boolean') {
    store.data.desktopIconButtonEnabled = patch.desktopIconButtonEnabled;
  }
  if (typeof patch.desktopIconsHidden === 'boolean') {
    await setDesktopIconVisibility(patch.desktopIconsHidden);
  }
  if (typeof patch.taskbarTransparent === 'boolean') {
    await updateTaskbarAppearance(patch.taskbarTransparent);
  }
  if (patch.orientation === 'horizontal' || patch.orientation === 'vertical') {
    if (patch.orientation !== store.data.orientation) {
      // Each direction has a different natural edge and center anchor.  Let
      // the renderer report its final dimensions before applying that anchor.
      snapToDefaultPositionAfterLayout = true;
    }
    store.data.orientation = patch.orientation;
  }
  if (Number.isFinite(patch.scale)) {
    store.data.scale = Math.round(clamp(patch.scale, 0.65, 1.5) * 100) / 100;
  }
  saveStore();
  refreshTrayMenu();
  const state = await broadcastState();
  void publishHostState();
  return state;
}

function resetDockPosition() {
  const current = dockWindow.getBounds();
  const workArea = screen.getDisplayMatching(current).workArea;
  const position = defaultDockPosition(
    workArea,
    current.width,
    current.height,
    store.data.orientation,
  );
  snapToDefaultPositionAfterLayout = false;
  dockWindow.setPosition(position.x, position.y, false);
  rememberWindowPosition();
}

function resizeToContent(size) {
  if (!dockWindow || dockWindow.isDestroyed()) return;
  if (!size || !Number.isFinite(size.width) || !Number.isFinite(size.height)) {
    return;
  }
  const current = dockWindow.getBounds();
  const display = screen.getDisplayMatching(current).workArea;
  const compact = size.compact === true;
  compactDockWindow = compact;
  const vertical = size.orientation === 'vertical';
  const minimumWidth = compact ? 56 : vertical ? 120 : 220;
  const minimumHeight = compact ? 56 : vertical ? 160 : 120;
  const nextWidth = clamp(
    Math.round(size.width),
    minimumWidth,
    Math.max(minimumWidth, display.width),
  );
  const nextHeight = clamp(
    Math.round(size.height),
    minimumHeight,
    Math.max(minimumHeight, display.height),
  );
  let x;
  let y;
  if (snapToDefaultPositionAfterLayout) {
    const position = defaultDockPosition(
      display,
      nextWidth,
      nextHeight,
      size.orientation,
    );
    x = position.x;
    y = position.y;
    snapToDefaultPositionAfterLayout = false;
  } else {
    const centerX = current.x + current.width / 2;
    const centerY = current.y + current.height / 2;
    x = clamp(
      Math.round(centerX - nextWidth / 2),
      display.x,
      display.x + display.width - nextWidth,
    );
    const requestedY = size.preserveCenter
      ? Math.round(centerY - nextHeight / 2)
      : current.y;
    y = clamp(
      requestedY,
      display.y,
      display.y + display.height - nextHeight,
    );
  }
  dockWindow.setBounds({ x, y, width: nextWidth, height: nextHeight }, false);
  applyDockAlwaysOnTop();
  syncDockVisibility();
}

function registerIpc() {
  ipcMain.handle('dock:get-state', publicState);
  ipcMain.handle('dock:add-apps', chooseApplications);
  ipcMain.handle('dock:remove-app', (_event, id) => removeApplication(id));
  ipcMain.handle('dock:launch', (_event, id) => launchApplication(id));
  ipcMain.handle('dock:toggle-desktop-icons', toggleDesktopIconVisibility);
  ipcMain.handle('settings:update', (_event, patch) => updateSettings(patch));
  ipcMain.handle('settings:refresh-icons', async () => {
    iconCache.clear();
    iconLoadPromises.clear();
    persistentIconCache.clear();
    try { fs.rmSync(iconCachePath, { force: true }); } catch { /* best effort */ }
    return broadcastState();
  });
  ipcMain.on('dock:set-content-size', (_event, size) => resizeToContent(size));
  ipcMain.on('settings:reset-position', resetDockPosition);
}

function setupManagedHostIpc() {
  if (!hostIpc.enabled) return;

  hostIpc.on('warning', (message) => {
    console.warn(message);
  });
  hostIpc.on('disconnected', () => {
    if (isQuitting) return;
    console.warn('LightWallpaper host IPC disconnected; closing managed Icontra.');
    isQuitting = true;
    app.quit();
  });
  hostIpc.on('message', async (message) => {
    const requestId = message.requestId || '';
    switch (message.type) {
      case 'initialize':
        managedHostInitialized = true;
        await announceManagedReady(requestId);
        break;
      case 'show':
        showDock();
        await publishHostState(requestId);
        break;
      case 'hide':
        hideDock();
        await publishHostState(requestId);
        break;
      case 'toggle':
        toggleDockVisibility();
        await publishHostState(requestId);
        break;
      case 'openSettings':
        hostIpc.send('warning', { message: 'Dock settings are managed from LightWallpaper.' }, requestId);
        break;
      case 'getState':
        await publishHostState(requestId);
        break;
      case 'shutdown':
        isQuitting = true;
        hostIpc.send('shutdownComplete', { processId: process.pid }, requestId);
        setTimeout(() => app.quit(), 20);
        break;
      case 'updateSettings':
        await updateSettings(message.payload || {});
        await publishHostState(requestId);
        break;
      case 'addApplications': {
        const imported = await addApplicationPaths(message.payload?.paths);
        if (imported.unresolved?.length) {
          hostIpc.send(
            'warning',
            { message: `Some selected applications have no usable icon: ${imported.unresolved.map((entry) => entry.path).join(', ')}` },
            requestId,
          );
        }
        await publishHostState(requestId);
        break;
      }
      case 'removeApplication':
        await removeApplication(message.payload?.id);
        await publishHostState(requestId);
        break;
      case 'moveApplication':
        await moveApplication(message.payload?.id, message.payload?.destinationIndex);
        await publishHostState(requestId);
        break;
      case 'resetPosition':
        resetDockPosition();
        await publishHostState(requestId);
        break;
      default:
        hostIpc.send('warning', { message: `Unknown host command: ${message.type}` }, requestId);
        break;
    }
  });
  hostIpc.connect();
}

const hasSingleInstanceLock = app.requestSingleInstanceLock();
if (!hasSingleInstanceLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    showDock();
    if (!hostIpc.enabled) openSettings();
  });
  app.whenReady().then(async () => {
    store = new ConfigStore(path.join(app.getPath('userData'), 'config.json'));
    store.load();
    loadPersistentIconCache();
    registerIpc();
    setupManagedHostIpc();
    createDockWindow();
    // LightWallpaper already owns the authoritative foreground/fullscreen
    // check.  Avoid a second PowerShell polling process in managed mode.
    if (!hostIpc.enabled) startFullscreenMonitor();
    if (!hostIpc.enabled) {
      await createTray();
      applyLaunchAtStartup();
    }
    if (store.data.desktopIconsHidden) {
      await setDesktopIconVisibility(true, false);
    }
    if (store.data.taskbarTransparent) {
      await updateTaskbarAppearance(true, false);
    }
  });
}

app.on('before-quit', () => {
  isQuitting = true;
  flushPersistentIconCache();
  hostIpc.close();
  clearInterval(cursorMonitorTimer);
  if (fullscreenMonitor) fullscreenMonitor.stop();
  if (tray && !tray.isDestroyed()) tray.destroy();
});
app.on('window-all-closed', () => {
  // Icontra stays alive in the system tray until the user explicitly exits.
});
