/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const Types = require("devtools/client/responsive/types");

add_task(async function() {
  const newWindowPromise = BrowserTestUtils.waitForNewWindow();
  window.open("data:text/html;charset=utf-8,", "_blank", "noopener,all");
  const newWindow = await newWindowPromise;

  newWindow.focus();
  await BrowserTestUtils.browserLoaded(newWindow.gBrowser.selectedBrowser);

  const tab = newWindow.gBrowser.selectedTab;
  const { ui } = await openRDM(tab);
  const { store } = ui.toolWindow;
  await waitUntilState(
    store,
    state =>
      state.viewports.length == 1 &&
      state.devices.listState == Types.loadableState.LOADED
  );

  // Close the window on a tab with an active responsive design UI and
  // wait for the UI to gracefully shutdown.  This has leaked the window
  // in the past.
  ok(
    ResponsiveUIManager.isActiveForTab(tab),
    "ResponsiveUI should be active for tab when the window is closed"
  );
  const offPromise = once(ResponsiveUIManager, "off");
  await BrowserTestUtils.closeWindow(newWindow);
  await offPromise;
});
