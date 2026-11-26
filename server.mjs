import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';

const files = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/style.css', ['style.css', 'text/css; charset=utf-8']],
  ['/app.mjs', ['app.mjs', 'text/javascript; charset=utf-8']],
  ['/gps.mjs', ['gps.mjs', 'text/javascript; charset=utf-8']],
]);

createServer(async (request, response) => {
  const file = files.get(request.url.split('?')[0]);
  if (!file) {
    response.writeHead(404).end();
    return;
  }
  try {
    const contents = await readFile(new URL(`./dist/${file[0]}`, import.meta.url));
    response.writeHead(200, {
      'Content-Type': file[1],
      'Cache-Control': 'no-store',
      'Permissions-Policy': 'serial=(self)',
    }).end(contents);
  } catch (error) {
    console.error(error);
    response.writeHead(500).end('Unable to load the tracker app.');
  }
}).listen(8787, '127.0.0.1', () => console.log('Dog Tracker is running at http://127.0.0.1:8787'));
