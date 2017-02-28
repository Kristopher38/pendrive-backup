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

/* Enumerate list of FDs to poll */
enum {
	FD_POLL_SIGNAL = 0,
	FD_POLL_FANOTIFY,
	FD_POLL_MAX
};

int
main(int          argc,
	const char **argv)
{
    init_settings();

    int signal_fd;
    int fanotify_fd;
    struct pollfd fds[FD_POLL_MAX];
    bool do_exit_procedures = false;

    umask(0); // set umask to 0 to be able to create all combinations of permissions on files and directories

    /* Initialize fanotify FD and the marks */
    if ((fanotify_fd = initialize_fanotify()) < 0)
    {
        std::cerr<<"Couldn't initialize fanotify"<<std::endl;
        exit(EXIT_FAILURE);
    }
    else std::cout<<"Successfully initialized fanotify"<<std::endl;

    /* Initialize signals FD */
    if ((signal_fd = initialize_signals()) < 0)
    {
        std::cerr<<"Couldn't initialize signals"<<std::endl;
        exit(EXIT_FAILURE);
    }
    else std::cout<<"Successfully initialized signal handling"<<std::endl;

    if (initialize_filecopier(argc, argv) < 0)
        exit(EXIT_FAILURE);

    password_process();

	/* Drop root previleges to desired user */
    if (drop_root(get_user_perm_name()) < 0)
    {
        std::cerr<<"Failed to drop root previleges (does specified user exist?)"<<std::endl;
        exit(EXIT_FAILURE);
    }
    else std::cout<<"Successfully dropped root previleges"<<std::endl;

    /* Setup polling */
    fds[FD_POLL_SIGNAL].fd = signal_fd;
    fds[FD_POLL_SIGNAL].events = POLLIN;
    fds[FD_POLL_FANOTIFY].fd = fanotify_fd;
    fds[FD_POLL_FANOTIFY].events = POLLIN;

  /* Now loop */
  for (;;)
    {
      /* Block until there is something to be read */
      if (poll (fds, FD_POLL_MAX, -1) < 0)
        {
          fprintf (stderr,
                   "Couldn't poll(): '%s'\n",
                   strerror (errno));
          exit (EXIT_FAILURE);
        }

      /* Signal received? */
      if (fds[FD_POLL_SIGNAL].revents & POLLIN)
        {
          struct signalfd_siginfo fdsi;

          if (read (fds[FD_POLL_SIGNAL].fd,
                    &fdsi,
                    sizeof (fdsi)) != sizeof (fdsi))
            {
              fprintf (stderr,
                       "Couldn't read signal, wrong size read\n");
              exit (EXIT_FAILURE);
            }

          /* Break loop if we got the expected signal */
          if (fdsi.ssi_signo == SIGINT ||
              fdsi.ssi_signo == SIGTERM)
            {
              do_exit_procedures = false;
              break;
            }

            if (fdsi.ssi_signo == SIGUSR1)
            {
                do_exit_procedures = true;
                break;
            }

          fprintf (stderr,
                   "Received unexpected signal\n");
        }

      /* fanotify event received? */
        if (fds[FD_POLL_FANOTIFY].revents & POLLIN)
        {
            char buffer[FANOTIFY_BUFFER_SIZE];
            ssize_t length;

            /* Read from the FD. It will read all events available up to
            * the given buffer size. */
            if ((length = read (fds[FD_POLL_FANOTIFY].fd, buffer, FANOTIFY_BUFFER_SIZE)) > 0)
            {
                struct fanotify_event_metadata *metadata;

                metadata = (struct fanotify_event_metadata *)buffer;
                while (FAN_EVENT_OK (metadata, length))
                {
                    add_file_to_list(metadata);
                    close(metadata->fd);
                    metadata = FAN_EVENT_NEXT (metadata, length);
                }
            }
        }
    }

    if (do_exit_procedures)
    {
        std::cout<<"Copying files"<<std::endl;
        copy_files();

        if (global_config.lookup("general.encrypt_files"))
        {
            std::cout<<"Encrypting files"<<std::endl;
            crypt_files(false, pendrive_dir);
        }

        if (global_config.lookup("general.send_to_ftp"))
        {
            // std::cout<<"Sending files to ftp server"<<std::endl;
            // send_to_ftp();
        }

        if (global_config.lookup("general.send_to_phone"))
        {
            // std::cout<<"Sending files to phone"<<std::endl;
            // send_to_phone();
        }
    }

  /* Clean exit */
  shutdown_fanotify (fanotify_fd);
  shutdown_signals (signal_fd);

  printf ("Exiting fanotify example...\n");

  return EXIT_SUCCESS;
}
