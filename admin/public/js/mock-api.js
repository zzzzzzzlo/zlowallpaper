import { validateMutation, validateFile } from './contracts.js';

const wait = (milliseconds, signal) =>
    new Promise((resolve, reject) => {
        if (signal?.aborted) return reject(new DOMException('Cancelled', 'AbortError'));
        const abort = () => {
            clearTimeout(timer);
            reject(new DOMException('Cancelled', 'AbortError'));
        };
        const timer = setTimeout(() => {
            signal?.removeEventListener('abort', abort);
            resolve();
        }, milliseconds);
        signal?.addEventListener('abort', abort, { once: true });
    });
export class MockApi {
    constructor({ origin = 'http://127.0.0.1:4173', latency = 350 } = {}) {
        this.origin = origin;
        this.latency = latency;
        this.sessions = new Map();
        this.refreshTokens = new Map();
        this.assets = new Map();
        this.previews = new Map();
        this.scenario = 'normal';
        this.uploadStatus = 'READY';
        this.counter = 100;
        this.requestCounter = 0;
        this.categories = [
            { id: 'nature', name: '自然风景' },
            { id: 'abstract', name: '抽象艺术' },
            { id: 'space', name: '宇宙星空' },
        ];
        const titles = [
            '潮汐 · 蓝色时刻',
            '山间的第一束光',
            '轨道之外',
            '静谧的轮廓',
            '群山慢慢醒来',
            '星云漫游',
            '深海的呼吸',
            '风穿过松林',
            '月面来信',
            '无声流动',
            '远山与湖',
            '环形宇宙',
            '海岸线之外',
            '昼夜边界',
        ];
        this.products = titles.map((title, index) => {
            const category = this.categories[index % 3];
            const type = index % 4 === 0 ? 'VIDEO' : index % 4 === 3 ? 'GIF' : 'IMAGE';
            const originalAssetId = `asset-original-${index + 1}`;
            const coverAssetId = `asset-cover-${index + 1}`;
            const sizeBytes = String(
                type === 'VIDEO' ? 28473600 + index * 1024 : 2473980 + index * 1024
            );
            this.assets.set(originalAssetId, {
                id: originalAssetId,
                purpose: 'ORIGINAL',
                status: 'READY',
                mimeType:
                    type === 'VIDEO' ? 'video/mp4' : type === 'GIF' ? 'image/gif' : 'image/png',
                fileExtension: type === 'VIDEO' ? 'mp4' : type === 'GIF' ? 'gif' : 'png',
                sizeBytes,
                sha256: 'a'.repeat(64),
                width: 3840,
                height: 2160,
                fps: type === 'IMAGE' ? 0 : 30,
            });
            this.assets.set(coverAssetId, {
                id: coverAssetId,
                purpose: 'COVER',
                status: 'READY',
                mimeType: 'image/png',
                fileExtension: 'png',
                sizeBytes: '120000',
                sha256: 'b'.repeat(64),
                width: 960,
                height: 540,
                fps: 0,
            });
            return {
                id: `wp-${1001 + index}`,
                title,
                description:
                    '模拟商品，用于演练素材上传、商品编辑与发布流程。此数据不会写入业务后端。',
                categoryId: category.id,
                categoryName: category.name,
                type,
                priceCents: index % 5 === 0 ? 0 : 1200 + index * 100,
                currency: 'CNY',
                coverUrl: `${origin}/assets/${['coast', 'mountain', 'orbit'][index % 3]}.svg`,
                width: 3840,
                height: 2160,
                fps: type === 'IMAGE' ? 0 : 30,
                resourceVersion: String((index % 3) + 1),
                sizeBytes,
                creator: 'Zlo Studio',
                originalAssetId,
                coverAssetId,
                publicationStatus: ['PUBLISHED', 'DRAFT', 'WITHDRAWN'][index % 3],
            };
        });
        this.fetch = this.fetch.bind(this);
    }
    response(status, data, code = 'OK', message = '') {
        return new Response(
            JSON.stringify({ code, message, data, requestId: `mock-${++this.requestCounter}` }),
            { status, headers: { 'Content-Type': 'application/json; charset=utf-8' } }
        );
    }
    error(status, code, message) {
        return this.response(status, null, code, message);
    }
    issue(username) {
        const serial = ++this.counter;
        const accessToken = `mock-access-${serial}`;
        const refreshToken = `mock-refresh-${serial}`;
        this.sessions.set(accessToken, username);
        this.refreshTokens.set(refreshToken, { username, accessToken });
        return {
            accessToken,
            refreshToken,
            expiresIn: 900,
            user: {
                id: `user-${username}`,
                username,
                displayName: username === 'admin' ? '演示管理员' : '普通用户',
            },
        };
    }
    preview(product) {
        return this.previews.get(product.coverAssetId) || product.coverUrl;
    }
    dispose() {
        for (const url of this.previews.values()) URL.revokeObjectURL(url);
        this.previews.clear();
    }
    async fetch(address, options = {}) {
        await wait(this.latency, options.signal);
        const url = new URL(address);
        const route = url.pathname.replace(/^\/api\/v1/, '');
        const method = options.method || 'GET';
        let body = {};
        if (typeof options.body === 'string') {
            try {
                body = JSON.parse(options.body);
            } catch {
                return this.error(400, 'VALIDATION_ERROR', 'JSON 格式错误。');
            }
        }
        if (route === '/auth/login' && method === 'POST') {
            if (
                ![
                    ['admin', 'Admin12345'],
                    ['viewer', 'Viewer12345'],
                ].some(
                    ([username, password]) =>
                        body.username === username && body.password === password
                )
            )
                return this.error(401, 'INVALID_CREDENTIALS', '账号或密码错误。');
            return this.response(200, this.issue(body.username));
        }
        if (route === '/auth/refresh' && method === 'POST') {
            const session = this.refreshTokens.get(body.refreshToken);
            if (!session) return this.error(401, 'SESSION_EXPIRED', '会话已失效，请重新登录。');
            this.refreshTokens.delete(body.refreshToken);
            this.sessions.delete(session.accessToken);
            return this.response(200, this.issue(session.username));
        }
        if (route === '/auth/logout' && method === 'POST') {
            const session = this.refreshTokens.get(body.refreshToken);
            if (session) this.sessions.delete(session.accessToken);
            this.refreshTokens.delete(body.refreshToken);
            return this.response(200, {});
        }
        if (route === '/categories' && method === 'GET') return this.response(200, this.categories);
        if (!route.startsWith('/admin/'))
            return this.error(404, 'PRODUCT_NOT_FOUND', '模拟模式没有这个接口。');
        const token = new Headers(options.headers).get('Authorization')?.replace(/^Bearer /, '');
        const username = this.sessions.get(token);
        if (!username || this.scenario === 'expired')
            return this.error(401, 'SESSION_EXPIRED', '会话已过期，请重新登录。');
        if (username !== 'admin' || this.scenario === 'forbidden')
            return this.error(403, 'FORBIDDEN', '当前账号没有管理员权限。');
        if (this.scenario === 'error')
            return this.error(503, 'SERVICE_UNAVAILABLE', '模拟服务暂不可用；切回正常状态后重试。');
        if (route === '/admin/wallpapers' && method === 'GET') {
            const page = Number(url.searchParams.get('page') || 1);
            const pageSize = Number(url.searchParams.get('pageSize') || 12);
            if (
                !Number.isInteger(page) ||
                page < 1 ||
                !Number.isInteger(pageSize) ||
                pageSize < 1 ||
                pageSize > 50 ||
                [...url.searchParams.keys()].some((key) => !['page', 'pageSize'].includes(key))
            )
                return this.error(422, 'VALIDATION_ERROR', '分页参数无效。');
            const products = this.scenario === 'empty' ? [] : this.products;
            return this.response(200, {
                items: products.slice((page - 1) * pageSize, page * pageSize),
                page,
                pageSize,
                total: products.length,
            });
        }
        if (route === '/admin/uploads' && method === 'POST') {
            if (!(options.body instanceof FormData))
                return this.error(400, 'VALIDATION_ERROR', '上传须为 multipart。');
            const file = options.body.get('file');
            const purpose = options.body.get('purpose');
            try {
                if (!['COVER', 'ORIGINAL'].includes(purpose)) throw new Error('purpose 无效。');
                const extension = validateFile(file, purpose);
                if (file.size > 64 * 1024 * 1024)
                    throw new Error(
                        '仅模拟模式限制为 64 MiB，避免浏览器在内存中计算大文件摘要；真实接口原文件上限仍为 10 GiB。'
                    );
                const bytes = await file.arrayBuffer();
                const hash = await crypto.subtle.digest('SHA-256', bytes);
                if (options.signal?.aborted) throw new DOMException('Cancelled', 'AbortError');
                let width = 1920;
                let height = 1080;
                if (
                    typeof createImageBitmap === 'function' &&
                    ['jpg', 'jpeg', 'png', 'bmp', 'webp', 'gif'].includes(extension)
                ) {
                    const bitmap = await createImageBitmap(file);
                    width = bitmap.width;
                    height = bitmap.height;
                    bitmap.close();
                }
                const asset = {
                    id: `asset-upload-${++this.counter}`,
                    purpose,
                    status: this.uploadStatus,
                    mimeType: file.type || 'application/octet-stream',
                    fileExtension: extension,
                    sizeBytes: String(file.size),
                    sha256: Array.from(new Uint8Array(hash), (byte) =>
                        byte.toString(16).padStart(2, '0')
                    ).join(''),
                    width,
                    height,
                    fps: ['mp4', 'webm', 'mov', 'avi', 'gif'].includes(extension) ? 30 : 0,
                };
                this.assets.set(asset.id, asset);
                if (purpose === 'COVER' && typeof URL.createObjectURL === 'function')
                    this.previews.set(asset.id, URL.createObjectURL(file));
                return this.response(201, asset);
            } catch (error) {
                if (error.name === 'AbortError') throw error;
                return this.error(422, 'VALIDATION_ERROR', error.message);
            }
        }
        const match = route.match(/^\/admin\/wallpapers\/([A-Za-z0-9._-]+)(\/publication)?$/);
        if (
            (route === '/admin/wallpapers' && method === 'POST') ||
            (match && method === 'PATCH' && !match[2])
        ) {
            const create = !match;
            const previous = create ? null : this.products.find((item) => item.id === match[1]);
            if (!create && !previous) return this.error(404, 'PRODUCT_NOT_FOUND', '商品不存在。');
            try {
                validateMutation(body, create);
                const merged = { ...previous, ...body };
                const category = this.categories.find((item) => item.id === merged.categoryId);
                const original = this.assets.get(merged.originalAssetId);
                const cover = this.assets.get(merged.coverAssetId);
                if (
                    !category ||
                    !original ||
                    original.purpose !== 'ORIGINAL' ||
                    !cover ||
                    cover.purpose !== 'COVER' ||
                    original.status !== 'READY' ||
                    cover.status !== 'READY'
                )
                    throw new Error('分类或素材不可用：两份资产都须为 READY。');
                const changed = previous && previous.originalAssetId !== original.id;
                const product = {
                    ...merged,
                    id: previous?.id || `wp-${++this.counter + 2000}`,
                    description: merged.description || '',
                    creator: merged.creator || '',
                    categoryName: category.name,
                    type:
                        original.fileExtension === 'gif'
                            ? 'GIF'
                            : ['mp4', 'webm', 'mov', 'avi'].includes(original.fileExtension)
                              ? 'VIDEO'
                              : 'IMAGE',
                    currency: 'CNY',
                    coverUrl:
                        previous && previous.coverAssetId === cover.id
                            ? previous.coverUrl
                            : `${this.origin}/mock-assets/${cover.id}.${cover.fileExtension}`,
                    width: original.width,
                    height: original.height,
                    fps: original.fps,
                    sizeBytes: original.sizeBytes,
                    resourceVersion: previous
                        ? changed
                            ? String(Number(previous.resourceVersion) + 1)
                            : previous.resourceVersion
                        : '1',
                    publicationStatus: previous?.publicationStatus || 'DRAFT',
                };
                if (previous) this.products[this.products.indexOf(previous)] = product;
                else this.products.unshift(product);
                return this.response(create ? 201 : 200, product);
            } catch (error) {
                return this.error(422, 'VALIDATION_ERROR', error.message);
            }
        }
        if (match?.[2] && method === 'PUT') {
            const product = this.products.find((item) => item.id === match[1]);
            if (!product) return this.error(404, 'PRODUCT_NOT_FOUND', '商品不存在。');
            if (!['PUBLISHED', 'WITHDRAWN'].includes(body.status))
                return this.error(422, 'VALIDATION_ERROR', '上下架状态无效。');
            if (
                body.status === 'PUBLISHED' &&
                [product.originalAssetId, product.coverAssetId].some(
                    (id) => this.assets.get(id)?.status !== 'READY'
                )
            )
                return this.error(422, 'VALIDATION_ERROR', '素材未就绪，不能上架。');
            product.publicationStatus = body.status;
            return this.response(200, product);
        }
        return this.error(404, 'PRODUCT_NOT_FOUND', '模拟模式没有这个接口。');
    }
}
