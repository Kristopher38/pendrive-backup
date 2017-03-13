#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/fanotify.h>

#include <iostream>
#include <string>

#include "encryption.h"
#include "signals.h"
#include "fanotify.h"
#include "filescopy.h"
#include "settings.h"
#include "permissions.h"
#include "ftp.h"

/* Lista deskryptorów do odpytywania */
enum {
	FD_POLL_SIGNAL = 0,
	FD_POLL_FANOTIFY,
	FD_POLL_MAX
};

int
main(int          argc,
	const char **argv)
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
    if (initialize_filecopier(argc, argv) < 0)
        exit(EXIT_FAILURE);

    //password_process();
    encryption_key = "temppass";

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


        std::cout<<"Compressing files"<<std::endl;
        std::string backup_folder = global_config.lookup("general.copy_directory");
        std::string command("sh compress.sh " + backup_folder);
        int ret = system(command.c_str());
        std::cout<<"Compression script exited with exit code "<<ret<<std::endl;
        if (ret == 2)
        {
            std::cerr<<"Fatal error occured, couldn't compress files, exiting"<<std::endl;
            return (EXIT_FAILURE);
        }

        bool do_encrypt = global_config.lookup("general.encrypt_files");
        bool do_ftp = global_config.lookup("general.send_to_ftp");
        bool do_userscript = global_config.lookup("general.send_to_phone");

        /* Jeżeli w konfiguracji jest włączone szyfrowanie plików, zaszyfruj */
        if (do_encrypt)
        {
            std::cout<<"Encrypting files"<<std::endl;
            std::string command("sh encrypt.sh " + backup_folder);
            if (system(command.c_str()) != 0)
                std::cerr<<"Error occured while encrypting files"<<std::endl;
        }

        /* Jeżeli w konfiguracji jest włączone wysyłanie na serwer ftp, wyślij */
        if (do_ftp)
        {
            std::cout<<"Sending files to ftp server"<<std::endl;
            std::string command("sh ftp.sh 192.168.1.110 21 pi usiatko85 " + backup_folder);
            if (system(command.c_str()) != 0)
                std::cerr<<"Error occured while sending files to ftp server"<<std::endl;
        }

        /* Jeżeli w konfiguracji jest włączone uruchomienie skryptu użytkownika, uruchom */
        if (do_userscript)
        {
            std::cout<<"Running user-defined script"<<std::endl;
            system("sh userscript.sh");
        }
    }

    /* Zwolnienie zasobów */
    shutdown_fanotify(fanotify_fd);
    shutdown_signals(signal_fd);

    return EXIT_SUCCESS;
}
