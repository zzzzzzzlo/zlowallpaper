const fs = require('node:fs');
const path = require('node:path');

const DEFAULT_CONFIG = Object.freeze({
  version: 6,
  apps: [],
  window: null,
  alwaysOnTop: true,
  launchAtStartup: false,
  hideButtonEnabled: false,
  desktopIconButtonEnabled: false,
  desktopIconsHidden: false,
  taskbarTransparent: false,
  orientation: 'horizontal',
  scale: 1,
});

function clamp(value, minimum, maximum) {
  return Math.min(Math.max(value, minimum), maximum);
}

function normalizeApp(candidate) {
  if (!candidate || typeof candidate !== 'object') return null;
  if (typeof candidate.id !== 'string' || !candidate.id) return null;
  if (typeof candidate.path !== 'string' || !candidate.path) return null;

  return {
    id: candidate.id,
    name:
      typeof candidate.name === 'string' && candidate.name.trim()
        ? candidate.name.trim()
        : path.parse(candidate.path).name,
    path: candidate.path,
  };
}

function normalizeConfig(candidate) {
  const source = candidate && typeof candidate === 'object' ? candidate : {};
  const apps = Array.isArray(source.apps)
    ? source.apps.map(normalizeApp).filter(Boolean)
    : [];

  const hasWindowPosition =
    source.window &&
    Number.isFinite(source.window.x) &&
    Number.isFinite(source.window.y);

  return {
    version: DEFAULT_CONFIG.version,
    apps,
    // Positions saved before v6 have no orientation.  Treat those as legacy
    // positions so the Dock can adopt the new, orientation-aware default once.
    window: hasWindowPosition
      ? {
          x: Math.round(source.window.x),
          y: Math.round(source.window.y),
          orientation:
            source.window.orientation === 'vertical' ||
            source.window.orientation === 'horizontal'
              ? source.window.orientation
              : null,
        }
      : null,
    alwaysOnTop:
      typeof source.alwaysOnTop === 'boolean'
        ? source.alwaysOnTop
        : DEFAULT_CONFIG.alwaysOnTop,
    launchAtStartup:
      typeof source.launchAtStartup === 'boolean'
        ? source.launchAtStartup
        : DEFAULT_CONFIG.launchAtStartup,
    hideButtonEnabled:
      typeof source.hideButtonEnabled === 'boolean'
        ? source.hideButtonEnabled
        : DEFAULT_CONFIG.hideButtonEnabled,
    desktopIconButtonEnabled:
      typeof source.desktopIconButtonEnabled === 'boolean'
        ? source.desktopIconButtonEnabled
        : DEFAULT_CONFIG.desktopIconButtonEnabled,
    desktopIconsHidden:
      typeof source.desktopIconsHidden === 'boolean'
        ? source.desktopIconsHidden
        : DEFAULT_CONFIG.desktopIconsHidden,
    taskbarTransparent:
      typeof source.taskbarTransparent === 'boolean'
        ? source.taskbarTransparent
        : DEFAULT_CONFIG.taskbarTransparent,
    orientation:
      source.orientation === 'vertical' ? 'vertical' : DEFAULT_CONFIG.orientation,
    scale: Number.isFinite(source.scale)
      ? Math.round(clamp(source.scale, 0.65, 1.5) * 100) / 100
      : DEFAULT_CONFIG.scale,
  };
}

class ConfigStore {
  constructor(filePath) {
    this.filePath = filePath;
    this.data = normalizeConfig(null);
  }

  load() {
    try {
      this.data = normalizeConfig(
        JSON.parse(fs.readFileSync(this.filePath, 'utf8')),
      );
    } catch (error) {
      if (error.code !== 'ENOENT') {
        console.warn('Could not read Icontra configuration:', error.message);
      }
      this.data = normalizeConfig(null);
    }
    return this.data;
  }

  save(nextData = this.data) {
    this.data = normalizeConfig(nextData);
    fs.mkdirSync(path.dirname(this.filePath), { recursive: true });
    const temporaryPath = `${this.filePath}.tmp`;
    fs.writeFileSync(temporaryPath, JSON.stringify(this.data, null, 2), 'utf8');
    fs.renameSync(temporaryPath, this.filePath);
    return this.data;
  }
}

module.exports = { ConfigStore, DEFAULT_CONFIG, normalizeConfig };
