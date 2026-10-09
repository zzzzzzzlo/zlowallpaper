import { ApiClient, ApiError } from './api.js';
import { MockApi } from './mock-api.js';
import {
    DEFAULT_BASE,
    STATUSES,
    TYPES,
    normalizeBase,
    safeImageUrl,
    parsePrice,
    formatPrice,
    formatSize,
    validateEmpty,
} from './contracts.js';

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];
const mock = new MockApi({ origin: location.origin });
let preferences = { mode: 'mock', base: DEFAULT_BASE };
try {
    const stored = JSON.parse(localStorage.getItem('zlo.admin.preferences') || 'null');
    if (stored && ['mock', 'real'].includes(stored.mode))
        preferences = { mode: stored.mode, base: normalizeBase(stored.base) };
} catch {
    /* Storage is optional; credentials are never stored. */
}
let api;
let loggedIn = false;
let page = 1,
    pageSize = 12,
    pageData = null,
    categories = [];
let listBusy = false,
    loginBusy = false,
    mutationBusy = false,
    canManage = false;
let listRevision = 0,
    editorRevision = 0,
    editor = null;
let toastTimer;
let confirmJob = null;

function makeApi() {
    api = new ApiClient({
        base: preferences.base,
        transport: preferences.mode === 'mock' ? mock.fetch : fetch.bind(window),
        onExpired: sessionExpired,
    });
}
function node(tag, text, className) {
    const element = document.createElement(tag);
    if (text !== undefined) element.textContent = text;
    if (className) element.className = className;
    return element;
}
function describe(error) {
    const prefix = error.status === 403 ? '权限不足：' : '';
    return prefix + error.message;
}
function errorBox(element, error) {
    element.replaceChildren(node('span', describe(error)));
    if (error.requestId)
        element.append(
            node('small', `HTTP ${error.status} · ${error.code} · requestId: ${error.requestId}`)
        );
    else if (error.code) element.append(node('small', error.code));
    element.classList.toggle('permission-box', error.status === 403);
    element.hidden = false;
}
function toast(message, error = false) {
    clearTimeout(toastTimer);
    $('#toast').textContent = message;
    $('#toast').classList.toggle('error', error);
    $('#toast').hidden = false;
    toastTimer = setTimeout(() => {
        $('#toast').hidden = true;
    }, 6500);
}
function closeDialogs() {
    for (const dialog of $$('dialog[open]')) dialog.close();
    releasePreview();
    editor = null;
    confirmJob = null;
    editorRevision++;
}
function exitToLogin(message = '', detail = null) {
    api.dispose();
    closeDialogs();
    loggedIn = false;
    listBusy = false;
    mutationBusy = false;
    canManage = false;
    pageData = null;
    categories = [];
    listRevision++;
    $('#workspace').hidden = true;
    $('#login-view').hidden = false;
    $('#password').value = '';
    $('#login-submit').disabled = false;
    $('#login-submit').textContent = '登录工作台 →';
    loginBusy = false;
    $('.skip-link').href = '#main';
    makeApi();
    updateMode();
    $('#login-error').hidden = !message;
    if (message)
        errorBox($('#login-error'), detail || new ApiError(message, 401, 'SESSION_EXPIRED'));
}
function sessionExpired(error) {
    if (preferences.mode === 'mock' && mock.scenario === 'expired') {
        mock.scenario = 'normal';
        $('#mock-scenario').value = 'normal';
    }
    const message = error
        ? `${error.message} 旧请求已取消。`
        : '会话已过期，旧请求已取消。请重新登录。';
    exitToLogin(message, error ? new ApiError(message, 401, error.code, error.requestId) : null);
}
function updateMode() {
    document.body.classList.toggle('real-mode', preferences.mode === 'real');
    $$('[data-mode-label]').forEach((element) => {
        element.textContent = preferences.mode === 'mock' ? '模拟数据' : '真实接口';
    });
    $('#login-mode').replaceChildren();
    if (preferences.mode === 'mock') {
        $('#login-mode').append(
            node('strong', '当前为模拟模式'),
            node('br'),
            node('span', '管理员：admin / Admin12345'),
            node('br'),
            node('span', '权限验证：viewer / Viewer12345（无管理权限）')
        );
    } else {
        $('#login-mode').append(
            node('strong', '当前为真实接口模式'),
            node('br'),
            node('code', preferences.base),
            node('br'),
            node('span', '后端未启动时会显示连接错误；不会使用模拟账号。')
        );
    }
    $('#mode-banner').textContent =
        preferences.mode === 'mock'
            ? '模拟模式 · 数据与上传文件只在当前页面演练，刷新即重置，不写入业务后端。'
            : `真实接口 · ${preferences.base} · 所有更改将写入服务端。`;
    $('#mock-tools').hidden = preferences.mode !== 'mock';
}
function updateControls() {
    $('.catalog-panel').setAttribute('aria-busy', String(listBusy));
    const blocked = !canManage || listBusy || mutationBusy;
    $$('[data-create]').forEach((button) => {
        button.disabled = blocked || !categories.length;
        button.title = !categories.length ? '分类加载成功后可创建商品；点击刷新列表重试。' : '';
    });
    $('#refresh-list').disabled = listBusy || mutationBusy;
    $('#page-size').disabled = listBusy || mutationBusy;
    $('#previous').disabled = listBusy || mutationBusy || !pageData || page <= 1;
    $('#next').disabled =
        listBusy || mutationBusy || !pageData || page * pageSize >= pageData.total;
    $$('.row-action').forEach((button) => {
        button.disabled = blocked;
    });
    $('#apply-scenario').disabled = listBusy || mutationBusy;
}
function setListState(kind, title, copy, retry = false, error) {
    $('#table-wrap').hidden = true;
    $('#list-state').hidden = false;
    $('#list-state').classList.toggle('loading', kind === 'loading');
    $('#list-state').replaceChildren(
        node(
            'span',
            kind === 'loading' ? '◌' : kind === 'empty' ? '▧' : kind === 'forbidden' ? '⊘' : '↻',
            'state-symbol'
        ),
        node('h3', title),
        node('p', copy)
    );
    if (error?.requestId)
        $('#list-state').append(
            node('small', `HTTP ${error.status} · ${error.code} · ${error.requestId}`)
        );
    if (retry) {
        const button = node('button', '重新加载', 'button secondary');
        button.addEventListener('click', () => reloadList());
        $('#list-state').append(button);
    } else if (kind === 'empty' && categories.length && canManage) {
        const button = node('button', '＋ 创建第一个商品', 'button primary');
        button.addEventListener('click', () => openEditor());
        $('#list-state').append(button);
    }
}
function renderPage() {
    $('#total').textContent = String(pageData.total);
    const totalPages = Math.max(1, Math.ceil(pageData.total / pageSize));
    $('#page-number').textContent = `${page} / ${totalPages}`;
    $('#page-description').textContent = pageData.total
        ? `共 ${pageData.total} 件商品 · 本页 ${pageData.items.length} 件`
        : '还没有商品';
    if (!pageData.items.length) {
        setListState(
            'empty',
            pageData.total ? '这一页暂时没有商品' : '从一份壁纸开始',
            pageData.total
                ? '商品列表发生变化，请返回上一页或刷新目录。'
                : '上传封面与原文件，保存草稿，再确认上架。'
        );
        updateControls();
        return;
    }
    $('#list-state').hidden = true;
    $('#table-wrap').hidden = false;
    $('#product-rows').replaceChildren();
    for (const product of pageData.items) {
        const row = node('tr');
        const main = node('td');
        const cell = node('div', undefined, 'product-cell');
        const image = node('img');
        image.className = 'cover-thumb';
        image.alt = `${product.title} 封面`;
        image.loading = 'lazy';
        image.referrerPolicy = 'no-referrer';
        image.src =
            preferences.mode === 'mock' ? mock.preview(product) : safeImageUrl(product.coverUrl);
        image.addEventListener(
            'error',
            () => {
                const fallback = node('span', '▧', 'cover-thumb cover-fallback');
                fallback.setAttribute('aria-label', '封面暂不可用');
                image.replaceWith(fallback);
            },
            { once: true }
        );
        const text = node('div');
        text.append(
            node('div', product.title, 'product-name'),
            node(
                'div',
                `${TYPES[product.type]} · ${product.width} × ${product.height}${product.fps ? ` · ${product.fps} fps` : ''} · v${product.resourceVersion}`,
                'product-meta'
            ),
            node('div', `${product.id} / ${formatSize(product.sizeBytes)}`, 'product-id')
        );
        cell.append(image, text);
        main.append(cell);
        row.append(
            main,
            node('td', product.categoryName),
            node('td', formatPrice(product.priceCents), 'price')
        );
        const status = node('td');
        status.append(
            node(
                'span',
                STATUSES[product.publicationStatus],
                `status status-${product.publicationStatus.toLowerCase()}`
            )
        );
        row.append(status);
        const actions = node('td');
        const group = node('div', undefined, 'row-actions');
        const edit = node('button', '编辑', 'row-action');
        edit.setAttribute('aria-label', `编辑 ${product.title}`);
        edit.addEventListener('click', () => openEditor(product));
        const publish = node(
            'button',
            product.publicationStatus === 'PUBLISHED' ? '下架' : '上架',
            'row-action publication'
        );
        publish.setAttribute(
            'aria-label',
            `${product.publicationStatus === 'PUBLISHED' ? '下架' : '上架'} ${product.title}`
        );
        publish.addEventListener('click', () => confirmPublication(product));
        group.append(edit, publish);
        actions.append(group);
        row.append(actions);
        $('#product-rows').append(row);
    }
    updateControls();
}
async function loadCategories(client) {
    try {
        const result = await client.categories();
        if (client !== api || !loggedIn) return;
        categories = result;
        if (!categories.length)
            errorBox(
                $('#list-notice'),
                new Error('服务端没有启用的分类。现有商品仍可管理；创建商品需先由后端配置分类。')
            );
    } catch (error) {
        if (client !== api || error.name === 'AbortError') return;
        categories = [];
        errorBox($('#list-notice'), new Error(`分类加载失败，暂不可创建商品。${describe(error)}`));
    }
}
async function reloadList(targetPage = page, reloadCategories = true) {
    if (!loggedIn || listBusy || mutationBusy) return;
    const client = api,
        revision = ++listRevision;
    listBusy = true;
    pageData = null;
    $('#total').textContent = '—';
    $('#page-description').textContent = '正在加载';
    $('#list-notice').hidden = true;
    setListState('loading', '正在整理商品目录', '获取商品与发布状态…');
    updateControls();
    try {
        const result = await client.list(targetPage, pageSize);
        if (client !== api || revision !== listRevision) return;
        page = targetPage;
        pageData = result;
        canManage = true;
        if (reloadCategories) await loadCategories(client);
        if (client !== api || revision !== listRevision) return;
        renderPage();
    } catch (error) {
        if (client !== api || error.name === 'AbortError') return;
        canManage = error.status !== 403;
        $('#total').textContent = '—';
        $('#page-description').textContent = '列表未加载';
        setListState(
            error.status === 403 ? 'forbidden' : 'error',
            error.status === 403 ? '当前账号没有管理权限' : '商品目录暂不可用',
            describe(error),
            true,
            error
        );
    } finally {
        if (client === api && revision === listRevision) {
            listBusy = false;
            updateControls();
        }
    }
}

