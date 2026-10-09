const fs = require('node:fs');
const path = require('node:path');
const { fileURLToPath } = require('node:url');

function expandEnvironmentVariables(value, environment = process.env) {
  const lookup = new Map(
    Object.entries(environment).map(([key, entry]) => [key.toLowerCase(), entry]),
  );

  return value.replace(/%([^%]+)%/g, (match, name) => {
    return lookup.get(name.toLowerCase()) || match;
  });
}

function cleanWindowsResourcePath(value, shortcutPath, environment) {
  if (typeof value !== 'string' || !value.trim()) return null;

  let cleaned = expandEnvironmentVariables(value.trim(), environment);
  if (cleaned.startsWith('@') && !cleaned.startsWith('@{')) {
    cleaned = cleaned.slice(1);
  }
  cleaned = cleaned.replace(/^"|"$/g, '').replace(/,\s*-?\d+\s*$/, '').trim();

  if (!cleaned || cleaned.startsWith('@{')) return null;
  if (!path.isAbsolute(cleaned)) {
    cleaned = path.resolve(path.dirname(shortcutPath), cleaned);
  }
  return cleaned;
}

function parseInternetShortcut(contents) {
  const result = {};
  for (const line of contents.split(/\r?\n/)) {
    const separator = line.indexOf('=');
    if (separator === -1) continue;
    const key = line.slice(0, separator).trim().toLowerCase();
    const value = line.slice(separator + 1).trim();
    if (key === 'iconfile') result.iconFile = value;
    if (key === 'iconindex') result.iconIndex = Number.parseInt(value, 10) || 0;
    if (key === 'url') result.url = value;
  }
  return result;
}

function resolveIconSources(
  shortcutPath,
  {
    platform = process.platform,
    environment = process.env,
    readShortcutLink,
    readFile = fs.readFileSync,
  } = {},
) {
  const extension = path.extname(shortcutPath).toLowerCase();
  const sources = [];
  const add = (value, iconIndex = 0, kind = 'resource') => {
    const cleaned = cleanWindowsResourcePath(
      value,
      shortcutPath,
      environment,
    );
    if (cleaned) sources.push({ path: cleaned, iconIndex, kind });
  };

  if (platform === 'win32' && extension === '.lnk' && readShortcutLink) {
    try {
      const details = readShortcutLink(shortcutPath);
      const iconExtension = path.extname(details.icon || '').toLowerCase();

      // A dedicated image or executable icon is the shortcut author's intent.
      // DLL resources need icon-index support that getFileIcon does not expose,
      // so the target executable is a better fallback for those shortcuts.
      if (details.icon && !['.dll', '.icl'].includes(iconExtension)) {
        add(details.icon, details.iconIndex || 0);
      }
      add(details.target);
      add(details.icon, details.iconIndex || 0);
    } catch (error) {
      console.warn(`Could not resolve shortcut ${shortcutPath}:`, error.message);
    }

    // The Shell item's PIDL is the final authoritative fallback for every
    // shortcut. It covers namespace items (This PC, Recycle Bin) and links
    // whose recorded icon resource has gone stale.
    sources.push({ path: shortcutPath, iconIndex: 0, kind: 'shell-link' });
  }

  if (platform === 'win32' && extension === '.url') {
    try {
      const details = parseInternetShortcut(readFile(shortcutPath, 'utf8'));
      add(details.iconFile, details.iconIndex || 0);
      if (details.url && details.url.toLowerCase().startsWith('file:')) {
        add(fileURLToPath(details.url));
      }
    } catch (error) {
      console.warn(`Could not resolve internet shortcut ${shortcutPath}:`, error.message);
    }
  }

  if (['.lnk', '.url'].includes(extension)) {
    // Packaged Windows app shortcuts can have neither target nor icon path.
    // Their package manifest is the authoritative source for the app logo.
    if (extension === '.lnk') add(shortcutPath, 0, 'packaged-app');
  } else {
    add(shortcutPath);
  }

  const seen = new Set();
  return sources.filter((source) => {
    const normalizedPath =
      platform === 'win32' ? source.path.toLowerCase() : source.path;
    const key = `${normalizedPath}:${source.iconIndex}:${source.kind}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

function resolveIconCandidates(shortcutPath, options) {
  return resolveIconSources(shortcutPath, options)
    .filter(
      (source) =>
        !['shell-item', 'shell-link', 'packaged-app'].includes(source.kind),
    )
    .map((source) => source.path);
}

module.exports = {
  cleanWindowsResourcePath,
  expandEnvironmentVariables,
  parseInternetShortcut,
  resolveIconCandidates,
  resolveIconSources,
};
