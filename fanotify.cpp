#include "fanotify.h"

uint64_t initialize_event_mask() /* Zwraca maskę monitorowanych eventów dyskowych */
{
    uint64_t event_mask = 0;

    /* Sprawdzanie każdego eventu czy jest włączony w configu i ORowanie z finalną maską */
    if (global_config.lookup("monitored_events.access"))
        event_mask |= FAN_ACCESS;
    if (global_config.lookup("monitored_events.open"))
        event_mask |= FAN_OPEN;
    if (global_config.lookup("monitored_events.modify"))
        event_mask |= FAN_MODIFY;
    if (global_config.lookup("monitored_events.close_write"))
        event_mask |= FAN_CLOSE_WRITE;
    if (global_config.lookup("monitored_events.close_nowrite"))
        event_mask |= FAN_CLOSE_NOWRITE;
    if (global_config.lookup("monitored_events.close"))
        event_mask |= FAN_CLOSE;

    return event_mask;
}

void shutdown_fanotify (int fanotify_fd) /* Zamknięcie fanotify, zwrot zasobów systemowych */
{
    close(fanotify_fd); /* Zamknięcie deskryptora fanotify */
}

int initialize_fanotify() /* Inicjalizacja fanotify i monitorowanych ścieżek, zwraca deskryptor fanotify do odczytu eventów dyskowych */
{
    /* Sprawdź czy listy mountów, plików i katalogów do monitorowania są puste */
    if (global_config.lookup("monitoring.mounts").getLength() == 0 && global_config.lookup("monitoring.files_and_dirs").getLength() == 0 && global_config.lookup("monitoring.directory_trees").getLength() == 0)
    {
        std::cerr<<"Monitoring lists are empty, edit \"monitoring\" section in config.cfg"<<std::endl;
        return -1;
    }

    /* Utwórz fanotify */
    int fanotify_fd = fanotify_init(FAN_CLOEXEC | FAN_NONBLOCK, O_RDONLY | O_CLOEXEC | O_LARGEFILE | O_NOATIME);
    if (fanotify_fd < 0)
    {
        fprintf (stderr, "Couldn't setup new fanotify device: %s\n", strerror (errno));
        return -1;
    }

    /* Pobierz maskę monitorowanych eventów dyskowych */
    uint64_t event_mask = initialize_event_mask();

    /* Dodaj monitorowane mountpointy */
    for (int i = 0; i < global_config.lookup("monitoring.mounts").getLength(); ++i)
    {
        const char* monitored_mount = global_config.lookup("monitoring.mounts")[i]; /* Pobierz ścieżkę z konfiguracji */
        /* Dodaj monitorowanie na mountpoint */
        if (fanotify_mark(fanotify_fd, FAN_MARK_ADD | FAN_MARK_MOUNT, event_mask, 0, monitored_mount) < 0)
            std::cerr<<"Couldn't add monitor on mount "<<monitored_mount<<": "<<strerror(errno)<<std::endl;
        else std::cout<<"Started monitoring mount "<<monitored_mount<<std::endl;
    }
    /* Dodaj monitorowane pliki i katalogi */
    for (int i = 0; i < global_config.lookup("monitoring.files_and_dirs").getLength(); ++i)    // add files and directories to be monitored
    {
        const char* monitored_file = global_config.lookup("monitoring.files_and_dirs")[i];  /* Pobierz ścieżkę z konfiguracji */
        /* Dodaj monitorowanie na plik/katalog */
        if (fanotify_mark(fanotify_fd, FAN_MARK_ADD, event_mask | FAN_EVENT_ON_CHILD, 0, monitored_file) < 0)
            std::cerr<<"Couldn't add monitor on file or directory "<<monitored_file<<": "<<strerror(errno)<<std::endl;
        else std::cout<<"Started monitoring file or directory "<<monitored_file<<std::endl;
    }
    /* Dodaj monitorowane drzewa katalogów */
    for (int i = 0; i < global_config.lookup("monitoring.directory_trees").getLength(); ++i)    // add files and directories to be monitored
    {
        const char* monitored_dirtree = global_config.lookup("monitoring.directory_trees")[i];  /* Pobierz ścieżkę z konfiguracji */
        /* Dodaj monitorowanie na drzewo katalogowe (tak naprawdę mountpoint, fanotify nie obsługuje monitorowania drzew katalogowych poza całymi mountami, funkcjonalność ta jest obsługiwana podczas filtrowania w funkcji add_file_to_list */
        if (fanotify_mark(fanotify_fd, FAN_MARK_ADD | FAN_MARK_MOUNT, event_mask, 0, monitored_dirtree) < 0)
            std::cerr<<"Couldn't add monitor on directory tree "<<monitored_dirtree<<": "<<strerror(errno)<<std::endl;
        else std::cout<<"Started monitoring directory tree "<<monitored_dirtree<<std::endl;
    }

    return fanotify_fd;
}
