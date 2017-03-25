#ifndef H_SIGNALS
#define H_SIGNALS

#include <sys/signal.h> // signals
#include <unistd.h> // close
#include <string.h> // strerror
#include <sys/signalfd.h> // signalfd

#include <iostream>

void shutdown_signals (int signal_fd);
/* Zwalnia zasoby używane przez deskryptor obsługi sygnałów
   Argumenty:
    int signal_fd - deskryptor sygnałów do zamknięcia
*/

int initialize_signals();
/* Inicjalizuje obsługę sygnałów niezbędnych do poprawnego działania
   programu (SIGINT i SIGTERM do poprawnego zwolnienia zasobów, SIGUSR1
   do poprawnej obsługi wykonania czynności końcowych)
   Zwraca deskryptor do odczytu sygnałów, lub -1 w przypadku błędu inicjalizacji.
*/

#endif // H_SIGNALS
