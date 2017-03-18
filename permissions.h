#ifndef H_PERMISSIONS
#define H_PERMISSIONS

#include <unistd.h> // setgid setuid
#include <pwd.h> // access passwd

#include <iostream>
#include <fstream>

#include "settings.h"

std::string get_user_perm_name();
int drop_root(std::string drop_to);

#endif // H_PERMISSIONS
