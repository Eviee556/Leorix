const { contextBridge, ipcRenderer } = require('electron');
const path = require('path');

contextBridge.exposeInMainWorld('vellumAPI', {
  openFileDialog: () => ipcRenderer.invoke('open-file-dialog'),
  saveFileDialog: (filename, format) => ipcRenderer.invoke('save-file-dialog', filename, format),
  selectFolderDialog: () => ipcRenderer.invoke('select-folder-dialog'),
  joinPath: (...parts) => path.join(...parts),
  readImageFile: async (filePath) => {
    const fs = require('fs');
    const data = fs.readFileSync(filePath);
    const ext = filePath.split('.').pop().toLowerCase();
    return `data:image/${ext};base64,${data.toString('base64')}`;
  },
  saveImageFile: (filePath, buffer) => {
    const fs = require('fs');
    fs.writeFileSync(filePath, Buffer.from(buffer));
  }
});