$('#login-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    if (loginBusy) return;
    loginBusy = true;
    const client = api;
    $('#login-error').hidden = true;
    $('#login-submit').disabled = true;
    $('#login-submit').textContent = '正在验证账号与管理权限…';
    const password = $('#password').value;
    $('#password').value = '';
    try {
        const session = await client.login($('#username').value, password);
        const result = await client.list(1, pageSize); // User has no role field; the protected endpoint is authoritative.
        if (client !== api) return;
        loggedIn = true;
        canManage = true;
        page = 1;
        pageData = result;
        $('#user-name').textContent = session.user.displayName || session.user.username;
        $('#workspace').hidden = false;
        $('#login-view').hidden = true;
        $('#list-notice').hidden = true;
        $('.skip-link').href = '#catalog-main';
        await loadCategories(client);
        if (client !== api) return;
        renderPage();
        $('#catalog-main').focus();
    } catch (error) {
        if (client !== api || error.name === 'AbortError') return;
        if (client.session) {
            try {
                await client.logout();
            } catch {
                /* Local credentials are already cleared. */
            }
        }
        if (client === api) errorBox($('#login-error'), error);
    } finally {
        if (client === api) {
            loginBusy = false;
            $('#login-submit').disabled = false;
            $('#login-submit').textContent = '登录工作台 →';
        }
    }
});

