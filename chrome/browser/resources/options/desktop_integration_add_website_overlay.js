// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///////////////////////////////////////////////////////////////////////////////
// AddDesktopIntegrationWebsiteOverlay class:

cr.define('options', function() {
  var Page = cr.ui.pageManager.Page;
  var PageManager = cr.ui.pageManager.PageManager;

  /**
   * @constructor
   */
  function AddDesktopIntegrationWebsiteOverlay() {
    Page.call(this, 'addDesktopIntegrationWebsite',
              loadTimeData.getString('add_button'),
              'add-desktop-integration-website-overlay-page');
  }

  cr.addSingletonGetter(AddDesktopIntegrationWebsiteOverlay);

  AddDesktopIntegrationWebsiteOverlay.prototype = {
    // Inherit AddDesktopIntegrationWebsiteOverlay from Page.
    __proto__: Page.prototype,

    /**
     * Initializes AddDesktopIntegrationWebsiteOverlay page.
     * Calls base class implementation to starts preference initialization.
     */
    initializePage: function() {
      // Call base class implementation to starts preference initialization.
      Page.prototype.initializePage.call(this);
      
      // Cleanup any previously entered text.
      $('integration-domain-url-field').value = '';

      // Set up the cancel button.
      $('add-integration-website-overlay-cancel-button').onclick = function(e) {
        PageManager.closeOverlay();
      };
    },
  };

  return {
    AddDesktopIntegrationWebsiteOverlay: AddDesktopIntegrationWebsiteOverlay
  };
});
