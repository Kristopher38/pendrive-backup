#ifndef H_FANOTIFY
#define H_FANOTIFY

#include <string.h> // strerror
#include <unistd.h> // close
#include <sys/fanotify.h> // fanotify
#include <sys/stat.h> // stat
#include <fcntl.h> // open

#include <iostream>

#include <libconfig.h++>

extern libconfig::Config global_config;

uint64_t initialize_event_mask();
void shutdown_fanotify (int fanotify_fd);
int initialize_fanotify();

#endif // H_FANOTIFY
