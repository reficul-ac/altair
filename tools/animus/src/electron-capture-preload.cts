const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('altairAnimus', {
  getMapCacheStatus() {
    return ipcRenderer.invoke('map-cache:status');
  }
});
