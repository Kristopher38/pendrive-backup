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
/* Inicjalizuje maskę eventów fanotify, czyli na jakie zdarzenia dyskowe program ma reagować
   Dostępne zdarzenia to: dostęp, otwarcie, modyfikacja, lub zamknięcie pliku (z podziałem na
   zamknięcie pliku otwartego w trybie zapisu, lub w trybie odczytu)
   Zwraca maskę do wykorzystania przy dodawaniu subskrypcji fanotify na dany obiekt systemu plików
*/

void shutdown_fanotify (int fanotify_fd);
/* Zwalnia zasoby używane przez fanotify
   Argumenty:
    int fanotify_fd - deskryptor fanotify do zamknięcia
*/

int initialize_fanotify();
/* Inicjalizuje deskryptor fanotify, tworząc i dodając subskrypcje na określone obiekty
   systemu plików
   Zwraca deskryptor do fanotify device, lub -1 w przypadku błędu inicjalizacji.
*/

#endif // H_FANOTIFY
