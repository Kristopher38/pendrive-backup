#ifndef H_SETTINGS
#define H_SETTINGS

#include <iostream>
#include <string>
#include <limits.h> // PATH_MAX
#include <unistd.h> // readlink

#include <libconfig.h++>

extern libconfig::Config global_config;
extern std::string app_launch_dir;             /* Ścieżka z której uruchamiany jest program (argv[0]) */

template <typename T=int> bool check_and_make_setting(libconfig::Config& cfg, std::string name, libconfig::Setting::Type val_type, T value = 0, libconfig::Setting::Type list_type = libconfig::Setting::TypeString);
int init_settings();

#endif // H_SETTINGS
