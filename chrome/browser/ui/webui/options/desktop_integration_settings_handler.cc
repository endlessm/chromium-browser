// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/options/desktop_integration_settings_handler.h"

#include <glib.h>
#include <dlfcn.h>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "grit/generated_resources.h"
//#include "grit/webkit_resources.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/webui/web_ui_util.h"

// Not a constant but preprocessor definition for easy concatenation.
#define kLibunityWebappsEntryPointLibName "libunity-webapps.so.0"

namespace options {

// TODO: -> extract to function
DesktopIntegrationSettingsHandler::DesktopIntegrationSettingsHandler()
    : lib_unity_webapps_handle_(NULL),
      get_all_func_handle_(NULL),
      remove_from_permissions_func_handle_(NULL),
      add_allowed_domain_func_handle_(NULL),
      is_integration_allowed_func_handle_(NULL),
      set_integration_allowed_func_handle_(NULL) {
}

DesktopIntegrationSettingsHandler::~DesktopIntegrationSettingsHandler() {
  if (lib_unity_webapps_handle_) {
    dlclose (lib_unity_webapps_handle_);
    lib_unity_webapps_handle_ = NULL;

    remove_from_permissions_func_handle_ = NULL;
    get_all_func_handle_ = NULL;
    add_allowed_domain_func_handle_ = NULL;
    is_integration_allowed_func_handle_ = NULL;
    set_integration_allowed_func_handle_ = NULL;
  }
}

/////////////////////////////////////////////////////////////////////////////
// OptionsPageUIHandler implementation:
void DesktopIntegrationSettingsHandler::GetLocalizedValues(
    base::DictionaryValue* localized_strings) {
  LOG(INFO) << "GetLocalizedValues";
  DCHECK(localized_strings);

  static OptionsStringResource resources[] = {
    { "desktopIntegrationInfoText",
      IDS_DESKTOP_INTEGRATION_SETTINGS_OVERLAY_DESCRIPTION }
  };

  localized_strings->SetString("add_button",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_INTEGRATION_WEBSITE_ADD_BUTTON));
  localized_strings->SetString("add_desktop_website_input_label",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_INTEGRATION_WEBSITE_ADD_TEXT));
  localized_strings->SetString("add_desktop_website_title",
      l10n_util::GetStringUTF16(IDS_OPTIONS_SETTINGS_INTEGRATION_WEBSITE_ADD_TITLE));

  RegisterStrings(localized_strings, resources, arraysize(resources));
  RegisterTitle(localized_strings, "desktopIntegrationPage",
                IDS_DESKTOP_INTEGRATION_SETTINGS_OVERLAY_TITLE);
}

void DesktopIntegrationSettingsHandler::InitializeHandler() {
  LOG(INFO) << "InitializeHandler";
  if (!IsUnityWebappsInitialized()) {
    LoadUnityWebappsEntryPoint();
  }
}

void DesktopIntegrationSettingsHandler::InitializePage() {
  LOG(INFO) << "InitializePage";
  DCHECK(IsUnityWebappsInitialized());
  LoadIntegratedWebsitesData();
}

