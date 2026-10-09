import {
    MAX_JSON,
    normalizeBase,
    validateEnvelope,
    validateSession,
    validateCategories,
    validatePage,
    validateAsset,
    validateProduct,
    validateMutation,
    validateFile,
    validateEmpty,
} from './contracts.js';

export class ApiError extends Error {
    constructor(message, status = 0, code = 'NETWORK_ERROR', requestId = '') {
        super(message);
        Object.assign(this, { status, code, requestId });
    }
}
export class ApiClient {
    constructor({
        base,
        transport = globalThis.fetch.bind(globalThis),
        onExpired = () => {},
        timeout = 30000,
    }) {
        this.base = normalizeBase(base);
        this.transport = transport;
        this.onExpired = onExpired;
        this.timeout = timeout;
        this.session = null;
        this.controllers = new Set();
        this.disposed = false;
        this.refreshing = null;
    }
    setSession(session) {
        this.session = {
            ...validateSession(session),
            expiresAt: Date.now() + session.expiresIn * 1000,
        };
    }
    dispose() {
        this.disposed = true;
        this.session = null;
        for (const controller of this.controllers) controller.abort();
    }
    async ensureFresh() {
        if (!this.session) throw new ApiError('请先登录管理员账号。', 401, 'SESSION_EXPIRED');
        if (this.session.expiresAt - Date.now() > 45000) return;
        if (!this.refreshing) {
            const refreshToken = this.session.refreshToken;
            this.refreshing = this.request('/auth/refresh', {
                method: 'POST',
                body: { refreshToken },
                validate: validateSession,
            })
                .then((value) => {
                    if (!this.disposed && this.session?.refreshToken === refreshToken)
                        this.setSession(value);
                })
                .finally(() => {
                    this.refreshing = null;
                });
        }
        return this.refreshing;
    }
    async request(
        path,
        {
            method = 'GET',
            body,
            privateRequest = false,
            expected = 200,
            validate = (value) => value,
            timeout = this.timeout,
        } = {}
    ) {
        if (this.disposed) throw new DOMException('请求已取消', 'AbortError');
        if (privateRequest) await this.ensureFresh();
        if (this.disposed) throw new DOMException('请求已取消', 'AbortError');
        const controller = new AbortController();
        this.controllers.add(controller);
        let timedOut = false;
        let receivedStatus = 0,
            failure;
        const timer = setTimeout(() => {
            timedOut = true;
            controller.abort();
        }, timeout);
        const multipart = body instanceof FormData;
        const headers = { Accept: 'application/json' };
        if (body !== undefined && !multipart) headers['Content-Type'] = 'application/json';
        if (privateRequest) headers.Authorization = `Bearer ${this.session.accessToken}`;
        try {
            const response = await this.transport(`${this.base}${path}`, {
                method,
                headers,
                body: body === undefined ? undefined : multipart ? body : JSON.stringify(body),
                signal: controller.signal,
                redirect: 'error',
                credentials: 'omit',
                cache: 'no-store',
            });
            receivedStatus = response.status;
            if (!response.headers.get('content-type')?.toLowerCase().includes('application/json'))
                throw new ApiError(
                    '服务端没有返回 JSON；检查 API 地址及网关配置。',
                    response.status,
                    'CONTRACT_ERROR'
                );
            if (Number(response.headers.get('content-length')) > MAX_JSON)
                throw new ApiError('API 响应超过 4 MiB。', response.status, 'CONTRACT_ERROR');
            const reader = response.body.getReader();
            const chunks = [];
            let length = 0;
            try {
                while (true) {
                    const { value, done } = await reader.read();
                    if (done) break;
                    length += value.byteLength;
                    if (length > MAX_JSON) {
                        await reader.cancel();
                        throw new ApiError(
                            'API 响应超过 4 MiB。',
                            response.status,
                            'CONTRACT_ERROR'
                        );
                    }
                    chunks.push(value);
                }
            } finally {
                reader.releaseLock();
            }
            const bytes = new Uint8Array(length);
            let offset = 0;
            for (const chunk of chunks) {
                bytes.set(chunk, offset);
                offset += chunk.length;
            }
            let envelope;
            try {
                envelope = validateEnvelope(
                    JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes))
                );
            } catch (error) {
                throw new ApiError(error.message, response.status, 'CONTRACT_ERROR');
            }
            if (!response.ok) {
                if (envelope.code === 'OK' || envelope.data !== null)
                    throw new ApiError(
                        '错误响应须保留实际 HTTP 状态、错误 code 和 data:null。',
                        response.status,
                        'CONTRACT_ERROR',
                        envelope.requestId
                    );
                throw new ApiError(
                    envelope.message || '请求未完成。',
                    response.status,
                    envelope.code,
                    envelope.requestId
                );
            }
            if (response.status !== expected || envelope.code !== 'OK')
                throw new ApiError(
                    `预期 HTTP ${expected} / code=OK，实际 ${response.status} / ${envelope.code}。`,
                    response.status,
                    'CONTRACT_ERROR',
                    envelope.requestId
                );
            try {
                return validate(envelope.data);
            } catch (error) {
                throw new ApiError(
                    error.message,
                    response.status,
                    'CONTRACT_ERROR',
                    envelope.requestId
                );
            }
        } catch (error) {
            if (error instanceof ApiError) {
                failure = error;
                throw error;
            }
            if (controller.signal.aborted && !timedOut)
                throw new DOMException('请求已取消', 'AbortError');
            throw new ApiError(
                timedOut
                    ? '请求超时；检查服务端状态后重试。不要重复提交尚未确认结果的创建或上传。'
                    : '无法连接 API。检查后端是否启动、地址、CORS 和网络；不会自动切回模拟模式。',
                0,
                timedOut ? 'TIMEOUT' : 'NETWORK_ERROR'
            );
        } finally {
            clearTimeout(timer);
            this.controllers.delete(controller);
            // Parse the error first so expiry retains the backend code/requestId.
            // A disposed old connection must never log out a newer connection.
            if (
                !this.disposed &&
                receivedStatus === 401 &&
                (privateRequest || path === '/auth/refresh')
            ) {
                this.session = null;
                this.onExpired(failure);
            }
        }
    }
    async login(username, password) {
        const session = await this.request('/auth/login', {
            method: 'POST',
            body: { username, password },
            validate: validateSession,
        });
        this.setSession(session);
        return session;
    }
    categories() {
        return this.request('/categories', { validate: validateCategories });
    }
    async list(page = 1, pageSize = 12) {
        if (
            !Number.isInteger(page) ||
            page < 1 ||
            !Number.isInteger(pageSize) ||
            pageSize < 1 ||
            pageSize > 50
        )
            throw new Error('分页参数不合法。');
        const value = await this.request(`/admin/wallpapers?page=${page}&pageSize=${pageSize}`, {
            privateRequest: true,
            validate: validatePage,
        });
        if (value.page !== page || value.pageSize !== pageSize)
            throw new ApiError('服务端分页参数与请求不一致。', 200, 'CONTRACT_ERROR');
        return value;
    }
    upload(file, purpose) {
        validateFile(file, purpose);
        const form = new FormData();
        form.append('file', file);
        form.append('purpose', purpose);
        return this.request('/admin/uploads', {
            method: 'POST',
            body: form,
            privateRequest: true,
            expected: 201,
            timeout: 120000,
            validate: (value) => {
                validateAsset(value);
                if (value.purpose !== purpose) throw new Error('上传返回的 purpose 与请求不一致。');
                return value;
            },
        });
    }
    create(body) {
        validateMutation(body, true);
        return this.request('/admin/wallpapers', {
            method: 'POST',
            body,
            privateRequest: true,
            expected: 201,
            validate: (value) => {
                validateProduct(value);
                if (
                    value.publicationStatus !== 'DRAFT' ||
                    value.originalAssetId !== body.originalAssetId ||
                    value.coverAssetId !== body.coverAssetId
                )
                    throw new Error('创建须返回 DRAFT，且资产编号须与提交的一致。');
                return value;
            },
        });
    }
    update(id, body) {
        validateMutation(body);
        return this.request(`/admin/wallpapers/${this.pathId(id)}`, {
            method: 'PATCH',
            body,
            privateRequest: true,
            validate: (value) => {
                validateProduct(value);
                if (value.id !== id) throw new Error('编辑返回了其他商品。');
                return value;
            },
        });
    }
    publish(id, status) {
        if (!['PUBLISHED', 'WITHDRAWN'].includes(status)) throw new Error('不合法的上下架状态。');
        return this.request(`/admin/wallpapers/${this.pathId(id)}/publication`, {
            method: 'PUT',
            body: { status },
            privateRequest: true,
            validate: (value) => {
                validateProduct(value);
                if (value.id !== id || value.publicationStatus !== status)
                    throw new Error('上下架返回的商品或状态与请求不一致。');
                return value;
            },
        });
    }
    pathId(id) {
        if (!/^[A-Za-z0-9._-]{1,128}$/.test(id)) throw new Error('不合法的商品 ID。');
        return encodeURIComponent(id);
    }
    async logout() {
        const refreshToken = this.session?.refreshToken;
        this.session = null;
        if (refreshToken)
            await this.request('/auth/logout', {
                method: 'POST',
                body: { refreshToken },
                validate: validateEmpty,
            });
    }
}
