import http from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = path.resolve(fileURLToPath(new URL('./public/', import.meta.url)));
const port = Number(process.env.PORT || 4173);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid PORT');
const types = {
    '.html': 'text/html',
    '.css': 'text/css',
    '.js': 'text/javascript',
    '.svg': 'image/svg+xml',
    '.png': 'image/png',
    '.ico': 'image/x-icon',
};
export function createStaticServer() {
    return http.createServer(async (request, response) => {
        response.setHeader('X-Content-Type-Options', 'nosniff');
        response.setHeader('Referrer-Policy', 'no-referrer');
        response.setHeader('Cache-Control', 'no-store');
        // CSP source grammar does not support IPv6 host literals. Application URL
        // validation restricts cleartext HTTP APIs/covers to loopback hosts.
        response.setHeader(
            'Content-Security-Policy',
            "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' https: http: blob:; connect-src 'self' https: http:; media-src 'self' blob:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'"
        );
        if (!['GET', 'HEAD'].includes(request.method)) {
            response.writeHead(405, { Allow: 'GET, HEAD' }).end('Method not allowed');
            return;
        }
        try {
            const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
            const relative = pathname === '/' ? 'index.html' : pathname.slice(1);
            const filename = path.resolve(root, relative);
            if (
                !filename.startsWith(root + path.sep) ||
                !types[path.extname(filename)] ||
                !(await stat(filename)).isFile()
            ) {
                response.writeHead(404).end('Not found');
                return;
            }
            const body = await readFile(filename);
            response.writeHead(200, {
                'Content-Type': `${types[path.extname(filename)]}; charset=utf-8`,
                'Content-Length': body.length,
            });
            response.end(request.method === 'HEAD' ? undefined : body);
        } catch {
            response.writeHead(404).end('Not found');
        }
    });
}
if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const server = createStaticServer();
    server.on('error', (error) => {
        console.error(
            error.code === 'EADDRINUSE'
                ? `端口 ${port} 已被占用；设置 PORT 后重试。`
                : error.message
        );
        process.exitCode = 1;
    });
    server.listen(port, '127.0.0.1', () =>
        console.log(
            `ZloWallpaper 管理工作台：http://127.0.0.1:${port}\n仅提供静态页面，不是业务后端。Ctrl+C 停止。`
        )
    );
}
