/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_task(async function() {
  // allow top level data: URI navigations, otherwise loading data: URIs
  // in toplevel windows fail.
  await SpecialPowers.pushPrefEnv({
    set: [["security.data_uri.block_toplevel_data_uri_navigations", false]],
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, null, false);

  let browser = tab.linkedBrowser;
  browser.stop(); // stop the about:blank load

  let writeDomainURL = encodeURI(
    "data:text/html,<script>document.write(document.domain);</script>"
  );

  let tests = [
    {
      name: "view background image",
      url: "http://mochi.test:8888/",
      element: "body",
      go() {
        return SpecialPowers.spawn(
          gBrowser.selectedBrowser,
          [{ writeDomainURL }],
          async function(arg) {
            let contentBody = content.document.body;
            contentBody.style.backgroundImage =
              "url('" + arg.writeDomainURL + "')";

            return "context-viewbgimage";
          }
        );
      },
      verify() {
        return SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function(
          arg
        ) {
          Assert.ok(
            !content.document.body.textContent,
            "no domain was inherited for view background image"
          );
        });
      },
    },
    {
      name: "view image",
      url: "http://mochi.test:8888/",
      element: "img",
      go() {
        return SpecialPowers.spawn(
          gBrowser.selectedBrowser,
          [{ writeDomainURL }],
          async function(arg) {
            let doc = content.document;
            let img = doc.createElement("img");
            img.height = 100;
            img.width = 100;
            img.setAttribute("src", arg.writeDomainURL);
            doc.body.insertBefore(img, doc.body.firstElementChild);

            return "context-viewimage";
          }
        );
      },
      verify() {
        return SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function(
          arg
        ) {
          Assert.ok(
            !content.document.body.textContent,
            "no domain was inherited for view image"
          );
        });
      },
    },
    {
      name: "show only this frame",
      url: "http://mochi.test:8888/",
      element: "html",
      frameIndex: 0,
      go() {
        return SpecialPowers.spawn(
          gBrowser.selectedBrowser,
          [{ writeDomainURL }],
          async function(arg) {
            let doc = content.document;
            let iframe = doc.createElement("iframe");
            iframe.setAttribute("src", arg.writeDomainURL);
            doc.body.insertBefore(iframe, doc.body.firstElementChild);

            // Wait for the iframe to load.
            return new Promise(resolve => {
              iframe.addEventListener(
                "load",
                function() {
                  resolve("context-showonlythisframe");
                },
                { capture: true, once: true }
              );
            });
          }
        );
      },
      verify() {
        return SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function(
          arg
        ) {
          Assert.ok(
            !content.document.body.textContent,
            "no domain was inherited for 'show only this frame'"
          );
        });
      },
    },
  ];

  let contentAreaContextMenu = document.getElementById(
    "contentAreaContextMenu"
  );

  for (let test of tests) {
    let loadedPromise = BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser
    );
    BrowserTestUtils.loadURI(gBrowser, test.url);
    await loadedPromise;

    info("Run subtest " + test.name);
    let commandToRun = await test.go();

    let popupShownPromise = BrowserTestUtils.waitForEvent(
      contentAreaContextMenu,
      "popupshown"
    );

    let browsingContext = gBrowser.selectedBrowser.browsingContext;
    if (test.frameIndex != null) {
      browsingContext = browsingContext.getChildren()[test.frameIndex];
    }

    await new Promise(r => {
      SimpleTest.executeSoon(r);
    });

    // Sometimes, the iframe test fails as the child iframe hasn't finishing layout
    // yet. Try again in this case.
    while (true) {
      try {
        await BrowserTestUtils.synthesizeMouse(
          test.element,
          3,
          3,
          { type: "contextmenu", button: 2 },
          browsingContext
        );
      } catch (ex) {
        continue;
      }
      break;
    }

    await popupShownPromise;
    info("onImage: " + gContextMenu.onImage);

    let loadedAfterCommandPromise = BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser
    );
    document.getElementById(commandToRun).click();
    await loadedAfterCommandPromise;

    await test.verify();

    let popupHiddenPromise = BrowserTestUtils.waitForEvent(
      contentAreaContextMenu,
      "popuphidden"
    );
    contentAreaContextMenu.hidePopup();
    await popupHiddenPromise;
  }

  gBrowser.removeCurrentTab();
});
