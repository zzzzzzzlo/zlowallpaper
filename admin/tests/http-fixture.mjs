// TEST ONLY: wire-level fixture, never a business backend or production auth implementation.
import http from 'node:http';
import { Readable } from 'node:stream';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { MockApi } from '../public/js/mock-api.js';

export async function startFixture({ port = 0, latency = 0 } = {}) {
    const mock = new MockApi({ latency });
    const requests = [];
    const files = new Map();
    const server = http.createServer(async (incoming, outgoing) => {
        const origin = incoming.headers.origin;
        if (origin && /^http:\/\/(?:127\.0\.0\.1|localhost):\d+$/.test(origin))
            outgoing.setHeader('Access-Control-Allow-Origin', origin);
        outgoing.setHeader('Vary', 'Origin');
        outgoing.setHeader('Access-Control-Allow-Methods', 'GET, POST, PATCH, PUT, OPTIONS');
        outgoing.setHeader('Access-Control-Allow-Headers', 'Authorization, Content-Type');
        if (incoming.method === 'OPTIONS') {
            outgoing.writeHead(204).end();
            return;
        }
        const url = `http://127.0.0.1:${server.address().port}${incoming.url}`;
        try {
            if (incoming.url.startsWith('/assets/')) {
                const name = incoming.url.slice('/assets/'.length);
                if (!['coast.svg', 'mountain.svg', 'orbit.svg'].includes(name)) {
                    outgoing.writeHead(404).end();
                    return;
                }
                outgoing
                    .writeHead(200, { 'Content-Type': 'image/svg+xml' })
                    .end(await readFile(new URL(`../public/assets/${name}`, import.meta.url)));
                return;
            }
            if (incoming.url.startsWith('/mock-assets/')) {
                const id = incoming.url.slice('/mock-assets/'.length).split('.')[0];
                const file = files.get(id);
                if (!file) {
                    outgoing.writeHead(404).end();
                    return;
                }
                outgoing
                    .writeHead(200, { 'Content-Type': file.type })
                    .end(Buffer.from(await file.arrayBuffer()));
                return;
            }
            const headers = new Headers(incoming.headers);
            const request = new Request(url, {
                method: incoming.method,
                headers,
                ...(incoming.method === 'GET'
                    ? {}
                    : { body: Readable.toWeb(incoming), duplex: 'half' }),
            });
            const body =
                incoming.method === 'GET'
                    ? undefined
                    : headers.get('Content-Type')?.startsWith('multipart/')
                      ? await request.formData()
                      : await request.text();
            // Capture protocol facts, never passwords/tokens.
            requests.push({
                path: incoming.url,
                method: incoming.method,
                authenticated: /^Bearer /u.test(headers.get('Authorization') || ''),
                contentType: headers.get('Content-Type'),
                body:
                    typeof body === 'string' && !incoming.url.startsWith('/api/v1/auth/')
                        ? JSON.parse(body || '{}')
                        : body instanceof FormData
                          ? [...body.keys()]
                          : null,
            });
            const response = await mock.fetch(url, { method: incoming.method, headers, body });
            const payload = await response.text();
            if (body instanceof FormData && response.status === 201)
                files.set(JSON.parse(payload).data.id, body.get('file'));
            outgoing.writeHead(response.status, Object.fromEntries(response.headers)).end(payload);
        } catch {
            outgoing.writeHead(500, { 'Content-Type': 'application/json' }).end(
                JSON.stringify({
                    code: 'INTERNAL_ERROR',
                    message: '测试夹具错误',
                    data: null,
                    requestId: 'fixture-error',
                })
            );
        }
    });
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(port, '127.0.0.1', resolve);
    });
    mock.origin = `http://127.0.0.1:${server.address().port}`;
    mock.products.forEach((product) => {
        product.coverUrl = product.coverUrl.replace('http://127.0.0.1:4173', mock.origin);
    });
    return {
        server,
        mock,
        requests,
        base: `${mock.origin}/api/v1`,
        close: () =>
            new Promise((resolve) => {
                server.close(resolve);
                server.closeAllConnections();
            }),
    };
}
if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const fixture = await startFixture({
        port: Number(process.env.FIXTURE_PORT || 8080),
        latency: 100,
    });
    console.log(
        `TEST ONLY HTTP fixture: ${fixture.base}\nCredentials: admin / Admin12345. NOT a business backend.`
    );
}
