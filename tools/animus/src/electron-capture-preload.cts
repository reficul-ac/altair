const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('altairAnimus', {
  getMapCacheStatus() {
    return ipcRenderer.invoke('map-cache:status');
  },
  getDemCacheStatus() {
    return ipcRenderer.invoke('dem-cache:status');
  }
});
