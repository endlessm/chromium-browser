#!/usr/bin/python3
#
# chromium-browser-appmode: takes an URI like webapp://<WM_CLASS>@<ACTUAL_URI>,
# parses it and and launches chromium-browser with --class and --app arguments.
#
# Copyright (C) 2016, 2017 Endless Mobile, Inc.
# Authors:
#  Mario Sanchez Prada <mario@endlessm.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

import os
import subprocess
import sys

import gi
gi.require_version('Flatpak', '1.0')
from gi.repository import Flatpak
from gi.repository import GLib, Gio


CHROMIUM_COMMAND = 'chromium-browser'
CHROMIUM_DATA_DIR = 'chromium-appmode'

CHROME_COMMAND = 'eos-google-chrome'
CHROME_DATA_DIR = 'chrome-appmode'

FLATPAK_CHROME_APP_ID = 'com.google.Chrome'


class ParsingError(Exception):
   def __init__(self, value):
      Exception.__init__(self)
      self.value = value

   def __str__(self):
      return repr(self.value)


def parseURI(uri):
   if not str.startswith(uri, 'webapp://'):
      raise ParsingError('Wrong URI scheme')

   # Get the application ID, to be used to set WM_CLASS,
   # and the URI we want to launch in application mode.
   uri_parts = uri[9:].split('@', 2)
   if len(uri_parts) < 2:
      raise ParsingError('Invalid URI format')

   # First element is the value to be set as WM_CLASS,
   # second element is the actual URI we'll launch.
   return (uri_parts[0], uri_parts[1])


if __name__ == '__main__':
   if len(sys.argv[1:]) < 1:
      print('Missing parameter')
      sys.exit(2)

   custom_uri = sys.argv[1]
   try:
      (wm_class, actual_uri) = parseURI(custom_uri)

      scheme = GLib.uri_parse_scheme(actual_uri)
      default_browser = Gio.AppInfo.get_default_for_uri_scheme(scheme)
      print("Found default browser: {}".format(default_browser.get_display_name()))

      # We only use Chrome if it's the default application AND it's installed, otherwise
      # we use Chromium and ignore any other browser that might have been set (e.g. Firefox).
      command = CHROMIUM_COMMAND
      if CHROME_COMMAND in default_browser.get_executable():
         installation = Flatpak.Installation.new_system()
         try:
            installation.get_current_installed_app(FLATPAK_CHROME_APP_ID, None)
            print("Chrome set as the default application and available. Selecting...")
            command = CHROME_COMMAND
         except GLib.Error as e:
            print("Chrome set as the default application but not available. Ignoring...")

      # External web apps (i.e. URLs not managed by chromium-browser as extensions) need
      # to run in an isolated environment not to conflict with the main browsing session.
      data_dir = CHROME_DATA_DIR if command == CHROME_COMMAND else CHROMIUM_DATA_DIR
      config_dir = os.path.expanduser('~/.config/{}/{}'.format(data_dir, wm_class))
      print("Using {} to launch web application (config dir: {})...".format(command, config_dir))

      subprocess.Popen([command,
                        '--class={}'.format(wm_class),
                        '--app={}'.format(actual_uri),
                        '--start-maximized',
                        '--no-default-browser-check',
                        '--no-first-run',
                        '--user-data-dir={}'.format(config_dir)])
   except ParsingError as e:
      print('Error parsing custom URI: {}'.format(e.value))
      print('Usage: chromium-browser-appmode webapp://<WM_CLASS>@<URI>')
      sys.exit(2)
   except OSError as e:
      print('Error launching chromium: {}'.format(e.value))
      sys.exit(2)

   sys.exit(0)
