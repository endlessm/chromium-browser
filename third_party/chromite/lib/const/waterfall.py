# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Constants related to waterfall names and behaviors."""

WATERFALL_INTERNAL = 'chromeos'
WATERFALL_EXTERNAL = 'chromiumos'
WATERFALL_INFRA = 'chromeos.infra'
WATERFALL_TRYBOT = 'chromiumos.tryserver'
WATERFALL_RELEASE = 'chromeos_release'
WATERFALL_BRANCH = 'chromeos.branch'
# These waterfalls are not yet using cidb.
WATERFALL_CHROMIUM = 'chromiumos.chromium'
WATERFALL_CHROME = 'chromeos.chrome'
WATERFALL_BRILLO = 'internal.client.brillo'
WATERFALL_WEAVE = 'internal.client.weave'

# These waterfalls should send email reports regardless of cidb connection.
EMAIL_WATERFALLS = (
    WATERFALL_INTERNAL,
    WATERFALL_EXTERNAL,
    WATERFALL_RELEASE,
    WATERFALL_BRANCH,
    WATERFALL_BRILLO,
    WATERFALL_WEAVE,
)

CIDB_KNOWN_WATERFALLS = (WATERFALL_INTERNAL,
                         WATERFALL_EXTERNAL,
                         WATERFALL_TRYBOT,
                         WATERFALL_RELEASE,
                         WATERFALL_BRANCH,
                         WATERFALL_CHROMIUM,
                         WATERFALL_CHROME,)

ALL_WATERFALLS = CIDB_KNOWN_WATERFALLS

# URLs to the various waterfalls.
BUILD_DASHBOARD = 'http://build.chromium.org/p/chromiumos'
BUILD_INT_DASHBOARD = 'https://uberchromegw.corp.google.com/i/chromeos'
TRYBOT_DASHBOARD = 'https://uberchromegw.corp.google.com/i/chromiumos.tryserver'
RELEASE_DASHBOARD = 'https://uberchromegw.corp.google.com/i/chromeos_release'
BRANCH_DASHBOARD = 'https://uberchromegw.corp.google.com/i/chromeos.branch'
CHROMIUM_DASHBOARD = ('https://uberchromegw.corp.google.com/'
                      'i/chromiumos.chromium')
CHROME_DASHBOARD = 'https://uberchromegw.corp.google.com/i/chromeos.chrome'

# Waterfall to dashboard URL mapping.
WATERFALL_TO_DASHBOARD = {
    WATERFALL_INTERNAL: BUILD_INT_DASHBOARD,
    WATERFALL_EXTERNAL: BUILD_DASHBOARD,
    WATERFALL_TRYBOT: TRYBOT_DASHBOARD,
    WATERFALL_RELEASE: RELEASE_DASHBOARD,
    WATERFALL_BRANCH: BRANCH_DASHBOARD,
    WATERFALL_CHROMIUM: CHROMIUM_DASHBOARD,
    WATERFALL_CHROME: CHROME_DASHBOARD,
}

# Sheriff-o-Matic tree which Chrome OS alerts are posted to.
SOM_TREE = 'chromeos'

# Sheriff-o-Matic severities (all severities must start at 1000 and should
# be synchronized with:
# https://cs.chromium.org/chromium/infra/go/src/infra/appengine/sheriff-o-matic/elements/
SOM_SEVERITY_CQ_FAILURE = 1000
SOM_SEVERITY_PFQ_FAILURE = 1001
SOM_SEVERITY_CANARY_FAILURE = 1002
SOM_SEVERITY_RELEASE_FAILURE = 1003
SOM_SEVERITY_CHROME_INFORMATIONAL_FAILURE = 1004
SOM_SEVERITY_CHROMIUM_INFORMATIONAL_FAILURE = 1005

# List of master builds to generate Sheriff-o-Matics alerts for.
# Waterfall, build config, SOM alert severity.
SOM_BUILDS = {
    SOM_TREE: [
        (WATERFALL_INTERNAL, 'master-paladin', SOM_SEVERITY_CQ_FAILURE),
        (WATERFALL_INTERNAL, 'master-android-pfq', SOM_SEVERITY_PFQ_FAILURE),
        (WATERFALL_INTERNAL, 'master-nyc-android-pfq',
         SOM_SEVERITY_PFQ_FAILURE),
        (WATERFALL_INTERNAL, 'master-release', SOM_SEVERITY_CANARY_FAILURE),
    ],

    # TODO: Once SoM supports alerts being added individually, this should
    # be changed to a programatically list instead of a hardcoded list.
    'gardener': [
        (WATERFALL_INTERNAL, 'master-chromium-pfq', SOM_SEVERITY_PFQ_FAILURE),
        (WATERFALL_CHROME, 'lumpy-tot-chrome-pfq-informational',
         SOM_SEVERITY_CHROME_INFORMATIONAL_FAILURE),
        (WATERFALL_CHROME, 'peach_pit-tot-chrome-pfq-informational',
         SOM_SEVERITY_CHROME_INFORMATIONAL_FAILURE),
        (WATERFALL_CHROME, 'cyan-tot-chrome-pfq-informational',
         SOM_SEVERITY_CHROME_INFORMATIONAL_FAILURE),
        (WATERFALL_CHROME, 'tricky-tot-chrome-pfq-informational',
         SOM_SEVERITY_CHROME_INFORMATIONAL_FAILURE),
        (WATERFALL_CHROME, 'veyron_minnie-tot-chrome-pfq-informational',
         SOM_SEVERITY_CHROME_INFORMATIONAL_FAILURE),
        (WATERFALL_CHROMIUM, 'amd64-generic-tot-chromium-pfq-informational',
         SOM_SEVERITY_CHROMIUM_INFORMATIONAL_FAILURE),
        (WATERFALL_CHROMIUM, 'daisy-tot-chromium-pfq-informational',
         SOM_SEVERITY_CHROMIUM_INFORMATIONAL_FAILURE),
        (WATERFALL_CHROMIUM, 'amd64-generic-tot-asan-informational',
         SOM_SEVERITY_CHROMIUM_INFORMATIONAL_FAILURE),
        (WATERFALL_CHROMIUM, 'amd64-generic-telemetry',
         SOM_SEVERITY_CHROMIUM_INFORMATIONAL_FAILURE),
    ],
}
