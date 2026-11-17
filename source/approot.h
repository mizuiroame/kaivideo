// Where an app keeps its own files: next to its NRO, as brewstore's path
// guide asks, in a "config" folder of the app's own folder:
//   sdmc:/brewstore/apps/<app>/<App>.nro -> sdmc:/brewstore/apps/<app>/config
//   sdmc:/switch/<app>/<App>.nro         -> sdmc:/switch/<app>/config
//   sdmc:/switch/<App>.nro               -> sdmc:/switch/<app>/config
// (a folder of its own, never files loose in sdmc:/switch). Without a usable
// NRO path, the fixed folder used before (sdmc:/config/<app>).
// The same file is in KaiPlayer, KaiRadio and KaiVideo.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define APPROOT_PATH_MAX 512

// The folder for app's files (app: its name in lowercase, "kaivideo"), from
// the NRO's path (argv[0]). False when it falls back to sdmc:/config/<app>.
bool appRootFolder(const char *nro_path, const char *app, char *out, size_t size);

// Makes folder, and the folders above it.
bool appRootMakeFolder(const char *folder);

// Once (a ".moved" file marks it): copies into folder what old_folder has
// (files, and the files of its subfolders) that folder hasn't. old_folder is
// left as it is. Returns how many files were copied.
int appRootMigrate(const char *old_folder, const char *folder);