function releasePreview() {
    if (editor?.previewUrl) URL.revokeObjectURL(editor.previewUrl);
}
function dirty() {
    return !!editor && (editor.changed || Object.keys(editor.assets).length > 0);
}
function editorBusy() {
    return !!editor && (editor.saving || editor.uploading.size > 0);
}
function closeEditor(force = false) {
    if (!force && editorBusy()) {
        toast('正在上传或保存，请等待请求完成。', true);
        return false;
    }
    if (
        !force &&
        dirty() &&
        !window.confirm('放弃未保存的修改？已上传资产不会被删除；后端需按策略清理未引用资产。')
    )
        return false;
    $('#editor-dialog').close();
    releasePreview();
    editor = null;
    editorRevision++;
    return true;
}
function updateEditorControls() {
    if (!editor) return;
    const busy = editorBusy();
    const ready = ['COVER', 'ORIGINAL'].every((purpose) =>
        editor.assets[purpose]
            ? editor.assets[purpose].status === 'READY'
            : !!editor.product?.[purpose === 'COVER' ? 'coverAssetId' : 'originalAssetId']
    );
    $('#save-product').disabled = busy || !ready || !categories.length;
    $('#save-product').textContent = editor.saving
        ? '正在保存…'
        : editor.product
          ? '保存修改'
          : '创建商品';
    $('#close-editor').disabled = busy;
    $('#cancel-editor').disabled = busy;
    $('#cover-file').disabled = editor.saving || editor.uploading.has('COVER');
    $('#original-file').disabled = editor.saving || editor.uploading.has('ORIGINAL');
    $$(
        '#product-title, #product-category, #product-price, #product-creator, #product-description'
    ).forEach((element) => {
        element.disabled = editor.saving;
    });
    $('#save-hint').textContent = busy
        ? '请求进行中，请勿重复提交。'
        : !ready
          ? '两份素材须为 READY；处理中或失败不能保存。'
          : editor.product
            ? '未更换的素材继续使用原资产编号。'
            : '创建后为草稿，不会自动上架。';
}
function showPreview(url) {
    $('#cover-preview').hidden = !url;
    $('#preview-placeholder').hidden = !!url;
    if (url) {
        $('#cover-preview').src = url;
        $('#cover-preview').classList.remove('image-broken');
    } else $('#cover-preview').removeAttribute('src');
}
$('#cover-preview').addEventListener('error', () => {
    $('#cover-preview').hidden = true;
    $('#preview-placeholder').hidden = false;
});
function openEditor(product = null) {
    if (!canManage || listBusy || mutationBusy || !categories.length) return;
    editorRevision++;
    releasePreview();
    editor = {
        product,
        assets: {},
        uploading: new Set(),
        saving: false,
        changed: false,
        previewUrl: '',
    };
    $('#editor-form').reset();
    $('#editor-error').hidden = true;
    $('#editor-title').textContent = product ? '编辑商品' : '创建商品';
    $('#product-category').replaceChildren(node('option', '选择分类'));
    $('#product-category').firstElementChild.value = '';
    for (const category of categories) {
        const option = node('option', category.name);
        option.value = category.id;
        $('#product-category').append(option);
    }
    if (product && !categories.some((category) => category.id === product.categoryId)) {
        const option = node('option', `${product.categoryName}（当前分类未启用）`);
        option.value = product.categoryId;
        $('#product-category').append(option);
    }
    $('#product-title').value = product?.title || '';
    $('#product-category').value = product?.categoryId || '';
    $('#product-price').value = ((product?.priceCents || 0) / 100).toFixed(2);
    $('#product-creator').value = product?.creator || '';
    $('#product-description').value = product?.description || '';
    $('#editing-summary').hidden = !product;
    if (product)
        $('#editing-summary').textContent =
            `${product.id} · ${STATUSES[product.publicationStatus]} · 资源版本 v${product.resourceVersion}。保存不会自动更改发布状态。`;
    $('#cover-status').replaceChildren(node('span', product ? '保留现有封面' : '尚未上传'));
    $('#original-status').replaceChildren(
        node('span', product ? `保留现有原文件 · ${formatSize(product.sizeBytes)}` : '尚未上传')
    );
    if (product) {
        $('#cover-status').append(node('code', product.coverAssetId));
        $('#original-status').append(node('code', product.originalAssetId));
    }
    for (const element of [$('#cover-status'), $('#original-status')])
        element.className = 'asset-status';
    $('#upload-help').textContent =
        preferences.mode === 'mock'
            ? '模拟上传会计算 SHA-256，仅本页保留。为避免占用过多内存，模拟原文件限制 64 MiB；视频元数据为样例值。真实原文件上限为 10 GiB。'
            : '选择文件后立即上传。服务端检查通过后，使用 data.id 关联商品；不上传本机绝对路径。';
    showPreview(
        product
            ? preferences.mode === 'mock'
                ? mock.preview(product)
                : safeImageUrl(product.coverUrl)
            : ''
    );
    updateDescriptionCount();
    updateEditorControls();
    $('#editor-dialog').showModal();
    $('#product-title').focus();
}
function updateDescriptionCount() {
    $('#description-count').textContent =
        `${[...$('#product-description').value].length.toLocaleString('en-US')} / 4,000`;
}
$('#editor-form').addEventListener('input', (event) => {
    if (editor && event.target.type !== 'file') editor.changed = true;
    updateDescriptionCount();
});
for (const [inputId, purpose] of [
    ['cover-file', 'COVER'],
    ['original-file', 'ORIGINAL'],
]) {
    $(`#${inputId}`).addEventListener('change', async (event) => {
        const file = event.target.files[0];
        if (!file || !editor || editor.uploading.has(purpose)) return;
        const client = api,
            revision = editorRevision,
            current = editor;
        const status = $(purpose === 'COVER' ? '#cover-status' : '#original-status');
        current.uploading.add(purpose);
        status.className = 'asset-status';
        status.textContent = `正在上传 ${file.name}…`;
        $('#editor-error').hidden = true;
        updateEditorControls();
        // A failed replacement must not accidentally submit the old asset as if it succeeded.
        current.assets[purpose] = { status: 'FAILED' };
        try {
            const asset = await client.upload(file, purpose);
            if (client !== api || revision !== editorRevision || editor !== current) return;
            current.assets[purpose] = asset;
            status.replaceChildren(
                node(
                    'strong',
                    asset.status === 'READY'
                        ? '已就绪 · READY'
                        : asset.status === 'PROCESSING'
                          ? '服务端处理中 · PROCESSING'
                          : '服务端处理失败 · FAILED'
                ),
                node('code', asset.id),
                node(
                    'small',
                    `${formatSize(asset.sizeBytes)} · ${asset.width} × ${asset.height}${asset.fps ? ` · ${asset.fps} fps` : ''}`
                )
            );
            status.classList.toggle('processing', asset.status === 'PROCESSING');
            status.classList.toggle('failed', asset.status === 'FAILED');
            const digest = node('details');
            digest.append(node('summary', 'SHA-256'), node('code', asset.sha256));
            status.append(digest);
            if (asset.status !== 'READY')
                status.append(
                    node(
                        'small',
                        asset.status === 'PROCESSING'
                            ? '现有契约没有资产状态查询接口。请联系后端确认处理结果；当前不能保存，不会自动重复上传或假定就绪。'
                            : '重新选择文件上传，或联系后端查看 requestId 日志。'
                    )
                );
            if (purpose === 'COVER' && asset.status === 'READY') {
                releasePreview();
                current.previewUrl = URL.createObjectURL(file);
                showPreview(current.previewUrl);
            }
        } catch (error) {
            if (client !== api || revision !== editorRevision || error.name === 'AbortError')
                return;
            status.textContent = '上传未完成，请重新选择文件。';
            status.classList.add('failed');
            errorBox($('#editor-error'), error);
        } finally {
            if (client === api && revision === editorRevision && editor === current) {
                current.uploading.delete(purpose);
                event.target.value = '';
                updateEditorControls();
            }
        }
    });
}
$('#editor-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    if (!editor || editorBusy() || $('#save-product').disabled) return;
    const client = api,
        current = editor,
        revision = editorRevision;
    $('#editor-error').hidden = true;
    try {
        const product = current.product;
        const payload = {
            title: $('#product-title').value,
            description: $('#product-description').value,
            categoryId: $('#product-category').value,
            priceCents: parsePrice($('#product-price').value),
            creator: $('#product-creator').value,
            originalAssetId: current.assets.ORIGINAL?.id || product?.originalAssetId,
            coverAssetId: current.assets.COVER?.id || product?.coverAssetId,
        };
        if (!payload.title.trim()) throw new Error('商品标题不能只有空白。');
        if (!payload.categoryId || !payload.originalAssetId || !payload.coverAssetId)
            throw new Error('请补全分类与两份素材。');
        const changed = product
            ? Object.fromEntries(
                  Object.entries(payload).filter(([key, value]) => value !== product[key])
              )
            : payload;
        if (!Object.keys(changed).length) {
            toast('没有需要保存的修改。');
            return;
        }
        current.saving = true;
        updateEditorControls();
        if (product) await client.update(product.id, changed);
        else await client.create(changed);
        if (client !== api || revision !== editorRevision) return;
        closeEditor(true);
        toast(product ? '修改已保存。' : '商品已创建为草稿，可从目录确认上架。');
        await reloadList(product ? page : 1, false);
    } catch (error) {
        if (client === api && revision === editorRevision && error.name !== 'AbortError')
            errorBox($('#editor-error'), error);
    } finally {
        if (client === api && revision === editorRevision && editor === current) {
            current.saving = false;
            updateEditorControls();
        }
    }
});
$('#close-editor').addEventListener('click', () => closeEditor());
$('#cancel-editor').addEventListener('click', () => closeEditor());
$('#editor-dialog').addEventListener('cancel', (event) => {
    event.preventDefault();
    closeEditor();
});

