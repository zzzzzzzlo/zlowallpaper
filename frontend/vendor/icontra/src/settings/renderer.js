const scaleInput = document.querySelector('#scale');
const scaleValue = document.querySelector('#scale-value');
const alwaysOnTop = document.querySelector('#always-on-top');
const launchAtStartup = document.querySelector('#launch-at-startup');
const hideButtonEnabled = document.querySelector('#hide-button-enabled');
const desktopIconsHidden = document.querySelector('#desktop-icons-hidden');
const desktopIconButtonEnabled = document.querySelector('#desktop-icon-button-enabled');
const taskbarTransparent = document.querySelector('#taskbar-transparent');
const orientationHorizontal = document.querySelector('#orientation-horizontal');
const orientationVertical = document.querySelector('#orientation-vertical');
const resetPosition = document.querySelector('#reset-position');
const refreshIcons = document.querySelector('#refresh-icons');
const addApplication = document.querySelector('#add-application');
const applicationList = document.querySelector('#application-list');
const emptyApps = document.querySelector('#empty-apps');
const appCount = document.querySelector('#app-count');
const version = document.querySelector('#version');

let scaleUpdateTimer;

function initial(name) {
  return name.trim()[0]?.toLocaleUpperCase() || '?';
}

function applicationRow(application) {
  const row = document.createElement('div');
  row.className = 'application-row';

  if (application.icon) {
    const icon = document.createElement('img');
    icon.className = 'application-icon';
    icon.src = application.icon;
    icon.alt = '';
    row.append(icon);
  } else {
    const fallback = document.createElement('div');
    fallback.className = 'application-fallback';
    fallback.textContent = initial(application.name);
    row.append(fallback);
  }

  const info = document.createElement('div');
  info.className = 'application-info';
  const name = document.createElement('div');
  name.className = 'application-name';
  name.textContent = application.name;
  const path = document.createElement('div');
  path.className = 'application-path';
  path.textContent = application.path;
  path.title = application.path;
  info.append(name, path);

  const remove = document.createElement('button');
  remove.type = 'button';
  remove.className = 'remove-button';
  remove.textContent = '删除';
  remove.addEventListener('click', async () => {
    remove.disabled = true;
    await window.icontra.removeApplication(application.id);
  });

  row.append(info, remove);
  return row;
}

function render(state) {
  const percentage = Math.round((state.scale || 1) * 100);
  scaleInput.value = String(percentage);
  scaleValue.value = `${percentage}%`;
  scaleValue.textContent = `${percentage}%`;
  alwaysOnTop.checked = state.alwaysOnTop;
  launchAtStartup.checked = state.launchAtStartup;
  launchAtStartup.closest('.setting-row').hidden = Boolean(state.managed);
  hideButtonEnabled.checked = state.hideButtonEnabled;
  desktopIconsHidden.checked = state.desktopIconsHidden;
  desktopIconButtonEnabled.checked = state.desktopIconButtonEnabled;
  taskbarTransparent.checked = state.taskbarTransparent;
  orientationHorizontal.checked = state.orientation !== 'vertical';
  orientationVertical.checked = state.orientation === 'vertical';
  version.textContent = `v${state.version}`;
  appCount.textContent = `${state.apps.length} 个`;
  applicationList.replaceChildren(...state.apps.map(applicationRow));
  emptyApps.hidden = state.apps.length > 0;
}

scaleInput.addEventListener('input', () => {
  scaleValue.value = `${scaleInput.value}%`;
  scaleValue.textContent = `${scaleInput.value}%`;
  clearTimeout(scaleUpdateTimer);
  scaleUpdateTimer = setTimeout(() => {
    window.icontra.updateSettings({ scale: Number(scaleInput.value) / 100 });
  }, 70);
});

alwaysOnTop.addEventListener('change', () => {
  window.icontra.updateSettings({ alwaysOnTop: alwaysOnTop.checked });
});

launchAtStartup.addEventListener('change', () => {
  window.icontra.updateSettings({ launchAtStartup: launchAtStartup.checked });
});

hideButtonEnabled.addEventListener('change', () => {
  window.icontra.updateSettings({
    hideButtonEnabled: hideButtonEnabled.checked,
  });
});

desktopIconsHidden.addEventListener('change', () => {
  window.icontra.updateSettings({
    desktopIconsHidden: desktopIconsHidden.checked,
  });
});

desktopIconButtonEnabled.addEventListener('change', () => {
  window.icontra.updateSettings({
    desktopIconButtonEnabled: desktopIconButtonEnabled.checked,
  });
});

taskbarTransparent.addEventListener('change', () => {
  window.icontra.updateSettings({
    taskbarTransparent: taskbarTransparent.checked,
  });
});

for (const input of [orientationHorizontal, orientationVertical]) {
  input.addEventListener('change', () => {
    if (input.checked) {
      window.icontra.updateSettings({ orientation: input.value });
    }
  });
}

resetPosition.addEventListener('click', () => window.icontra.resetPosition());
addApplication.addEventListener('click', () => window.icontra.addApplications());
refreshIcons.addEventListener('click', async () => {
  refreshIcons.disabled = true;
  refreshIcons.textContent = '正在刷新…';
  await window.icontra.refreshIcons();
  refreshIcons.textContent = '刷新图标';
  refreshIcons.disabled = false;
});

window.icontra.onStateChanged(render);
window.icontra.getState().then(render);
