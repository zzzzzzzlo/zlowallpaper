const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('icontra', {
  getState: () => ipcRenderer.invoke('dock:get-state'),
  addApplications: () => ipcRenderer.invoke('dock:add-apps'),
  removeApplication: (id) => ipcRenderer.invoke('dock:remove-app', id),
  launch: (id) => ipcRenderer.invoke('dock:launch', id),
  toggleDesktopIcons: () => ipcRenderer.invoke('dock:toggle-desktop-icons'),
  updateSettings: (patch) => ipcRenderer.invoke('settings:update', patch),
  refreshIcons: () => ipcRenderer.invoke('settings:refresh-icons'),
  resetPosition: () => ipcRenderer.send('settings:reset-position'),
  setContentSize: (size) => ipcRenderer.send('dock:set-content-size', size),
  onStateChanged: (callback) => {
    const listener = (_event, state) => callback(state);
    ipcRenderer.on('dock:state-changed', listener);
    return () => ipcRenderer.removeListener('dock:state-changed', listener);
  },
  onResetMagnification: (callback) => {
    const listener = () => callback();
    ipcRenderer.on('dock:reset-magnification', listener);
    return () => ipcRenderer.removeListener('dock:reset-magnification', listener);
  },
});
