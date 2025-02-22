/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

XPCOMUtils.defineLazyModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.jsm",
  PlacesSearchAutocompleteProvider:
    "resource://gre/modules/PlacesSearchAutocompleteProvider.jsm",
});

const EN_US_TOPSITES =
  "https://www.youtube.com/,https://www.facebook.com/,https://www.amazon.com/,https://www.reddit.com/,https://www.wikipedia.org/,https://twitter.com/";

add_task(async function init() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.update1", true],
      ["browser.urlbar.openViewOnFocus", true],
      ["browser.newtabpage.activity-stream.default.sites", EN_US_TOPSITES],
    ],
  });

  await updateTopSites(
    sites => sites && sites.length == EN_US_TOPSITES.split(",").length
  );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://example.com/"
  );

  registerCleanupFunction(() => {
    BrowserTestUtils.removeTab(tab);
  });
});

add_task(async function topSitesShown() {
  let sites = AboutNewTab.getTopSites();
  Assert.equal(
    sites.length,
    6,
    "The test suite browser should have 6 Top Sites."
  );
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(window.gURLBar.inputField, {});
  });
  Assert.ok(window.gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);
  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    sites.length,
    "The number of results should be the same as the number of Top Sites (6)."
  );

  for (let i = 0; i < sites.length; i++) {
    let site = sites[i];
    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (site.searchTopSite) {
      Assert.equal(
        result.searchParams.keyword,
        site.label,
        "The search Top Site should have an alias."
      );
      continue;
    }

    Assert.equal(
      site.url,
      result.url,
      "The Top Site URL and the result URL shoud match."
    );
    Assert.equal(
      site.label || site.title || site.hostname,
      result.title,
      "The Top Site title and the result title shoud match."
    );
  }
  await UrlbarTestUtils.promisePopupClose(window, () => {
    window.gURLBar.blur();
  });
});

add_task(async function selectSearchTopSite() {
  await updateTopSites(
    sites => sites && sites[0] && sites[0].searchTopSite,
    true
  );
  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(window.gURLBar.inputField, {});
  });
  await UrlbarTestUtils.promiseSearchComplete(window);

  let amazonSearch = await UrlbarTestUtils.waitForAutocompleteResultAt(
    window,
    0
  );

  Assert.equal(
    amazonSearch.result.type,
    UrlbarUtils.RESULT_TYPE.SEARCH,
    "First result should have SEARCH type."
  );

  Assert.equal(
    amazonSearch.result.payload.keyword,
    "@amazon",
    "First result should have the Amazon keyword."
  );

  EventUtils.synthesizeMouseAtCenter(amazonSearch, {});

  Assert.equal(
    gURLBar.value,
    "@amazon ",
    "The Urlbar should be populated with the search alias."
  );
  await UrlbarTestUtils.promisePopupClose(window, () => {
    window.gURLBar.blur();
  });
});

add_task(async function topSitesBookmarksAndTabs() {
  // Add some visits to a URL.
  for (let i = 0; i < 5; i++) {
    await PlacesTestUtils.addVisits("http://example.com/");
  }

  // Wait for example.com to be listed first.
  await updateTopSites(sites => {
    return sites && sites[0] && sites[0].url == "http://example.com/";
  });

  await PlacesUtils.bookmarks.insert({
    parentGuid: PlacesUtils.bookmarks.unfiledGuid,
    url: "https://www.youtube.com/",
    title: "YouTube",
  });

  registerCleanupFunction(async function() {
    await PlacesUtils.bookmarks.eraseEverything();
    await PlacesUtils.history.clear();
  });

  let sites = AboutNewTab.getTopSites();
  Assert.equal(
    sites.length,
    7,
    "The test suite browser should have 7 Top Sites."
  );

  await UrlbarTestUtils.promisePopupOpen(window, () => {
    EventUtils.synthesizeMouseAtCenter(window.gURLBar.inputField, {});
  });
  Assert.ok(window.gURLBar.view.isOpen, "UrlbarView should be open.");
  await UrlbarTestUtils.promiseSearchComplete(window);

  Assert.equal(
    UrlbarTestUtils.getResultCount(window),
    7,
    "The number of results should be the same as the number of Top Sites (7)."
  );

  let exampleResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
  Assert.equal(
    exampleResult.url,
    "http://example.com/",
    "The example.com Top Site should be the second result."
  );
  Assert.equal(
    exampleResult.source,
    UrlbarUtils.RESULT_SOURCE.TABS,
    "The example.com Top Site should appear in the view as an open tab result."
  );

  let youtubeResult = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
  Assert.equal(
    youtubeResult.url,
    "https://www.youtube.com/",
    "The YouTube Top Site should be the third result."
  );
  Assert.equal(
    youtubeResult.source,
    UrlbarUtils.RESULT_SOURCE.BOOKMARKS,
    "The YouTube Top Site should appear in the view as a bookmark result."
  );
  await UrlbarTestUtils.promisePopupClose(window, () => {
    window.gURLBar.blur();
  });
});
