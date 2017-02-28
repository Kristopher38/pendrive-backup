#include "signals.h"

void shutdown_signals (int signal_fd)
{
	close(signal_fd);
}

int initialize_signals (void)
{
	int signal_fd;
	sigset_t sigmask;

	/* We want to handle SIGINT and SIGTERM in the signal_fd, so we block them. */
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGUSR1);

	if (sigprocmask(SIG_BLOCK, &sigmask, NULL) < 0)
	{
        std::cerr<<"Couldn't block signals: "<<strerror(errno)<<std::endl;
		return -1;
	}

	/* Get new FD to read signals from it */
	if ((signal_fd = signalfd(-1, &sigmask, 0)) < 0)
	{
		std::cerr<<"Couldn't setup signal FD: "<<strerror(errno);
		return -1;
	}

	return signal_fd;
}
