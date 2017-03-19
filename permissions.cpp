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

    /* Workaround w celu kompatybilności z każdą instalacją systemu (nie można statycznie zlinkować glibc potrzebnego do getpwnam_r) */
    /* Wykonaj komendę getent passwd nazwa_użytkownika | cut -d ":" --fields=3,4 aby otrzymać uid i gid użytkownika o danej nazwie sformatowane w postaci uid:gid */
    std::string command("getent passwd " + drop_to + " | cut -d \":\" --fields=3,4");
    FILE* fp = popen(command.c_str(), "r");
    if (fp == NULL)
        return -1;

    int uid;
    int gid;
    /* Odczytaj uid i gid z deskryptora komendy (zparsuj output komendy) */
    if (fscanf(fp, "%d:%d", &uid, &gid) != 2)
        return -1;

    int resgid = setgid(gid); /* Zmiana uprawnień grupy (przelogowanie na grupę użytkownika) */
    int resuid = setuid(uid); /* Zmiana uprawnień użytkownika (przelogowanie na użytkownika) */
    if ((resgid == -1) || (resuid == -1))
        return -1;
    else return 0;
}
