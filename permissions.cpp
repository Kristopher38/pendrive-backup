#include "permissions.h"

std::string get_user_perm_name() /* Zwraca nazwę użytkownika na którego program ma się logować (z którego uprawnieniami ma działać) */
{
    /* Odczyt z pliku userperm */
    std::ifstream userfile(app_launch_dir + "userperm", std::ifstream::in);
    std::string user;
    if (userfile.good())
        userfile>>user;
    /* W przypadku błędów funkcja zwraca pusty string */
    else {
        std::cerr<<"I/O error while reading user permission file (does userperm file exists?)"<<std::endl;
        return std::string();
    }
    return user;
}

int drop_root(std::string drop_to) /* Przelogowuje się z roota na użytkownika o wskazanej w 'drop_to' nazwie, zwraca -1 w przypadku porażki, 0 w przypadku sukcesu*/
{
    /* sprawdzenie niezerowości długości nazwy */
    if (drop_to.length() == 0)
        return -1;

    /* Pobranie największej wartości bufora na nazwę usera i grupy */
    long const buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buflen == -1)
        return -1;

    /* Pobranie zawartości pliku passwd zawierającego informacje o UID i GID użytkownika i grupy, niezbędnych do przelogowania */
    char buf[buflen];
    struct passwd pwbuf, *pwbufp;
    if (0 != getpwnam_r(drop_to.c_str(), &pwbuf, buf, buflen, &pwbufp) || !pwbufp)
        return -1;

    int resgid = setgid(pwbufp->pw_gid); /* Zmiana uprawnień grupy (przelogowanie na grupę użytkownika) */
    int resuid = setuid(pwbufp->pw_uid); /* Zmiana uprawnień użytkownika (przelogowanie na użytkownika) */
    if ((resgid == -1) || (resuid == -1))
        return -1;
    else return 0;
}
