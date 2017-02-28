#ifndef H_ENCRYPTION
#define H_ENCRYPTION

#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/fanotify.h>
#include <mcrypt.h>
#include <regex>
#include <pwd.h>

#include <fstream>
#include "filescopy.h"

extern char* encryption_key;

void password_process();
bool replace(std::string& str, const std::string& from, const std::string& to);
char* encrypt_password(char* IV, char* key_org, char* buffer, bool is_decrypt);
int encrypt(char* IV, char* key_org, const char* path, bool isDecrypt);
void crypt_file(const char* file, bool is_decrypt);
void get_directory(const char* directory, bool is_decrypt);
void crypt_files(bool is_decrypt, std::string directory);
char *trim(char *str);

#endif // H_ENCRYPTION