void DesktopIntegrationSettingsHandler::RegisterMessages() {
  LOG(INFO) << "RegisterMessages";
  web_ui()->RegisterMessageCallback(
      std::string("addIntegrationSite"),
      base::Bind(&DesktopIntegrationSettingsHandler::AddIntegrationSite,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      std::string("setDesktopIntegrationAllowed"),
      base::Bind(&DesktopIntegrationSettingsHandler::SetDesktopIntegrationAllowed,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      std::string("removeIntegrationSite"),
      base::Bind(&DesktopIntegrationSettingsHandler::RemoveIntegrationSite,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      std::string("updateIntegratedWebsitesList"),
      base::Bind(&DesktopIntegrationSettingsHandler::UpdateIntegratedWebsitesList,
                 base::Unretained(this)));
}

void DesktopIntegrationSettingsHandler::LoadUnityWebappsEntryPoint() {
  LOG(INFO) << "LoadUnityWebappsEntryPoint";
  DCHECK(!IsUnityWebappsInitialized());

  // TODO run on FILE thread
  static const char* const search_paths[] = {
    kLibunityWebappsEntryPointLibName,
    "/usr/local/lib/" kLibunityWebappsEntryPointLibName
    , "/usr/lib/" kLibunityWebappsEntryPointLibName
  };

  void * handle = NULL;
  for (size_t i = 0; i < sizeof(search_paths)/sizeof(search_paths[0]); ++i) {
    DCHECK(handle == NULL);
    // TODO validate path?
    handle = dlopen (search_paths[i], RTLD_LAZY|RTLD_GLOBAL);
    if (handle) {
      LOG(INFO) << "Found libunitywebapps entry point:" << search_paths[i];
      break;
    } else
      LOG(INFO) << "Found libunitywebapps entry point:" << search_paths[i];
  }
  if (!handle) {
    LOG(INFO) << "Could not load Unity Webapps entry point library";
    return;
  }

  void * get_all_handle =
    dlsym (handle, "unity_webapps_permissions_get_all_domains");
  void * remove_from_permissions_handle =
    dlsym (handle, "unity_webapps_permissions_remove_domain_from_permissions");
  void * add_allowed_domain_handle =
    dlsym (handle, "unity_webapps_permissions_allow_domain");
  void * is_integration_allowed_handle =
    dlsym (handle, "unity_webapps_permissions_is_integration_allowed");
  void * set_integration_allowed_handle =
    dlsym (handle, "unity_webapps_permissions_set_integration_allowed");

  if (!get_all_handle ||
      !remove_from_permissions_handle ||
      !add_allowed_domain_handle ||
      !is_integration_allowed_handle ||
      !set_integration_allowed_handle) {
    LOG(WARNING) << "Could not load Unity Webapps entry point functions";
    dlclose(handle);
    return;
  }

  // TODO(FIXME): cleanup that mess
  lib_unity_webapps_handle_ = handle;
  get_all_func_handle_ = get_all_handle;
  remove_from_permissions_func_handle_ = remove_from_permissions_handle;
  add_allowed_domain_func_handle_ = add_allowed_domain_handle;
  is_integration_allowed_func_handle_ = is_integration_allowed_handle;
  set_integration_allowed_func_handle_ = set_integration_allowed_handle;
}

bool DesktopIntegrationSettingsHandler::IsUnityWebappsInitialized() const {
  return NULL != lib_unity_webapps_handle_
    && NULL != get_all_func_handle_
    && NULL != remove_from_permissions_func_handle_
    && NULL != add_allowed_domain_func_handle_
    && NULL != is_integration_allowed_func_handle_
    && NULL != set_integration_allowed_func_handle_;
}

void DesktopIntegrationSettingsHandler::LoadIntegratedWebsitesData() {
  LOG(INFO) << "LoadIntegratedWebsitesData";
  if (!IsUnityWebappsInitialized()) {
    web_ui()->CallJavascriptFunction(
        "BrowserOptions.disableDesktopIntegration");
    LOG(INFO) << "Disabling Desktop Integration options";
    return;
  }

  typedef gboolean (* IsIntegrationAllowedFunc) (void);
  gboolean isallowed =
    ((IsIntegrationAllowedFunc) is_integration_allowed_func_handle_) ();
  web_ui()->CallJavascriptFunction(
       "BrowserOptions.setDesktopIntegrationIsAllowed",
       base::FundamentalValue(isallowed));
}

void DesktopIntegrationSettingsHandler::UpdateIntegratedWebsitesList(
    const base::ListValue* args) {
  LOG(INFO) << "UpdateIntegratedWebsitesList";
  DCHECK(IsUnityWebappsInitialized());

  // TODO move elsewhere
  typedef gchar* (* GetAllDomainsFunc) (void);

  gchar* all_domains = ((GetAllDomainsFunc) get_all_func_handle_) ();
  if (all_domains) { 
    web_ui()->CallJavascriptFunction(
        "DesktopIntegrationOverlay.setIntegratedWebsites",
        base::StringValue(all_domains));
  }
}

void DesktopIntegrationSettingsHandler::SetDesktopIntegrationAllowed(
    const base::ListValue* args) {
  LOG(INFO) << "SetDesktopIntegrationAllowed";
  DCHECK(IsUnityWebappsInitialized());

  // TODO move elsewhere
  typedef void (* SetDesktopIntegrationAllowedFunc) (gboolean);

  bool is_allowed;
  if (!args->GetBoolean(0, &is_allowed)) {
    NOTREACHED();
    return;
  }

  LOG(INFO) << "Setting desktop integration:" << is_allowed;

  ((SetDesktopIntegrationAllowedFunc) set_integration_allowed_func_handle_) (
      is_allowed ? TRUE : FALSE);
}

void DesktopIntegrationSettingsHandler::AddIntegrationSite(
    const base::ListValue* args) {
  LOG(INFO) << "AddIntegrationSite";
  DCHECK(IsUnityWebappsInitialized());

  // TODO move elsewhere
  typedef void (* AddDomainFromPermissionsFunc) (gchar *);

  std::string domain;
  if (!args->GetString(0, &domain)) {
    NOTREACHED();
    return;
  }

  LOG(INFO) << "Adding domain:" << domain << " to the list of websites allowed";

  ((AddDomainFromPermissionsFunc) add_allowed_domain_func_handle_) (
      const_cast<gchar *>(domain.c_str()));

  LoadIntegratedWebsitesData ();
}

void DesktopIntegrationSettingsHandler::RemoveIntegrationSite(
    const base::ListValue* args) {
  LOG(INFO) << "RemoveIntegrationSite";
  DCHECK(IsUnityWebappsInitialized());

  // TODO move elsewhere
  typedef void (* RemoveDomainFromPermissionsFunc) (gchar *);

  std::string domain;
  if (!args->GetString(0, &domain)) {
    NOTREACHED();
    return;
  }

  LOG(INFO) << "Removing domain:" << domain << " from the list of websites not prompting integration";

  ((RemoveDomainFromPermissionsFunc) remove_from_permissions_func_handle_) (
      const_cast<gchar *>(domain.c_str()));

  LoadIntegratedWebsitesData ();
}

}  // namespace options
