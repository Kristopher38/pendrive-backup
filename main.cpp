#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/fanotify.h>

#include <iostream>
#include <string>

#include "signals.h"
#include "fanotify.h"
#include "filescopy.h"
#include "settings.h"
#include "permissions.h"

/* Lista deskryptorów do odpytywania */
enum {
	FD_POLL_SIGNAL = 0,
	FD_POLL_FANOTIFY,
	FD_POLL_MAX
};

int main()
{
    /* Inicjalizacja ustawień z pliku konfiguracyjnego */
    init_settings();

    int signal_fd;                      /* Deskryptor sygnałów */
    int fanotify_fd;                    /* Deskryptor fanotify */
    struct pollfd fds[FD_POLL_MAX];     /* lista deskryptorów do odpytywania */
    bool do_exit_procedures = false;    /* sposób zakończenia programu */

    /* Ustawienie umaski na 0 aby można było tworzyć wszystkie kombinacje uprawnień na plikach i katalogach */
    umask(0);

    /* Inicjalizacja deskryptora fanotify i monitorowanych ścieżek */
    if ((fanotify_fd = initialize_fanotify()) < 0)
    {
        std::cerr<<"Couldn't initialize fanotify"<<std::endl;
        exit(EXIT_FAILURE);
    }
    else std::cout<<"Successfully initialized fanotify"<<std::endl;

    /* Inicjalizacja deskryptora sygnałów */
    if ((signal_fd = initialize_signals()) < 0)
    {
        std::cerr<<"Couldn't initialize signals"<<std::endl;
        exit(EXIT_FAILURE);
    }
    else std::cout<<"Successfully initialized signal handling"<<std::endl;

    /* Inicjalizacja ustawień kopiowania plików */
    if (initialize_filecopier() < 0)
    {
        std::cerr<<"Failed to initialize file copying mechanism"<<std::endl;
        exit(EXIT_FAILURE);
    }

	/* Schodzenie z uprawnień roota do uprawnień użytkownika ustawionego w konfiguracji */
    if (drop_root(get_user_perm_name()) < 0)
    {
        std::cerr<<"Failed to drop root previleges (does specified user exist?)"<<std::endl;
        exit(EXIT_FAILURE);
    }
    else std::cout<<"Successfully dropped root previleges"<<std::endl;

    /* Ustawienie odpytywania deskryptorów */
    fds[FD_POLL_SIGNAL].fd = signal_fd;
    fds[FD_POLL_SIGNAL].events = POLLIN;
    fds[FD_POLL_FANOTIFY].fd = fanotify_fd;
    fds[FD_POLL_FANOTIFY].events = POLLIN;

    /* Główna pętla */
    while (true)
    {
      /* Blokuj aż będzie coś do odczytania z jednego z deskryptorów */
      if (poll (fds, FD_POLL_MAX, -1) < 0)
        {
          fprintf (stderr,
                   "Couldn't poll(): '%s'\n",
                   strerror (errno));
          exit (EXIT_FAILURE);
        }

      /* Otrzymano sygnał? */
      if (fds[FD_POLL_SIGNAL].revents & POLLIN)
        {
          struct signalfd_siginfo fdsi;

            /* Odczytaj sygnał */
          if (read (fds[FD_POLL_SIGNAL].fd,
                    &fdsi,
                    sizeof (fdsi)) != sizeof (fdsi))
            {
              fprintf (stderr,
                       "Couldn't read signal, wrong size read\n");
              exit (EXIT_FAILURE);
            }

          /* Przerwij pętlę jeśli otrzymaliśmy jeden z zainicjalizowanych wcześniej sygnałów */
          if (fdsi.ssi_signo == SIGINT ||
              fdsi.ssi_signo == SIGTERM)
            {
              do_exit_procedures = false; /* wyjdź bez kopiowania i dalszych działań */
              break;
            }

            if (fdsi.ssi_signo == SIGUSR1)
            {
                do_exit_procedures = true; /* wyjdź z kopiowaniem i dalszymi działaniami */
                break;
            }

          fprintf (stderr,
                   "Received unexpected signal\n");
        }

        /* Otrzymaliśmy event fanotify? */
        if (fds[FD_POLL_FANOTIFY].revents & POLLIN)
        {
            char buffer[FANOTIFY_BUFFER_SIZE];
            ssize_t length;

            /* Odczytaj dane z deskryptora. Odczyta wszystkie dostępne eventy do wielkości bufora */
            if ((length = read (fds[FD_POLL_FANOTIFY].fd, buffer, FANOTIFY_BUFFER_SIZE)) > 0)
            {
                struct fanotify_event_metadata *metadata;

                /* Cast danych na strukturę zdarzenia dyskowego fanotify */
                metadata = (struct fanotify_event_metadata *)buffer;
                while (FAN_EVENT_OK (metadata, length))
                {
                    add_file_to_list(metadata); /* Dodaj plik do listy używanych plików */
                    close(metadata->fd);        /* Zamknij deskryptor eventu */
                    metadata = FAN_EVENT_NEXT (metadata, length);   /* Przejdź do następnego eventu */
                }
            }
        }
    }

    /* Mamy wykonać procedury końcowe? */
    if (do_exit_procedures)
    {
        /* Skopiuj pliki umieszczone na liście używanych plików */
        std::cout<<"Copying files"<<std::endl;
        copy_files();

        /* Skompresuj plik skryptem */
        std::cout<<"Compressing files"<<std::endl;
        /* Pobierz dane z konfiguracji i uruchom skrypt z odpowiednim argumentem (ścieżka folderu z backupem) */
        std::string command("sh " + app_launch_dir + "compress.sh " + pendrive_dir);
        if (system(command.c_str()) != 0)
            return (EXIT_FAILURE);  /* Wyjdź jeśli nie udało się skompresować */

        /* Jeżeli w konfiguracji jest włączone szyfrowanie plików, zaszyfruj skryptem */
        if (global_config.lookup("general.encrypt_files"))
        {
            std::cout<<"Encrypting files"<<std::endl;
            /* Uruchom skrypt z odpowiednim argumentem (ścieżka folderu z backupem) */
            std::string command("sh " + app_launch_dir + "encrypt.sh " + pendrive_dir);
            if (system(command.c_str()) != 0)
                std::cerr<<"Error occured while encrypting files"<<std::endl;
        }

        /* Jeżeli w konfiguracji jest włączone wysyłanie na serwer ftp, wyślij skryptem */
        if (global_config.lookup("general.send_to_ftp"))
        {
            std::cout<<"Sending files to ftp server"<<std::endl;
            /* Pobierz dane z konfiguracji i uruchom skrypt z odpowiednimi argumentami (dane potrzebne do połączenia i zalogowania na serwer ftp oraz ścieżka folderu z backupem) */
            std::string address = global_config.lookup("ftp.address");
            std::string port = global_config.lookup("ftp.port");
            std::string username = global_config.lookup("ftp.username");
            std::string password = global_config.lookup("ftp.password");
            std::string command("sh " + app_launch_dir + "ftp.sh " + address + " " + port + " " + username + " " + password + " " + pendrive_dir);
            if (system(command.c_str()) != 0)
                std::cerr<<"Error occured while sending files to ftp server"<<std::endl;
        }

        /* Jeżeli w konfiguracji jest włączone uruchomienie skryptu użytkownika, uruchom skrypt */
        if (global_config.lookup("general.send_to_phone"))
        {
            std::cout<<"Running user-defined script"<<std::endl;
            std::string command = "sh " + app_launch_dir + "userscript.sh";
            if (system(command.c_str()) != 0)
                std::cerr<<"Error occured while running user-defined script"<<std::endl;
        }
    }

    /* Zwolnienie zasobów */
    shutdown_fanotify(fanotify_fd);
    shutdown_signals(signal_fd);

    return EXIT_SUCCESS;
}