function confirmPublication(product) {
    if (!canManage || listBusy || mutationBusy) return;
    const status = product.publicationStatus === 'PUBLISHED' ? 'WITHDRAWN' : 'PUBLISHED';
    const action = status === 'PUBLISHED' ? '上架' : '下架';
    confirmJob = { product, status };
    $('#confirm-title').textContent = `${action}商品？`;
    $('#confirm-copy').textContent =
        `「${product.title}」${status === 'PUBLISHED' ? '将出现在公开商城。服务端会校验资产是否完整且就绪。' : '将停止新增购买；既有购买权益与下载规则由服务端保留策略决定。'}`;
    $('#confirm-action').textContent = `确认${action}`;
    $('#confirm-action').disabled = false;
    $('#confirm-cancel').disabled = false;
    $('#confirm-error').hidden = true;
    $('#confirm-dialog').showModal();
}
$('#confirm-action').addEventListener('click', async () => {
    if (!confirmJob || mutationBusy) return;
    const client = api,
        job = confirmJob;
    mutationBusy = true;
    updateControls();
    $('#confirm-action').disabled = true;
    $('#confirm-cancel').disabled = true;
    $('#confirm-action').textContent = '正在更新…';
    $('#confirm-error').hidden = true;
    try {
        await client.publish(job.product.id, job.status);
        if (client !== api) return;
        $('#confirm-dialog').close();
        confirmJob = null;
        mutationBusy = false;
        toast(job.status === 'PUBLISHED' ? '商品已上架。' : '商品已下架。');
        await reloadList(page, false);
    } catch (error) {
        if (client === api && error.name !== 'AbortError') errorBox($('#confirm-error'), error);
    } finally {
        if (client === api) {
            mutationBusy = false;
            $('#confirm-action').disabled = false;
            $('#confirm-cancel').disabled = false;
            $('#confirm-action').textContent = job.status === 'PUBLISHED' ? '确认上架' : '确认下架';
            updateControls();
        }
    }
});
$('#confirm-cancel').addEventListener('click', () => {
    if (!mutationBusy) {
        $('#confirm-dialog').close();
        confirmJob = null;
    }
});
$('#confirm-dialog').addEventListener('cancel', (event) => {
    if (mutationBusy) event.preventDefault();
    else confirmJob = null;
});

