#include "permissions.h"

std::string get_user_perm_name()
{
    std::ifstream userfile("userperm", std::ifstream::in);
    std::string user;
    if (userfile.good())
        userfile>>user;
    else {
        std::cerr<<"I/O error while reading user permission file (does userperm file exists?)"<<std::endl;
        return std::string();
    }
    return user;
}

int drop_root(std::string drop_to)
{
    if (drop_to.length() == 0)
        return -1;
    long const buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buflen == -1)
        return -1;
    // requires c99
    char buf[buflen];
    struct passwd pwbuf, *pwbufp;
    if (0 != getpwnam_r(drop_to.c_str(), &pwbuf, buf, buflen, &pwbufp) || !pwbufp)
        return -1;

    int resgid = setgid(pwbufp->pw_gid);
    int resuid = setuid(pwbufp->pw_uid);
    if ((resgid == -1) || (resuid == -1))
        return -1;
    else return 0;
}
