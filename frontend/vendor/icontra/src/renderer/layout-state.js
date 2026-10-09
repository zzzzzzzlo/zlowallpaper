(function attachLayoutState(root) {
  function applicationLayoutSignature(applications) {
    return applications
      .map((application) => {
        return [application.id, application.path, application.icon || ''].join(
          '\u0000',
        );
      })
      .join('\u0001');
  }

  function layoutChanges(previousState, nextState) {
    if (!previousState) {
      return {
        applications: true,
        scale: true,
        hideButton: true,
        desktopIconButton: true,
        orientation: true,
      };
    }
    return {
      applications:
        applicationLayoutSignature(previousState.apps) !==
        applicationLayoutSignature(nextState.apps),
      scale: previousState.scale !== nextState.scale,
      hideButton:
        previousState.hideButtonEnabled !== nextState.hideButtonEnabled,
      desktopIconButton:
        previousState.desktopIconButtonEnabled !==
        nextState.desktopIconButtonEnabled,
      orientation: previousState.orientation !== nextState.orientation,
    };
  }

  function shouldShowEmptyAddButton(applications) {
    return applications.length === 0;
  }

  function shouldShowHideButton(state) {
    return Boolean(state.hideButtonEnabled && state.apps.length > 0);
  }

  function shouldShowDesktopIconButton(state) {
    return Boolean(state.desktopIconButtonEnabled && state.apps.length > 0);
  }

  function dockContentSize(state, collapsed = false) {
    if (collapsed) {
      return {
        width: 64,
        height: 64,
        compact: true,
        orientation: state.orientation,
        preserveCenter: true,
      };
    }

    const scale = state.scale || 1;
    const controlCount =
      Number(shouldShowHideButton(state)) +
      Number(shouldShowDesktopIconButton(state));
    const itemCount = Math.max(1, state.apps.length + controlCount);
    if (state.orientation === 'vertical') {
      return {
        width: Math.ceil(140 * scale + 36),
        height: Math.ceil(Math.max(200, itemCount * 86 + 64) * scale + 20),
        compact: false,
        orientation: 'vertical',
        preserveCenter: true,
      };
    }

    return {
      width: Math.ceil(Math.max(200, itemCount * 86 + 64) * scale + 20),
      height: Math.ceil(140 * scale + 36),
      compact: false,
      orientation: 'horizontal',
      preserveCenter: true,
    };
  }

  const api = {
    applicationLayoutSignature,
    layoutChanges,
    dockContentSize,
    shouldShowEmptyAddButton,
    shouldShowDesktopIconButton,
    shouldShowHideButton,
  };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  if (root) root.IcontraDockLayout = api;
})(typeof window === 'undefined' ? null : window);
