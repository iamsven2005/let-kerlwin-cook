/*
  secrets_example.h - copy this file to secrets.h (same folder) and fill in your networks.
  secrets.h is listed in .gitignore, so your passwords stay out of the repository.

  Known networks, tried in this order: the first one in range is used.
    user = ""          a normal home network (password only)
    user = your login  a network that asks for a username AND a password
                       (WPA2-Enterprise, e.g. school). Limits of the Wi-Fi chip: username
                       up to 20 characters, password up to 40, PEAP/MSCHAPv2 only, no
                       certificate checking.
*/
#pragma once

#define WIFI_PROFILES \
  { "HomeNetwork", "",           "home-password" },   /* home */             \
  { "SchoolWiFi",  "your-login", "your-password" },   /* school: change these */
