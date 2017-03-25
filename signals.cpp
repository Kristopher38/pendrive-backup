#include "signals.h"

void shutdown_signals (int signal_fd) /* Zwrot zasobów systemowych */
{
	close(signal_fd); /* Zamknięcie deskryptora sygnałów */
}

int initialize_signals() /* Inicjalizacja sygnałów, zwraca deskryptor do odczytu sygnałów */
{
	int signal_fd;
	sigset_t sigmask;

	/* Dodajemy do maski blokowania sygnały SIGINT, SIGTERM i SIGUSR1 (do obsługi poprawnego wychodzenia z kopiowaniem, szyfrowaniem, etc.) */
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGUSR1);

    /* Blokujemy sygnały */
	if (sigprocmask(SIG_BLOCK, &sigmask, NULL) < 0)
	{
        std::cerr<<"Couldn't block signals: "<<strerror(errno)<<std::endl;
		return -1;
	}

	/* Pobieramy deskryptor z którego będziemy czytać przychodzące sygnały */
	if ((signal_fd = signalfd(-1, &sigmask, 0)) < 0)
	{
		std::cerr<<"Couldn't setup signal FD: "<<strerror(errno)<<std::endl;
		return -1;
	}

    /* Zwrócenie deskryptora sygnałów */
	return signal_fd;
}
