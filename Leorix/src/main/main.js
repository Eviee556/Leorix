const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');

let win;

function createWindow() {
  win = new BrowserWindow({
    width: 1500,
    height: 950,
    minWidth: 900,
    minHeight: 600,
    title: 'Leorix — PBR Image Editor',
    // Frameless: Leorix draws its own title bar so the window can be dragged
    // anywhere on screen from the menu strip.
    frame: false,
    backgroundColor: '#14171c',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  });

  win.loadFile(path.join(__dirname, '../../index.html'));

  win.on('maximize', () => win.webContents.send('window-state', true));
  win.on('unmaximize', () => win.webContents.send('window-state', false));
}

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});

/* ---- Window controls for the custom title bar ---- */
ipcMain.handle('win-minimize', () => { if (win) win.minimize(); });
ipcMain.handle('win-maximize', () => {
  if (!win) return false;
  if (win.isMaximized()) { win.unmaximize(); return false; }
  win.maximize(); return true;
});
ipcMain.handle('win-close', () => { if (win) win.close(); });
ipcMain.handle('win-is-maximized', () => (win ? win.isMaximized() : false));

ipcMain.handle('open-file-dialog', async () => {
  const result = await dialog.showOpenDialog(win, {
    filters: [{ name: 'Images', extensions: ['png', 'jpg', 'jpeg', 'gif', 'webp'] }]
  });
  return result.filePaths[0] || null;
});

ipcMain.handle('save-file-dialog', async (event, filename, format) => {
  const known = ['jpg', 'zip', 'exr', 'usda', 'png'];
  const ext = known.includes(format) ? format : 'png';
  const result = await dialog.showSaveDialog(win, {
    defaultPath: filename + '.' + ext,
    filters: [{ name: ext.toUpperCase(), extensions: [ext] }]
  });
  return result.filePath || null;
});

ipcMain.handle('select-folder-dialog', async () => {
  const result = await dialog.showOpenDialog(win, {
    properties: ['openDirectory', 'createDirectory']
  });
  return result.filePaths[0] || null;
});

ipcMain.handle('save-project-dialog', async (event, filename) => {
  const result = await dialog.showSaveDialog(win, {
    defaultPath: (filename || 'untitled') + '.leorix',
    filters: [{ name: 'Leorix Project', extensions: ['leorix'] }]
  });
  return result.filePath || null;
});

ipcMain.handle('open-project-dialog', async () => {
  const result = await dialog.showOpenDialog(win, {
    filters: [{ name: 'Leorix Project', extensions: ['leorix'] }]
  });
  return result.filePaths[0] || null;
});