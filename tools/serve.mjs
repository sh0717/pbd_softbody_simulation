import http from 'node:http';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const port = Number(process.env.PORT || 4173);
const types = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.txt': 'text/plain; charset=utf-8',
};
const server = http.createServer(async (request, response) => {
  try {
    if (!['GET', 'HEAD'].includes(request.method)) {
      response.writeHead(405);
      response.end();
      return;
    }
    const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
    const servedPath =
      pathname === '/'
        ? '/dist/index.html'
        : pathname.endsWith('/')
          ? `${pathname}index.html`
          : pathname;
    const target = path.resolve(root, `.${servedPath}`);
    const relative = path.relative(root, target);
    if (
      relative.startsWith('..') ||
      path.isAbsolute(relative) ||
      relative.split(path.sep).some((part) => part.startsWith('.')) ||
      (relative.split(path.sep).includes('node_modules') &&
        !relative.replaceAll('\\', '/').startsWith('node_modules/three/'))
    ) {
      response.writeHead(403);
      response.end('Forbidden');
      return;
    }
    const file = await stat(target);
    if (!file.isFile()) {
      response.writeHead(404);
      response.end('Not found');
      return;
    }
    response.writeHead(200, {
      'Content-Type': types[path.extname(target)] || 'application/octet-stream',
      'Content-Length': file.size,
      'Cache-Control': 'no-store',
    });
    if (request.method === 'HEAD') response.end();
    else
      createReadStream(target)
        .on('error', () => response.destroy())
        .pipe(response);
  } catch {
    response.writeHead(404);
    response.end('Not found');
  }
});
server.on('error', (error) => {
  console.error(error.message);
  process.exitCode = 1;
});
server.listen(port, '127.0.0.1', () => console.log(`Simulation Lab: http://127.0.0.1:${port}`));
