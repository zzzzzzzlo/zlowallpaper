import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ApiClient, ApiError } from '../public/js/api.js';
import { MockApi } from '../public/js/mock-api.js';
import {
    DEFAULT_BASE,
    MAX_JSON,
    parsePrice,
    normalizeBase,
    safeImageUrl,
    validateMutation,
    validateAsset,
    validateFile,
} from '../public/js/contracts.js';
import { createStaticServer } from '../server.mjs';
import { startFixture } from './http-fixture.mjs';

const png = Buffer.from(
    '89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000b49444154789c636000020000050001a5f645400000000049454e44ae426082',
    'hex'
);
const file = (name = 'cover.png') => new File([png], name, { type: 'image/png' });
const setup = () => {
    const mock = new MockApi({ latency: 0 });
    const api = new ApiClient({ base: DEFAULT_BASE, transport: mock.fetch });
    return { mock, api };
};
const login = (api) => api.login('admin', 'Admin12345');
const errorResponse = (status, code, message = '模拟错误') =>
    new Response(JSON.stringify({ code, message, data: null, requestId: 'test-id' }), {
        status,
        headers: { 'Content-Type': 'application/json' },
    });

test('价格按整数分处理，拒绝浮点精度、负数、科学计数和超上限输入', () => {
    assert.equal(parsePrice('0.29'), 29);
    assert.equal(parsePrice('12.3'), 1230);
    assert.equal(parsePrice('1000000.00'), 100000000);
    for (const value of ['-1', '1.001', '1e2', 'NaN', '1000000.01', '01', ''])
        assert.throws(() => parsePrice(value));
});
test('API 根地址规范化，拒绝非本机明文和凭证/query/fragment', () => {
    assert.equal(normalizeBase(DEFAULT_BASE + '/'), DEFAULT_BASE);
    assert.equal(normalizeBase('https://store.example/api/v1'), 'https://store.example/api/v1');
    for (const value of [
        'http://example.com/api/v1',
        'https://a:b@example.com/api/v1',
        DEFAULT_BASE + '?q=1',
        DEFAULT_BASE + '#x',
        'https://example.com/api',
    ])
        assert.throws(() => normalizeBase(value));
    assert.equal(safeImageUrl('javascript:alert(1)'), '');
    assert.equal(safeImageUrl('http://example.com/a.png'), '');
});
test('请求体白名单与必填项，拒绝 isAdmin/currency/width 等契约外字段', () => {
    for (const body of [
        {},
        { isAdmin: true },
        { title: '' },
        { priceCents: 0.3 },
        { width: 1920 },
        { currency: 'CNY' },
    ])
        assert.throws(() => validateMutation(body));
    assert.throws(() => validateMutation({ title: '测试' }, true));
});
test('上传边界：非空、扩展名、4 MiB 封面、10 GiB 原文件', () => {
    assert.equal(validateFile(file(), 'COVER'), 'png');
    assert.throws(() => validateFile({ name: 'cover.png', size: 4194305 }, 'COVER'));
    assert.throws(() => validateFile({ name: 'video.mp4', size: 10737418241 }, 'ORIGINAL'));
    assert.throws(() => validateFile({ name: 'image.svg', size: 42 }, 'COVER'));
    assert.throws(() => validateFile({ name: 'image.png', size: 0 }, 'COVER'));
});
test('错误密码为 401；普通用户登录后管理员接口为 403，保持状态码', async () => {
    const { api } = setup();
    await assert.rejects(
        api.login('admin', 'Wrong123'),
        (error) => error.status === 401 && error.code === 'INVALID_CREDENTIALS'
    );
    await api.login('viewer', 'Viewer12345');
    await assert.rejects(api.list(), (error) => error.status === 403 && error.code === 'FORBIDDEN');
    assert.ok(api.session, '403 不应被当作 401');
});
test('管理员商品分页含草稿和下架商品；query 仅 page/pageSize', async () => {
    const { api } = setup();
    await login(api);
    const first = await api.list();
    const second = await api.list(2, 12);
    assert.equal(first.items.length, 12);
    assert.equal(first.total, 14);
    assert.equal(second.items.length, 2);
    assert.ok(first.items.some((item) => item.publicationStatus === 'DRAFT'));
    assert.ok(first.items.some((item) => item.publicationStatus === 'WITHDRAWN'));
    await assert.rejects(api.list(0));
    await assert.rejects(api.list(1, 51));
});
test('上传为 201 Asset.id，创建为 201 DRAFT，编辑和上下架为 200；更换原文件改变版本', async () => {
    const { api } = setup();
    await login(api);
    const cover = await api.upload(file(), 'COVER');
    const original = await api.upload(file('wallpaper.png'), 'ORIGINAL');
    assert.ok(cover.id);
    assert.equal(cover.assetId, undefined);
    assert.equal(cover.sha256.length, 64);
    const created = await api.create({
        title: '新商品',
        categoryId: 'nature',
        priceCents: 1299,
        originalAssetId: original.id,
        coverAssetId: cover.id,
        creator: '测试作者',
    });
    assert.equal(created.publicationStatus, 'DRAFT');
    assert.equal(created.originalAssetId, original.id);
    assert.equal(created.coverAssetId, cover.id);
    const edited = await api.update(created.id, { title: '修改标题' });
    assert.equal(edited.resourceVersion, created.resourceVersion);
    const published = await api.publish(created.id, 'PUBLISHED');
    assert.equal(published.publicationStatus, 'PUBLISHED');
    const replacement = await api.upload(file('replacement.png'), 'ORIGINAL');
    const updated = await api.update(created.id, { originalAssetId: replacement.id });
    assert.equal(updated.resourceVersion, '2');
    const withdrawn = await api.publish(created.id, 'WITHDRAWN');
    assert.equal(withdrawn.publicationStatus, 'WITHDRAWN');
    assert.throws(() => api.publish(created.id, 'DRAFT'));
});
test('PROCESSING/FAILED 上传结果保留真实状态，服务端禁止使用未就绪资产', async () => {
    const { api, mock } = setup();
    await login(api);
    const original = await api.upload(file(), 'ORIGINAL');
    mock.uploadStatus = 'PROCESSING';
    const cover = await api.upload(file(), 'COVER');
    assert.equal(cover.status, 'PROCESSING');
    await assert.rejects(
        api.create({
            title: '处理中',
            categoryId: 'nature',
            priceCents: 0,
            originalAssetId: original.id,
            coverAssetId: cover.id,
        }),
        (error) => error.status === 422
    );
    mock.uploadStatus = 'FAILED';
    assert.equal((await api.upload(file(), 'COVER')).status, 'FAILED');
    assert.throws(() => validateAsset({ assetId: 'invented-field' }));
});
test('自动刷新旋转令牌，拒绝重放旧 refreshToken；退出使原 accessToken 失效', async () => {
    const { api, mock } = setup();
    const session = await login(api);
    api.session.expiresAt = Date.now() + 1;
    await api.list();
    assert.notEqual(api.session.accessToken, session.accessToken);
    const replay = await mock.fetch(DEFAULT_BASE + '/auth/refresh', {
        method: 'POST',
        body: JSON.stringify({ refreshToken: session.refreshToken }),
    });
    assert.equal(replay.status, 401);
    const token = api.session.accessToken;
    await api.logout();
    assert.equal(api.session, null);
    assert.equal(
        (
            await mock.fetch(DEFAULT_BASE + '/admin/wallpapers', {
                headers: { Authorization: `Bearer ${token}` },
            })
        ).status,
        401
    );
});
test('401 清除会话并通知 UI；503/空列表保留实际失败和空状态', async () => {
    const { api, mock } = setup();
    await login(api);
    let notified = 0,
        expiry;
    api.onExpired = (error) => {
        notified++;
        expiry = error;
    };
    mock.scenario = 'empty';
    assert.equal((await api.list()).total, 0);
    mock.scenario = 'error';
    await assert.rejects(api.list(), (error) => error.status === 503 && !!error.requestId);
    mock.scenario = 'expired';
    await assert.rejects(api.list(), (error) => error.status === 401);
    assert.equal(api.session, null);
    assert.equal(notified, 1);
    assert.equal(expiry.code, 'SESSION_EXPIRED');
    assert.match(expiry.requestId, /^mock-/);
});
test('真实 HTTP 路径：Bearer、multipart boundary、精确字段、创建/编辑/发布全部走 fetch', async (t) => {
    const fixture = await startFixture();
    t.after(() => fixture.close());
    const api = new ApiClient({ base: fixture.base });
    await login(api);
    await api.list();
    const cover = await api.upload(file(), 'COVER');
    const original = await api.upload(file(), 'ORIGINAL');
    const body = {
        title: '真实传输测试',
        categoryId: 'nature',
        priceCents: 29,
        coverAssetId: cover.id,
        originalAssetId: original.id,
    };
    const product = await api.create(body);
    await api.update(product.id, { description: '编辑' });
    await api.publish(product.id, 'PUBLISHED');
    const requests = fixture.requests.filter((request) =>
        request.path.startsWith('/api/v1/admin/')
    );
    assert.ok(requests.every((request) => request.authenticated));
    const upload = requests.find((request) => request.path === '/api/v1/admin/uploads');
    assert.match(upload.contentType, /^multipart\/form-data; boundary=/);
    assert.deepEqual(upload.body, ['file', 'purpose']);
    assert.deepEqual(
        requests.find(
            (request) => request.path === '/api/v1/admin/wallpapers' && request.method === 'POST'
        ).body,
        body
    );
    const preflight = await fetch(fixture.base + '/admin/wallpapers', {
        method: 'OPTIONS',
        headers: {
            Origin: 'http://127.0.0.1:4173',
            'Access-Control-Request-Headers': 'authorization',
            'Access-Control-Request-Method': 'PATCH',
        },
    });
    assert.equal(preflight.status, 204);
    assert.equal(preflight.headers.get('access-control-allow-origin'), 'http://127.0.0.1:4173');
});
test('拒绝 HTTP 200 假错误、错误状态里的 OK、201/200 混淆和非 JSON 响应', async () => {
    for (const response of [
        errorResponse(200, 'FORBIDDEN'),
        errorResponse(403, 'OK'),
        new Response('{}', { status: 201, headers: { 'Content-Type': 'application/json' } }),
        new Response('<html>login</html>', { headers: { 'Content-Type': 'text/html' } }),
    ]) {
        const api = new ApiClient({ base: DEFAULT_BASE, transport: async () => response });
        await assert.rejects(api.categories(), (error) => error.code === 'CONTRACT_ERROR');
    }
});
test('成功 upload 若只给 assetId 不给 id，拒绝；不手动覆写 multipart Content-Type', async () => {
    let headers;
    const api = new ApiClient({
        base: DEFAULT_BASE,
        transport: async (_url, options) => {
            headers = options.headers;
            return new Response(
                JSON.stringify({
                    code: 'OK',
                    message: '',
                    requestId: 'bad-asset',
                    data: { assetId: 'x' },
                }),
                { status: 201, headers: { 'Content-Type': 'application/json' } }
            );
        },
    });
    api.setSession({
        accessToken: 'token',
        refreshToken: 'refresh',
        expiresIn: 900,
        user: { id: 'user', username: 'admin', displayName: 'Admin' },
    });
    await assert.rejects(api.upload(file(), 'COVER'), (error) => error.code === 'CONTRACT_ERROR');
    assert.equal(headers['Content-Type'], undefined);
});
test('JSON 响应体上限包括没有 Content-Length 的流式响应', async () => {
    const api = new ApiClient({
        base: DEFAULT_BASE,
        transport: async () =>
            new Response('a'.repeat(MAX_JSON + 1), {
                headers: { 'Content-Type': 'application/json' },
            }),
    });
    await assert.rejects(api.categories(), (error) => error.code === 'CONTRACT_ERROR');
});
test('超时与取消分开；切换连接后取消旧请求，不重放写请求', async () => {
    const { mock } = setup();
    mock.latency = 150;
    const api = new ApiClient({ base: DEFAULT_BASE, transport: mock.fetch, timeout: 10 });
    await assert.rejects(api.categories(), (error) => error.code === 'TIMEOUT');
    const old = new ApiClient({ base: DEFAULT_BASE, transport: mock.fetch });
    const pending = old.categories();
    old.dispose();
    await assert.rejects(pending, (error) => error.name === 'AbortError');
    await assert.rejects(old.categories(), (error) => error.name === 'AbortError');
});
test('私有写请求返回 401 不自动重试；禁止 API 重定向和发送 cookie', async () => {
    let count = 0;
    const api = new ApiClient({
        base: DEFAULT_BASE,
        transport: async (_url, options) => {
            count++;
            assert.equal(options.redirect, 'error');
            assert.equal(options.credentials, 'omit');
            return errorResponse(401, 'SESSION_EXPIRED');
        },
    });
    api.setSession({
        accessToken: 't',
        refreshToken: 'r',
        expiresIn: 900,
        user: { id: '1', username: 'admin', displayName: 'Admin' },
    });
    await assert.rejects(api.publish('wp-1', 'PUBLISHED'), (error) => error.status === 401);
    assert.equal(count, 1);
});
test('拒绝创建时自动上架、更新其他商品、上下架返回状态不一致', async () => {
    for (const operation of ['create', 'update', 'publish']) {
        const mock = new MockApi({ latency: 0 });
        const api = new ApiClient({
            base: DEFAULT_BASE,
            transport: async (url, options) => {
                const response = await mock.fetch(url, options);
                if (!url.includes('/admin/') || options.method === 'GET') return response;
                const envelope = await response.json();
                if (operation === 'create') envelope.data.publicationStatus = 'PUBLISHED';
                if (operation === 'update') envelope.data.id = 'another-product';
                if (operation === 'publish') envelope.data.publicationStatus = 'PUBLISHED';
                return new Response(JSON.stringify(envelope), {
                    status: response.status,
                    headers: { 'Content-Type': 'application/json' },
                });
            },
        });
        await login(api);
        const request =
            operation === 'create'
                ? api.create({
                      title: '草稿',
                      categoryId: 'nature',
                      priceCents: 0,
                      originalAssetId: 'asset-original-1',
                      coverAssetId: 'asset-cover-1',
                  })
                : operation === 'update'
                  ? api.update('wp-1001', { title: '修改' })
                  : api.publish('wp-1001', 'WITHDRAWN');
        await assert.rejects(request, (error) => error.code === 'CONTRACT_ERROR');
    }
});
test('多个并发私有请求只刷新一次，统一使用旋转后的令牌', async () => {
    const mock = new MockApi({ latency: 5 });
    let refreshCalls = 0;
    const api = new ApiClient({
        base: DEFAULT_BASE,
        transport: (url, options) => {
            if (url.endsWith('/auth/refresh')) refreshCalls++;
            return mock.fetch(url, options);
        },
    });
    await login(api);
    api.session.expiresAt = Date.now() + 1;
    await Promise.all([api.list(1), api.list(2), api.publish('wp-1001', 'WITHDRAWN')]);
    assert.equal(refreshCalls, 1);
});
test('静态 HTTP 服务：页面、JS MIME、拒绝读取仓库文件与写请求', async (t) => {
    const server = createStaticServer();
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    t.after(
        () =>
            new Promise((resolve) => {
                server.close(resolve);
                server.closeAllConnections();
            })
    );
    const base = `http://127.0.0.1:${server.address().port}`;
    assert.equal((await fetch(base)).status, 200);
    assert.match((await fetch(base + '/js/app.js')).headers.get('content-type'), /javascript/);
    assert.equal((await fetch(base + '/package.json')).status, 404);
    assert.equal((await fetch(base + '/%2e%2e%5cpackage.json')).status, 404);
    assert.equal((await fetch(base, { method: 'POST' })).status, 405);
});
