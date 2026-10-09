export const DEFAULT_BASE = 'http://127.0.0.1:8080/api/v1';
export const MAX_JSON = 4 * 1024 * 1024;
export const MAX_COVER = 4 * 1024 * 1024;
export const MAX_ORIGINAL = 10 * 1024 * 1024 * 1024;
export const MUTATION_FIELDS = [
    'title',
    'description',
    'categoryId',
    'priceCents',
    'originalAssetId',
    'coverAssetId',
    'creator',
];
export const STATUSES = { DRAFT: '草稿', PUBLISHED: '已上架', WITHDRAWN: '已下架' };
export const TYPES = { IMAGE: '图片', GIF: '动图', VIDEO: '视频' };
const allowedExtensions = ['jpg', 'jpeg', 'png', 'bmp', 'webp', 'gif', 'mp4', 'webm', 'mov', 'avi'];
const assert = (condition, label) => {
    if (!condition) throw new Error(`接口契约不匹配：${label}`);
};
const object = (value) => value !== null && typeof value === 'object' && !Array.isArray(value);
const string = (value, min = 0, max = Infinity) =>
    typeof value === 'string' && [...value].length >= min && [...value].length <= max;
const integer = (value, min = 0, max = Infinity) =>
    Number.isSafeInteger(value) && value >= min && value <= max;
