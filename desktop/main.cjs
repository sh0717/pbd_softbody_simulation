const { app, BrowserWindow, Menu, protocol, session, dialog } = require('electron');
const { readFile, mkdir, writeFile } = require('node:fs/promises');
const path = require('node:path');
const config = require('./config.cjs');
const origin = `${config.scheme}://lab`;
const entryUrl = `${origin}/${config.entry}`;
const smokeOutput = process.argv.includes('--smoke-test') && process.env.SIMULATION_TEST_OUTPUT;
let mainWindow;

protocol.registerSchemesAsPrivileged([
  {
    scheme: config.scheme,
    privileges: {
      standard: true,
      secure: true,
      supportFetchAPI: true,
      corsEnabled: true,
    },
  },
]);
app.setName(config.productName);

async function registerAssets() {
  const assets = new Map();
  const listing = require('./assets.json');
  for (const [file, type] of Object.entries(listing))
    assets.set(file, { body: await readFile(path.join(__dirname, file.slice(1))), type });
  protocol.handle(config.scheme, (request) => {
    const url = new URL(request.url);
    const asset = assets.get(url.pathname);
    if (url.host !== 'lab' || request.method !== 'GET' || !asset)
      return new Response('Not found', { status: 404 });
    return new Response(asset.body, {
      headers: {
        'Content-Type': asset.type,
        'Content-Security-Policy': config.csp,
        'X-Content-Type-Options': 'nosniff',
      },
    });
  });
}

async function start() {
  if (smokeOutput) {
    await mkdir(path.join(smokeOutput, 'profile'), { recursive: true });
    app.setPath('userData', path.join(smokeOutput, 'profile'));
    app.setPath('sessionData', path.join(smokeOutput, 'profile'));
  }
  await app.whenReady();
  app.setAppUserModelId(config.appId);
  Menu.setApplicationMenu(null);
  await registerAssets();
  const errors = [],
    blockedRequests = [];
  session.defaultSession.webRequest.onBeforeRequest(
    { urls: ['http://*/*', 'https://*/*', 'ws://*/*', 'wss://*/*', 'file://*/*'] },
    (details, callback) => {
      blockedRequests.push(details.url);
      callback({ cancel: true });
    },
  );
  const mayFullscreen = (contents, permission) =>
    permission === 'fullscreen' && contents?.getURL() === entryUrl;
  session.defaultSession.setPermissionCheckHandler(mayFullscreen);
  session.defaultSession.setPermissionRequestHandler((contents, permission, callback) =>
    callback(mayFullscreen(contents, permission)),
  );
  session.defaultSession.on('will-download', (event, item) => {
    if (!config.allowBlobDownloads || !item.getURL().startsWith(`blob:${origin}/`))
      event.preventDefault();
  });
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 1000,
    minWidth: 840,
    minHeight: 640,
    useContentSize: true,
    title: config.productName,
    backgroundColor: config.backgroundColor || '#f3f5f2',
    ...(config.icon ? { icon: path.join(__dirname, config.icon) } : {}),
    show: false,
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: true,
      webSecurity: true,
      webviewTag: false,
      backgroundThrottling: !smokeOutput,
    },
  });
  mainWindow.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  mainWindow.webContents.on('will-navigate', (event, url) => {
    if (url !== entryUrl) event.preventDefault();
  });
  mainWindow.webContents.on('will-attach-webview', (event) => event.preventDefault());
  mainWindow.webContents.on('console-message', (details) => {
    if (details.level === 'error') errors.push(details.message);
  });
  mainWindow.webContents.on('preload-error', (_event, _file, error) => errors.push(error.message));
  mainWindow.webContents.on('render-process-gone', (_event, details) => {
    errors.push(`Renderer exited: ${details.reason}`);
    if (!smokeOutput && details.reason !== 'clean-exit') {
      dialog.showErrorBox(
        config.productName,
        '그래픽 렌더러가 종료되었습니다. 실행파일을 다시 실행하십시오.',
      );
      app.quit();
    }
  });
  mainWindow.webContents.on('before-input-event', (event, input) => {
    if (input.type !== 'keyDown' || input.isAutoRepeat) return;
    if (input.key === 'F11') {
      mainWindow.setFullScreen(!mainWindow.isFullScreen());
      event.preventDefault();
    } else if (input.key === 'Escape' && mainWindow.isFullScreen()) {
      mainWindow.setFullScreen(false);
      event.preventDefault();
    }
  });
  if (!smokeOutput) mainWindow.once('ready-to-show', () => mainWindow.show());
  await mainWindow.loadURL(entryUrl);
  if (smokeOutput) {
    const passed = await require('./smoke.cjs').runSmokeTest(mainWindow, smokeOutput, {
      errors,
      blockedRequests,
    });
    app.exit(passed ? 0 : 1);
  }
}

if (!smokeOutput && !app.requestSingleInstanceLock()) app.quit();
else {
  app.on('second-instance', () => {
    if (!mainWindow) return;
    if (mainWindow.isMinimized()) mainWindow.restore();
    mainWindow.show();
    mainWindow.focus();
  });
  app.on('window-all-closed', () => app.quit());
  start().catch(async (error) => {
    if (smokeOutput) {
      await mkdir(smokeOutput, { recursive: true });
      await writeFile(
        path.join(smokeOutput, 'result.json'),
        JSON.stringify({ passed: false, error: error.stack }, null, 2),
      );
    } else dialog.showErrorBox(`${config.productName} 시작 오류`, error.message);
    app.exit(1);
  });
}