$$('[data-create]').forEach((button) => button.addEventListener('click', () => openEditor()));
$('#refresh-list').addEventListener('click', () => reloadList());
$('#catalog-nav').addEventListener('click', () => $('#catalog-main').focus());
$('#previous').addEventListener('click', () => reloadList(page - 1));
$('#next').addEventListener('click', () => reloadList(page + 1));
$('#page-size').addEventListener('change', () => {
    pageSize = Number($('#page-size').value);
    reloadList(1, false);
});
$('#logout').addEventListener('click', async () => {
    if (editorBusy() || mutationBusy || loginBusy) {
        toast('正在执行操作，请稍后退出。', true);
        return;
    }
    if (editor && !closeEditor()) return;
    const client = api;
    const token = client.session?.refreshToken;
    // Abort outstanding list/category requests before changing the session.
    const logoutClient = new ApiClient({
        base: preferences.base,
        transport: preferences.mode === 'mock' ? mock.fetch : fetch.bind(window),
    });
    exitToLogin();
    toast('已退出本机会话。');
    if (token) {
        try {
            await logoutClient.request('/auth/logout', {
                method: 'POST',
                body: { refreshToken: token },
                validate: validateEmpty,
            });
        } catch (error) {
            toast(`本机已退出，但服务端注销未确认：${describe(error)}`, true);
        } finally {
            logoutClient.dispose();
        }
    }
});
$$('[data-config]').forEach((button) =>
    button.addEventListener('click', () => {
        if (editorBusy() || mutationBusy || loginBusy) {
            toast('正在执行操作，完成后再修改连接。', true);
            return;
        }
        if (editor && !closeEditor()) return;
        $('#api-base').value = preferences.base;
        $(`input[name=mode][value=${preferences.mode}]`).checked = true;
        $('#settings-error').hidden = true;
        $('#settings-dialog').showModal();
    })
);
$$('[data-close-settings]').forEach((button) =>
    button.addEventListener('click', () => $('#settings-dialog').close())
);
$('#settings-form').addEventListener('submit', async (event) => {
    event.preventDefault();
    try {
        const next = {
            base: normalizeBase($('#api-base').value),
            mode: $('input[name=mode]:checked').value,
        };
        if (next.base === preferences.base && next.mode === preferences.mode) {
            $('#settings-dialog').close();
            return;
        }
        if (loggedIn && !window.confirm('修改连接会退出当前账号并取消未完成的读取请求，是否继续？'))
            return;
        const old = {
            base: preferences.base,
            transport: preferences.mode === 'mock' ? mock.fetch : fetch.bind(window),
            token: api.session?.refreshToken,
        };
        preferences = next;
        try {
            localStorage.setItem('zlo.admin.preferences', JSON.stringify(preferences));
        } catch {
            /* Still usable without storage. */
        }
        mock.scenario = 'normal';
        $('#mock-scenario').value = 'normal';
        exitToLogin();
        toast('连接设置已保存，请重新登录。');
        if (old.token) {
            const logoutClient = new ApiClient(old);
            try {
                await logoutClient.request('/auth/logout', {
                    method: 'POST',
                    body: { refreshToken: old.token },
                    validate: validateEmpty,
                });
            } catch {
                toast('已切换连接；旧服务端注销未确认，旧会话需由服务端过期或撤销。', true);
            } finally {
                logoutClient.dispose();
            }
        }
    } catch (error) {
        errorBox($('#settings-error'), error);
    }
});
$('#apply-scenario').addEventListener('click', () => {
    mock.scenario = $('#mock-scenario').value;
    reloadList(1, false);
});
$('#mock-upload-status').addEventListener('change', () => {
    mock.uploadStatus = $('#mock-upload-status').value;
});
window.addEventListener('beforeunload', (event) => {
    if (dirty() || editorBusy() || mutationBusy) {
        event.preventDefault();
        event.returnValue = '';
    }
});
window.addEventListener('pagehide', () => {
    api.dispose();
    mock.dispose();
});
window.addEventListener('pageshow', (event) => {
    if (event.persisted) location.reload();
});
setInterval(async () => {
    if (!loggedIn || !api.session || api.session.expiresAt - Date.now() > 45000 || api.refreshing)
        return;
    const client = api;
    try {
        await client.ensureFresh();
    } catch (error) {
        if (client === api && error.name !== 'AbortError')
            toast(`会话刷新未完成：${describe(error)}`, true);
    }
}, 30000);
$('.table-wrap').prepend(node('p', '左右滑动查看价格、发布状态与操作 →', 'table-scroll-hint'));
makeApi();
updateMode();