const id = (value) => string(value, 1, 128) && /^[A-Za-z0-9._-]+$/.test(value);
export function normalizeBase(value) {
    const url = new URL(value.trim());
    const local = ['127.0.0.1', 'localhost', '[::1]'].includes(url.hostname);
    if (
        (url.protocol !== 'https:' && !(url.protocol === 'http:' && local)) ||
        url.username ||
        url.password ||
        url.search ||
        url.hash ||
        url.pathname.replace(/\/$/, '') !== '/api/v1'
    ) {
        throw new Error(
            'API 地址须为 HTTPS（本机允许 HTTP），路径为 /api/v1；不含用户名、密码、查询或片段。'
        );
    }
    return `${url.origin}/api/v1`;
}
export function safeImageUrl(value) {
    try {
        const url = new URL(value);
        return !url.username &&
            !url.password &&
            (url.protocol === 'https:' ||
                (url.protocol === 'http:' &&
                    ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname)))
            ? url.href
            : '';
    } catch {
        return '';
    }
}
export function parsePrice(value) {
    if (!/^(?:0|[1-9]\d{0,6})(?:\.\d{1,2})?$/.test(value))
        throw new Error('价格须为非负金额，最多两位小数，不接受科学计数法。');
    const [whole, decimals = ''] = value.split('.');
    const cents = Number(whole) * 100 + Number(decimals.padEnd(2, '0'));
    if (cents > 100000000) throw new Error('价格不得超过 ¥1,000,000.00。');
    return cents;
}
export function formatPrice(cents) {
    return `¥${(cents / 100).toFixed(2)}`;
}
export function formatSize(bytes) {
    const n = Number(bytes);
    if (!Number.isFinite(n)) return '—';
    return n >= 1024 ** 3
        ? `${(n / 1024 ** 3).toFixed(2)} GiB`
        : n >= 1024 ** 2
          ? `${(n / 1024 ** 2).toFixed(1)} MiB`
          : `${(n / 1024).toFixed(1)} KiB`;
}
export function validateFile(file, purpose) {
    if (!['COVER', 'ORIGINAL'].includes(purpose))
        throw new Error('purpose 必须为 COVER 或 ORIGINAL。');
    if (!file || file.size <= 0) throw new Error('请选择非空文件。');
    const extension = file.name.split('.').pop().toLowerCase();
    if (
        !allowedExtensions.includes(extension) ||
        (purpose === 'COVER' && !allowedExtensions.slice(0, 6).includes(extension))
    )
        throw new Error('文件扩展名不受支持；封面仅支持图片。');
    if (file.size > (purpose === 'COVER' ? MAX_COVER : MAX_ORIGINAL))
        throw new Error(purpose === 'COVER' ? '封面不能超过 4 MiB。' : '原文件不能超过 10 GiB。');
    return extension;
}
export function validateEnvelope(value) {
    assert(
        object(value) &&
            string(value.code, 1) &&
            string(value.message) &&
            string(value.requestId) &&
            Object.hasOwn(value, 'data'),
        '响应须包含 code、message、data、requestId'
    );
    return value;
}
export function validateSession(value) {
    assert(
        object(value) &&
            string(value.accessToken, 1) &&
            string(value.refreshToken, 1) &&
            integer(value.expiresIn, 60, 86400),
        'Session'
    );
    assert(
        object(value.user) &&
            string(value.user.id, 1) &&
            string(value.user.username) &&
            string(value.user.displayName),
        'Session.user'
    );
    return value;
}
export function validateCategories(value) {
    assert(
        Array.isArray(value) &&
            value.every((item) => object(item) && string(item.id) && string(item.name)),
        'Category[]'
    );
    return value;
}
export function validateEmpty(value) {
    assert(object(value), 'Empty.data 须为对象');
    return value;
}
export function validateProduct(value) {
    assert(
        object(value) &&
            id(value.id) &&
            string(value.title, 1, 120) &&
            string(value.description, 0, 4000),
        'Product 标题/ID/描述'
    );
    assert(
        string(value.categoryId) &&
            string(value.categoryName) &&
            Object.hasOwn(TYPES, value.type) &&
            integer(value.priceCents, 0, 100000000) &&
            value.currency === 'CNY',
        'Product 分类/类型/金额'
    );
    assert(
        string(value.coverUrl) && !!safeImageUrl(value.coverUrl),
        'coverUrl 须为最终 HTTPS / 本机 HTTP URL'
    );
    assert(
        integer(value.width, 1) &&
            integer(value.height, 1) &&
            typeof value.fps === 'number' &&
            Number.isFinite(value.fps) &&
            value.fps >= 0,
        'Product 媒体元数据'
    );
    assert(
        string(value.resourceVersion, 1) &&
            /^[1-9][0-9]*$/.test(value.sizeBytes) &&
            string(value.sizeBytes) &&
            string(value.creator),
        'Product 版本/大小/作者'
    );
    assert(
        Object.hasOwn(STATUSES, value.publicationStatus) &&
            string(value.originalAssetId) &&
            string(value.coverAssetId),
        'AdminProduct 资产/发布状态'
    );
    return value;
}
export function validatePage(value) {
    assert(
        object(value) &&
            Array.isArray(value.items) &&
            integer(value.page, 1) &&
            integer(value.pageSize, 1, 50) &&
            integer(value.total),
        '商品分页'
    );
    assert(
        value.items.length <= value.pageSize && value.items.length <= value.total,
        '商品分页数量'
    );
    value.items.forEach(validateProduct);
    return value;
}
export function validateAsset(value) {
    assert(
        object(value) &&
            string(value.id, 1) &&
            ['COVER', 'ORIGINAL'].includes(value.purpose) &&
            ['PROCESSING', 'READY', 'FAILED'].includes(value.status),
        'Asset id/purpose/status（上传响应不是 assetId）'
    );
    assert(
        string(value.mimeType) &&
            string(value.fileExtension) &&
            string(value.sizeBytes) &&
            /^[1-9][0-9]*$/.test(value.sizeBytes) &&
            string(value.sha256) &&
            /^[0-9a-fA-F]{64}$/.test(value.sha256),
        'Asset 文件元数据'
    );
    assert(
        integer(value.width) &&
            integer(value.height) &&
            typeof value.fps === 'number' &&
            Number.isFinite(value.fps) &&
            value.fps >= 0,
        'Asset 媒体元数据'
    );
    return value;
}
export function validateMutation(value, create = false) {
    if (
        !object(value) ||
        !Object.keys(value).length ||
        Object.keys(value).some((key) => !MUTATION_FIELDS.includes(key))
    )
        throw new Error('商品请求为空或包含契约外字段。');
    if (
        create &&
        ['title', 'categoryId', 'priceCents', 'originalAssetId', 'coverAssetId'].some(
            (key) => !Object.hasOwn(value, key)
        )
    )
        throw new Error('创建商品缺少必填字段。');
    for (const [key, field] of Object.entries(value)) {
        if (key === 'priceCents') {
            if (!integer(field, 0, 100000000)) throw new Error('priceCents 须为合法整数分。');
        } else if (
            !string(
                field,
                key === 'title' ? 1 : 0,
                key === 'title' || key === 'creator' ? 120 : key === 'description' ? 4000 : Infinity
            )
        )
            throw new Error(`${key} 不符合字段约束。`);
    }
    return value;
}
