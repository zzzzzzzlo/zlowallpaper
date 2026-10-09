const dockRoot = document.querySelector('#dock-root');
const dock = document.querySelector('#dock');
const appsElement = document.querySelector('#apps');
const emptyActions = document.querySelector('#empty-actions');
const addButton = document.querySelector('#add-button');
const hideButton = document.querySelector('#hide-button');
const desktopIconsButton = document.querySelector('#desktop-icons-button');
const restoreButton = document.querySelector('#restore-button');

let state = {
  apps: [],
  scale: 1,
  desktopIconButtonEnabled: false,
  desktopIconsHidden: false,
  hideButtonEnabled: false,
  orientation: 'horizontal',
};
let animationFrame = null;
let transitionTimer = null;
let expansionFallbackTimer = null;
let expansionWaiting = false;
let collapsed = false;
let hasRendered = false;
const DOCK_TRANSITION_TIMEOUT_MS = 300;

function displayInitial(name) {
  const trimmed = name.trim();
  return trimmed ? trimmed[0].toLocaleUpperCase() : '?';
}

function makeApplicationButton(application) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'dock-item';
  button.dataset.id = application.id;
  button.title = `${application.name}\n${application.path}`;
  button.setAttribute('aria-label', `启动 ${application.name}`);

  const motion = document.createElement('span');
  motion.className = 'icon-motion';

  if (application.icon) {
    const icon = document.createElement('img');
    icon.className = 'app-icon';
    icon.src = application.icon;
    icon.alt = '';
    motion.append(icon);

    const reflection = document.createElement('img');
    reflection.className = 'reflection';
    reflection.src = application.icon;
    reflection.alt = '';
    const reflectionMask = document.createElement('span');
    reflectionMask.className = 'reflection-mask';
    reflectionMask.append(reflection);
    motion.append(reflectionMask);
  } else {
    const fallback = document.createElement('span');
    fallback.className = 'fallback-icon';
    fallback.textContent = displayInitial(application.name);
    motion.append(fallback);
  }

  button.append(motion);
  button.addEventListener('click', () => window.icontra.launch(application.id));
  return button;
}

function visibleDockItems() {
  return dock.querySelectorAll('.dock-item:not([hidden])');
}

function resizeWindow(compact = collapsed) {
  window.icontra.setContentSize(
    window.IcontraDockLayout.dockContentSize(state, compact),
  );
}

function clearCollapseOffsets() {
  for (const item of dock.querySelectorAll('.dock-item')) {
    item.style.removeProperty('--collapse-x');
    item.style.removeProperty('--collapse-y');
  }
}

function prepareCollapseOffsets() {
  const scale = state.scale || 1;
  const bounds = dock.getBoundingClientRect();
  const centerX = bounds.left + bounds.width / 2;
  const centerY = bounds.top + bounds.height / 2;
  for (const item of visibleDockItems()) {
    const itemBounds = item.getBoundingClientRect();
    const itemCenterX = itemBounds.left + itemBounds.width / 2;
    const itemCenterY = itemBounds.top + itemBounds.height / 2;
    item.style.setProperty(
      '--collapse-x',
      `${(centerX - itemCenterX) / scale}px`,
    );
    item.style.setProperty(
      '--collapse-y',
      `${(centerY - itemCenterY) / scale}px`,
    );
  }
}

function cancelTransitionTimers() {
  clearTimeout(transitionTimer);
  clearTimeout(expansionFallbackTimer);
  transitionTimer = null;
  expansionFallbackTimer = null;
}

function collapseDock() {
  if (
    collapsed ||
    expansionWaiting ||
    dockRoot.classList.contains('is-collapsing') ||
    dockRoot.classList.contains('is-expansion-animating') ||
    !window.IcontraDockLayout.shouldShowHideButton(state)
  ) {
    return;
  }

  cancelTransitionTimers();
  resetMagnification();
  dockRoot.classList.add('collapse-preparing');
  void dock.offsetWidth;
  prepareCollapseOffsets();
  dockRoot.classList.remove('collapse-preparing');
  dockRoot.classList.add('is-collapsing');
  hideButton.disabled = true;

  transitionTimer = setTimeout(() => {
    transitionTimer = null;
    collapsed = true;
    dockRoot.classList.remove('is-collapsing');
    dockRoot.classList.add('is-collapsed');
    hideButton.disabled = false;
    resizeWindow(true);
  }, DOCK_TRANSITION_TIMEOUT_MS);
}

function finishExpansion() {
  if (!expansionWaiting) return;
  expansionWaiting = false;
  clearTimeout(expansionFallbackTimer);
  expansionFallbackTimer = null;
  window.removeEventListener('resize', finishExpansion);

  dockRoot.classList.add('expansion-measuring');
  dockRoot.classList.remove('is-collapsed');
  clearCollapseOffsets();
  void dock.offsetWidth;
  prepareCollapseOffsets();
  dockRoot.classList.remove('expansion-measuring');
  dockRoot.classList.add('is-expanding');
  void dock.offsetWidth;

  requestAnimationFrame(() => {
    requestAnimationFrame(() => {
      dockRoot.classList.remove('is-expanding');
      dockRoot.classList.add('is-expansion-animating');
      transitionTimer = setTimeout(() => {
        transitionTimer = null;
        dockRoot.classList.remove('is-expansion-animating');
        clearCollapseOffsets();
      }, DOCK_TRANSITION_TIMEOUT_MS);
    });
  });
}

