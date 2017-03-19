#ifndef H_FILESCOPY
#define H_FILESCOPY

#include <sys/fanotify.h> // fanotify
#include <sys/stat.h> // stat
#include <limits.h> // PATH_MAX
#include <unistd.h> // readlink
#include <fcntl.h> // open
#include <string.h> // strerror
#include <sys/sendfile.h> // sendfile64

#include <string>
#include <set>
#include <fstream>
#include <regex>
#include <iostream>

#include "settings.h"

/* Size of buffer to use when reading fanotify events */
#define FANOTIFY_BUFFER_SIZE 16384

extern std::string pendrive_dir;

int initialize_filecopier();
std::string get_program_name_from_pid(int pid);
std::string get_file_path_from_fd(int fd);
bool filter_out(std::string filter_config, std::string text_to_match);
bool is_directory(std::string path);
bool is_small(std::string path);
bool is_child(std::string parent, std::string child);
mode_t interpret_string(std::string permissions);
std::string target_path(std::string source_path);
void copy_file(std::string from, std::string to);
void make_dirs(std::string source_path);
bool is_allowed_by_paths(std::string source_path);
void add_file_to_list(const fanotify_event_metadata* metadata);
void copy_files();

#endif // H_FILESCOPY
