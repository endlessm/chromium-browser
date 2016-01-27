// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('options', function() {
  /** @const */ var ArrayDataModel = cr.ui.ArrayDataModel;
  /** @const */ var DeletableItemList = options.DeletableItemList;
  /** @const */ var DeletableItem = options.DeletableItem;

  /**
   * Creates a new exceptions list item.
   * @constructor
   * @extends {options.DeletableItem}
   */
  function IntegratedWebsitesListItem(domain) {
    var el = cr.doc.createElement('div');
    el.__proto__ = IntegratedWebsitesListItem.prototype;
    el.domain = domain;
    el.decorate();
    return el;
  }

  cr.addSingletonGetter(IntegratedWebsitesListItem);

  IntegratedWebsitesListItem.prototype = {
    __proto__: DeletableItem.prototype,

    /**
     * Called when an element is decorated as a list item.
     */
    decorate: function() {
      DeletableItem.prototype.decorate.call(this);

      // The stored label.
      var label = this.ownerDocument.createElement('div');
      label.className = 'domain-name';
      label.textContent = this.domain;
      this.contentElement.appendChild(label);

      this.deletable = true;
    },
  };

  /**
   * Creates a integrated websites list.
   * @constructor
   * @extends {cr.ui.List}
   */
  var IntegratedWebsitesList = cr.ui.define('list');

  cr.addSingletonGetter(IntegratedWebsitesList);

  IntegratedWebsitesList.prototype = {
    __proto__: DeletableItemList.prototype,

    /**
     * Called when an element is decorated as a list.
     */
    decorate: function() {
      DeletableItemList.prototype.decorate.call(this);
      this.reset();
    },

    /** @inheritDoc */
    createItem: function(domain) {
      return new IntegratedWebsitesListItem(domain);
    },

    /*
     * Adds a website domain name to the list of allowed integrated websites.
     * @param {string} domain domain name of the website to add.
     */
    addWebsite: function(domain) {
      if (!domain || this.dataModel.indexOf(domain) >= 0) {
        return;
      }
      this.dataModel.push(domain);
      this.redraw();
      chrome.send('addIntegrationSite', [domain]);
    },

    /**
     * Forces a revailidation of the list content. Content added while the list
     * is hidden is not properly rendered when the list becomes visible. In
     * addition, deleting a single item from the list results in a stale cache
     * requiring an invalidation.
     */
    refresh: function() {
      // TODO(kevers): Investigate if the root source of the problems can be
      // fixed in cr.ui.list.
      this.invalidate();
      this.redraw();
    },

    /**
     * Sets the integrated websites in the js model.
     * @param {Object} entries A list of dictionaries of values, each dictionary
     *     represents an exception.
     */
    setIntegratedWebsites: function(entries) {
      var integratedWebsites = null;
      try {
        integratedWebsites = JSON.parse(entries);
      } catch(e) {
        console.log("Error while parsing integrated websites json: " + entries);
        return;
      }
      // TODO hightlight domains differently based on permission
      var domains = [];
      domains = domains.concat(integratedWebsites['allowed']);
      domains = domains.concat(integratedWebsites['dontask']);
      this.dataModel = new ArrayDataModel(domains);
      this.refresh();
    },

    /**
     * Removes all integration scripts from the js model.
     */
    reset: function() {
      this.dataModel = new ArrayDataModel([]);
    },

    /** @inheritDoc */
    deleteItemAtIndex: function(index) {
      if (index >= 0) {
        var args = [this.dataModel.item(index)];
        chrome.send('removeIntegrationSite', args);
        this.dataModel.splice(index, 1);
      }
    },

    /**
     * The length of the list.
     */
    get length() {
      return null != this.dataModel ? this.dataModel.length : 0;
    },
  };

  var Page = cr.ui.pageManager.Page;
  var PageManager = cr.ui.pageManager.PageManager;

  /**
   * DesktopIntegrationOverlay class
   * Encapsulated handling of the 'Desktop integration' page.
   * @class
   */
  function DesktopIntegrationOverlay() {
    Page.call(this, 'desktopIntegrationOverlay',
                     loadTimeData.getString('desktopIntegrationPage'),
                     'desktop-integration-area');
  }

  cr.addSingletonGetter(DesktopIntegrationOverlay);

  DesktopIntegrationOverlay.prototype = {
    __proto__: Page.prototype,

    /**
     * Initialize the page.
     */
    initializePage: function() {
      Page.prototype.initializePage.call(this);

      var integrationList = $('domains-list');
      IntegratedWebsitesList.decorate(integrationList);
      $('desktop-integrations-overlay-confirm').onclick = function (ev) { PageManager.closeOverlay(); }

      // Set up add button.
      $('desktop-integration-add-button').onclick = function(e) {
        PageManager.showPageByName('addDesktopIntegrationWebsite');
      };

      // Listen to add website dialog ok button.
      var addWebsiteOkButton = $('add-integration-website-overlay-ok-button');
      addWebsiteOkButton.addEventListener('click',
          this.handleAddWebsiteOkButtonClick_.bind(this));
    },

    handleAddWebsiteOkButtonClick_: function () {
      var website = $('integration-domain-url-field').value;
      var integratedDomain = website.replace(/\s*/g, '')
      console.log (integratedDomain);
      $('domains-list').addWebsite(integratedDomain);
      PageManager.closeOverlay();
    },

    /**
     * Called by the options page when this page has been shown.
     */
    didShowPage: function() {
      chrome.send('updateIntegratedWebsitesList');
    },
  };

  DesktopIntegrationOverlay.setIntegratedWebsites = function(entries) {
    $('domains-list').setIntegratedWebsites(entries);
  };

  // Export
  return {
    IntegratedWebsitesListItem: IntegratedWebsitesListItem,
    IntegratedWebsitesList: IntegratedWebsitesList,
    DesktopIntegrationOverlay: DesktopIntegrationOverlay
  };
});