function expandDock() {
  if (!collapsed || expansionWaiting) return;
  cancelTransitionTimers();
  collapsed = false;
  expansionWaiting = true;
  window.addEventListener('resize', finishExpansion);
  resizeWindow(false);
  expansionFallbackTimer = setTimeout(finishExpansion, 140);
}

function forceExpanded() {
  cancelTransitionTimers();
  expansionWaiting = false;
  collapsed = false;
  window.removeEventListener('resize', finishExpansion);
  dockRoot.classList.remove(
    'collapse-preparing',
    'is-collapsing',
    'is-collapsed',
    'expansion-measuring',
    'is-expanding',
    'is-expansion-animating',
  );
  hideButton.disabled = false;
  clearCollapseOffsets();
  requestAnimationFrame(() => resizeWindow(false));
}

function render(nextState) {
  const normalizedState = {
    ...nextState,
    orientation: nextState.orientation === 'vertical' ? 'vertical' : 'horizontal',
  };
  const changes = window.IcontraDockLayout.layoutChanges(
    hasRendered ? state : null,
    normalizedState,
  );
  state = normalizedState;

  if (changes.applications) {
    appsElement.replaceChildren(...state.apps.map(makeApplicationButton));
    emptyActions.hidden =
      !window.IcontraDockLayout.shouldShowEmptyAddButton(state.apps);
  }

  if (changes.applications || changes.hideButton) {
    hideButton.hidden =
      !window.IcontraDockLayout.shouldShowHideButton(state);
  }
  if (changes.applications || changes.desktopIconButton) {
    desktopIconsButton.hidden =
      !window.IcontraDockLayout.shouldShowDesktopIconButton(state);
  }
  desktopIconsButton.dataset.iconsHidden = String(Boolean(state.desktopIconsHidden));
  const desktopIconsAreHidden = Boolean(state.desktopIconsHidden);
  desktopIconsButton.setAttribute(
    'aria-label',
    desktopIconsAreHidden ? '显示桌面图标' : '隐藏桌面图标',
  );
  desktopIconsButton.title = desktopIconsAreHidden
    ? '显示桌面图标'
    : '隐藏桌面图标';

  if (changes.orientation) {
    dockRoot.dataset.orientation = state.orientation;
  }
  if (changes.scale) {
    dock.style.setProperty('--overall-scale', String(state.scale || 1));
  }

  const layoutChanged =
    changes.applications ||
    changes.scale ||
    changes.hideButton ||
    changes.desktopIconButton ||
    changes.orientation;
  const hideControlAvailable =
    window.IcontraDockLayout.shouldShowHideButton(state);
  if (!hideControlAvailable && (collapsed || dockRoot.classList.contains('is-collapsing'))) {
    forceExpanded();
  } else if (layoutChanged && !collapsed && !expansionWaiting) {
    resetMagnification();
    requestAnimationFrame(() => resizeWindow(false));
  }
  hasRendered = true;
}

function updateMagnification(clientX, clientY) {
  animationFrame = null;
  if (
    collapsed ||
    expansionWaiting ||
    dockRoot.classList.contains('is-collapsing') ||
    dockRoot.classList.contains('is-expanding') ||
    dockRoot.classList.contains('is-expansion-animating')
  ) {
    return;
  }

  const scale = state.scale || 1;
  const influenceRadius = 145 * scale;
  const pointerPosition =
    state.orientation === 'vertical' ? clientY : clientX;

  for (const button of visibleDockItems()) {
    const bounds = button.getBoundingClientRect();
    const center =
      state.orientation === 'vertical'
        ? bounds.top + bounds.height / 2
        : bounds.left + bounds.width / 2;
    const distance = Math.abs(pointerPosition - center);
    const influence = Math.max(0, 1 - distance / influenceRadius);
    const eased = influence * influence * (3 - 2 * influence);
    button.style.setProperty('--dock-scale', (1 + eased * 0.28).toFixed(3));
    button.style.setProperty('--dock-lift', '0px');
    button.style.setProperty('--dock-z', Math.round(eased * 10));
  }
}

function resetMagnification() {
  if (animationFrame !== null) {
    cancelAnimationFrame(animationFrame);
    animationFrame = null;
  }
  for (const button of dock.querySelectorAll('.dock-item')) {
    button.style.setProperty('--dock-scale', '1');
    button.style.setProperty('--dock-lift', '0px');
    button.style.setProperty('--dock-z', '0');
  }
}

dock.addEventListener('pointermove', (event) => {
  if (animationFrame !== null) cancelAnimationFrame(animationFrame);
  animationFrame = requestAnimationFrame(() =>
    updateMagnification(event.clientX, event.clientY),
  );
});
dock.addEventListener('pointerleave', resetMagnification);
dock.addEventListener('pointercancel', resetMagnification);

window.addEventListener('blur', resetMagnification);
document.addEventListener('mouseleave', resetMagnification);
document.addEventListener('visibilitychange', () => {
  if (document.hidden) resetMagnification();
});

addButton.addEventListener('click', () => window.icontra.addApplications());
hideButton.addEventListener('click', collapseDock);
desktopIconsButton.addEventListener('click', async () => {
  if (desktopIconsButton.disabled) return;
  desktopIconsButton.disabled = true;
  try {
    await window.icontra.toggleDesktopIcons();
  } finally {
    desktopIconsButton.disabled = false;
  }
});
restoreButton.addEventListener('click', expandDock);

window.icontra.onResetMagnification(resetMagnification);
window.icontra.onStateChanged(render);
window.icontra.getState().then(render);
