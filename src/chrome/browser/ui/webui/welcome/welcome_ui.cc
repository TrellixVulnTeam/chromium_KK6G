// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/welcome/welcome_ui.h"

#include <memory>
#include <string>

#include "base/metrics/histogram_macros.h"
#include "build/build_config.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/ui/webui/welcome/nux/constants.h"
#include "chrome/browser/ui/webui/welcome/nux/email_handler.h"
#include "chrome/browser/ui/webui/welcome/nux/google_apps_handler.h"
#include "chrome/browser/ui/webui/welcome/nux/set_as_default_handler.h"
#include "chrome/browser/ui/webui/welcome/nux_helper.h"
#include "chrome/browser/ui/webui/welcome/welcome_handler.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/chrome_unscaled_resources.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/onboarding_welcome_resources.h"
#include "chrome/grit/onboarding_welcome_resources_map.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"
#include "net/base/url_util.h"
#include "ui/base/l10n/l10n_util.h"

namespace {
const bool kIsBranded =
#if defined(GOOGLE_CHROME_BUILD)
    true;
#else
    false;
#endif
}  // namespace

WelcomeUI::WelcomeUI(content::WebUI* web_ui, const GURL& url)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);

  // This page is not shown to incognito or guest profiles. If one should end up
  // here, we return, causing a 404-like page.
  if (!profile ||
      profile->GetProfileType() != Profile::ProfileType::REGULAR_PROFILE) {
    return;
  }

  StorePageSeen(profile);

  web_ui->AddMessageHandler(std::make_unique<WelcomeHandler>(web_ui));

  content::WebUIDataSource* html_source =
      content::WebUIDataSource::Create(url.host());

  bool is_dice =
      AccountConsistencyModeManager::IsDiceEnabledForProfile(profile);

  // There are multiple possible configurations that affects the layout, but
  // first add resources that are shared across all layouts.
  html_source->AddResourcePath("logo.png", IDR_PRODUCT_LOGO_128);
  html_source->AddResourcePath("logo2x.png", IDR_PRODUCT_LOGO_256);

  if (nux::IsNuxOnboardingEnabled(profile)) {
    // Add Onboarding welcome strings.
    html_source->AddLocalizedString("headerText", IDS_WELCOME_HEADER);
    html_source->AddLocalizedString("acceptText", IDS_WELCOME_ACCEPT_BUTTON);

    // Add all Onboarding resources.
    for (size_t i = 0; i < kOnboardingWelcomeResourcesSize; ++i) {
      html_source->AddResourcePath(kOnboardingWelcomeResources[i].name,
                                   kOnboardingWelcomeResources[i].value);
    }
    html_source->SetDefaultResource(
        IDR_WELCOME_ONBOARDING_WELCOME_WELCOME_HTML);

    html_source->AddResourcePath(
        "images/background_svgs/blue_circle.svg",
        IDR_WELCOME_ONBOARDING_WELCOME_IMAGES_BACKGROUND_SVGS_BLUE_CIRCLE_SVG);
    html_source->AddResourcePath(
        "images/background_svgs/green_rectangle.svg",
        IDR_WELCOME_ONBOARDING_WELCOME_IMAGES_BACKGROUND_SVGS_GREEN_RECTANGLE_SVG);
    html_source->AddResourcePath(
        "images/background_svgs/grey_oval.svg",
        IDR_WELCOME_ONBOARDING_WELCOME_IMAGES_BACKGROUND_SVGS_GREY_OVAL_SVG);
    html_source->AddResourcePath(
        "images/background_svgs/grey_rounded_rectangle.svg",
        IDR_WELCOME_ONBOARDING_WELCOME_IMAGES_BACKGROUND_SVGS_GREY_ROUNDED_RECTANGLE_SVG);
    html_source->AddResourcePath(
        "images/background_svgs/red_triangle.svg",
        IDR_WELCOME_ONBOARDING_WELCOME_IMAGES_BACKGROUND_SVGS_RED_TRIANGLE_SVG);
    html_source->AddResourcePath(
        "images/background_svgs/yellow_dots.svg",
        IDR_WELCOME_ONBOARDING_WELCOME_IMAGES_BACKGROUND_SVGS_YELLOW_DOTS_SVG);
    html_source->AddResourcePath(
        "images/background_svgs/yellow_semicircle.svg",
        IDR_WELCOME_ONBOARDING_WELCOME_IMAGES_BACKGROUND_SVGS_YELLOW_SEMICIRCLE_SVG);

    // Add email provider bookmarking onboarding module.
    web_ui->AddMessageHandler(std::make_unique<nux::EmailHandler>(
        profile->GetPrefs(), FaviconServiceFactory::GetForProfile(
                                 profile, ServiceAccessType::EXPLICIT_ACCESS)));
    nux::EmailHandler::AddSources(html_source, profile->GetPrefs());

    // Add google apps bookmarking onboarding module.
    web_ui->AddMessageHandler(std::make_unique<nux::GoogleAppsHandler>(
        profile->GetPrefs(), FaviconServiceFactory::GetForProfile(
                                 profile, ServiceAccessType::EXPLICIT_ACCESS)));
    nux::GoogleAppsHandler::AddSources(html_source, profile->GetPrefs());

    // Add set-as-default onboarding module.
    web_ui->AddMessageHandler(std::make_unique<nux::SetAsDefaultHandler>());
  } else if (kIsBranded && is_dice) {
    // Use special layout if the application is branded and DICE is enabled.
    html_source->AddLocalizedString("headerText", IDS_WELCOME_HEADER);
    html_source->AddLocalizedString("acceptText",
                                    IDS_PROFILES_DICE_SIGNIN_BUTTON);
    html_source->AddLocalizedString("secondHeaderText",
                                    IDS_DICE_WELCOME_SECOND_HEADER);
    html_source->AddLocalizedString("descriptionText",
                                    IDS_DICE_WELCOME_DESCRIPTION);
    html_source->AddLocalizedString("declineText",
                                    IDS_DICE_WELCOME_DECLINE_BUTTON);
    html_source->AddResourcePath("welcome_browser_proxy.html",
                                 IDR_DICE_WELCOME_BROWSER_PROXY_HTML);
    html_source->AddResourcePath("welcome_browser_proxy.js",
                                 IDR_DICE_WELCOME_BROWSER_PROXY_JS);
    html_source->AddResourcePath("welcome_app.html", IDR_DICE_WELCOME_APP_HTML);
    html_source->AddResourcePath("welcome_app.js", IDR_DICE_WELCOME_APP_JS);
    html_source->AddResourcePath("welcome.css", IDR_DICE_WELCOME_CSS);
    html_source->SetDefaultResource(IDR_DICE_WELCOME_HTML);
  } else {
    // Use default layout for non-DICE or unbranded build.
    std::string value;
    bool is_everywhere_variant =
        (net::GetValueForKeyInQuery(url, "variant", &value) &&
         value == "everywhere");

    if (kIsBranded) {
      base::string16 subheader =
          is_everywhere_variant
              ? base::string16()
              : l10n_util::GetStringUTF16(IDS_WELCOME_SUBHEADER);
      html_source->AddString("subheaderText", subheader);
    }

    int header_id = is_everywhere_variant ? IDS_WELCOME_HEADER_AFTER_FIRST_RUN
                                          : IDS_WELCOME_HEADER;
    html_source->AddString("headerText", l10n_util::GetStringUTF16(header_id));
    html_source->AddLocalizedString("acceptText", IDS_WELCOME_ACCEPT_BUTTON);
    html_source->AddLocalizedString("descriptionText", IDS_WELCOME_DESCRIPTION);
    html_source->AddLocalizedString("declineText", IDS_WELCOME_DECLINE_BUTTON);
    html_source->AddResourcePath("welcome.js", IDR_WELCOME_JS);
    html_source->AddResourcePath("welcome.css", IDR_WELCOME_CSS);
    html_source->SetDefaultResource(IDR_WELCOME_HTML);
  }

  content::WebUIDataSource::Add(profile, html_source);
}

WelcomeUI::~WelcomeUI() {}

void WelcomeUI::StorePageSeen(Profile* profile) {
  // Store that this profile has been shown the Welcome page.
  profile->GetPrefs()->SetBoolean(prefs::kHasSeenWelcomePage, true);
}
