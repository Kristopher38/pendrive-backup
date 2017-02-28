#ifndef H_SIGNALS
#define H_SIGNALS

#include <sys/signal.h> // signals
#include <unistd.h> // close
#include <string.h> // strerror
#include <sys/signalfd.h> // signalfd

#include <iostream>

void shutdown_signals (int signal_fd);
int initialize_signals (void);

#endif // H_SIGNALS
