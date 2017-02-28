#ifndef H_SETTINGS
#define H_SETTINGS

#include <iostream>
#include <string>

#include <libconfig.h++>

extern libconfig::Config global_config;

template <typename T=int> bool check_and_make_setting(libconfig::Config& cfg, std::string name, libconfig::Setting::Type val_type, T value = 0, libconfig::Setting::Type list_type = libconfig::Setting::TypeString);
void init_settings();

#endif // H_SETTINGS
