const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');

let win;

function createWindow() {
  win = new BrowserWindow({
    width: 1500,
    height: 950,
    title: 'Vellum — PBR Image Editor',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  });

  win.loadFile('index.html');
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});

ipcMain.handle('open-file-dialog', async () => {
  const result = await dialog.showOpenDialog(win, {
    filters: [{ name: 'Images', extensions: ['png', 'jpg', 'jpeg', 'gif', 'webp'] }]
  });
  return result.filePaths[0] || null;
});

ipcMain.handle('save-file-dialog', async (event, filename, format) => {
  const ext = format === 'jpg' ? 'jpg' : format === 'zip' ? 'zip' : 'png';
  const result = await dialog.showSaveDialog(win, {
    defaultPath: filename + '.' + ext,
    filters: [{ name: ext.toUpperCase(), extensions: [ext] }]
  });
  return result.filePath || null;
});

// Used when exporting an entire PBR texture set (one file per map) to a folder.
ipcMain.handle('select-folder-dialog', async () => {
  const result = await dialog.showOpenDialog(win, {
    properties: ['openDirectory', 'createDirectory']
  });
  return result.filePaths[0] || null;
});