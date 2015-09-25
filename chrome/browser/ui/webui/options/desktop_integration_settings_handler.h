// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_OPTIONS_DESKTOP_INTEGRATION_SETTINGS_HANDLER2_H_
#define CHROME_BROWSER_UI_WEBUI_OPTIONS_DESKTOP_INTEGRATION_SETTINGS_HANDLER2_H_

#include <string>

#include "base/compiler_specific.h"
#include "chrome/browser/ui/webui/options/options_ui.h"

namespace base {
class DictionaryValue;
class ListValue;
}

namespace options {

class DesktopIntegrationSettingsHandler : public OptionsPageUIHandler {
 public:
  DesktopIntegrationSettingsHandler();
  ~DesktopIntegrationSettingsHandler() override;

  // OptionsPageUIHandler implementation.
  void GetLocalizedValues(
      base::DictionaryValue* localized_strings) override;
  void InitializeHandler() override;
  void InitializePage() override;
  void RegisterMessages() override;

 private:

  // Loads the data associated with the currently integrated websites.
  void LoadIntegratedWebsitesData();

  // Initializes the list of website domain names that are currently
  // either allowed or 'dontask' and send it to the overlay.
  void LoadUnityWebappsEntryPoint();

  // Removes an website from the list of integrated websites that won't prompt
  // from integration.
  // |args| - A string, the domain name of the website to remove.
  void RemoveIntegrationSite(const base::ListValue* args);

  // Adds an website from the list of integrated websites that won't prompt
  // from integration. The website is being added to the list of 'allowed' sites.
  // |args| - A string, the domain name of the website to add.
  void AddIntegrationSite(const base::ListValue* args);

  // Updates the integration allowed flag.
  // |args| - A boolean flag indicating if integration should be allowed
  void SetDesktopIntegrationAllowed(const base::ListValue* args);

  // Updates the list of integrated websites.
  void UpdateIntegratedWebsitesList(const base::ListValue* args);

  // Predicate informing if we have been able to initialize the connection
  // with the unity-webapps library (entry point for website integration
  // permissions).
  bool IsUnityWebappsInitialized() const;

  // 
  void * lib_unity_webapps_handle_;
  void * get_all_func_handle_;
  void * remove_from_permissions_func_handle_;
  void * add_allowed_domain_func_handle_;
  void * is_integration_allowed_func_handle_;
  void * set_integration_allowed_func_handle_;

  DISALLOW_COPY_AND_ASSIGN(DesktopIntegrationSettingsHandler);
};

}  // namespace options

#endif  // CHROME_BROWSER_UI_WEBUI_OPTIONS_DESKTOP_INTEGRATION_SETTINGS_HANDLER2_H_
