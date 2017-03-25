#ifndef H_PERMISSIONS
#define H_PERMISSIONS

#include <unistd.h> // setgid setuid

#include <iostream>
#include <fstream>

#include "settings.h"

std::string get_user_perm_name();
/* Zwraca nazwę użytkownika znajdującego się w pliku konfiguracyjnym /etc/pbackup/userperm */

int drop_root(std::string drop_to);
/* Funkcja przelogowywująca się na użytkownika określonego stringiem drop_to
   Zwraca 0 w przypadku sukcesu, -1 w przypadku porażki. W przypadku porażki program dalej
   będzie działał z uprawnieniami użytkownika który go uruchomił.
   Argumenty:
    std::string drop_to - nazwa użytkownika na którą program ma się przelogować
*/

#endif // H_PERMISSIONS
