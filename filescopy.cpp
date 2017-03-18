#include "filescopy.h"

using namespace libconfig;

std::string pendrive_dir;               /* Ścieżka w drzewie katalogowym w której zamontowany jest pendrive */
const std::string app_name("pbackup");  /* Nazwa procesu */
std::set<std::string> files_to_copy;    /* Lista używanych plików */

int initialize_filecopier() /* Inicjalizacja zmiennych niezbędnych do poprawnego kopiowania plików */
{
    std::string copy_dir = global_config.lookup("general.copy_directory");
    pendrive_dir = app_launch_dir + copy_dir + "/";
    return 0;
}

/*std::string get_program_name_from_pid(int pid)  /* Zwraca nazwę programu na podstawie id procesu */
/*{
    std::string program_name;                                                                   /* nazwa programu */
/*    std::string path = std::string("/proc/") + std::to_string(pid) + std::string("/cmdline");   /* ścieżka do pliku z nazwą programu */

    /* otwarcie pliku */
/*    std::ifstream procfile(path, std::ifstream::in);
    if (procfile.good())
    {
        /* pobranie nazwy programu z pliku i odpowiednie jej przetworzenie */
/*        procfile>>program_name;
        if (program_name.length() > 0)
        {
            std::size_t pos = program_name.find('\0');
            if (pos != std::string::npos)
                program_name.erase(pos, std::string::npos);
            pos = program_name.find("^@");
            if (pos != std::string::npos)
                program_name.erase(pos, std::string::npos);
        }
    }
	return program_name;
}*/

std::string get_program_name_from_pid (int pid)
{
    char buffer[PATH_MAX];
	int fd;
	ssize_t len;
	char *aux;

	/* Try to get program name by PID */
	sprintf(buffer, "/proc/%d/cmdline", pid);
	if ((fd = open(buffer, O_RDONLY)) < 0)
		return std::string();

	/* Read file contents into buffer */
	if ((len = read(fd, buffer, PATH_MAX - 1)) <= 0)
	{
		close(fd);
		return std::string();
	}
	close(fd);

	buffer[len] = '\0';
	aux = strstr(buffer, "^@");
	if (aux)
		*aux = '\0';
    try {
        return std::string(buffer);
    } catch (const std::exception& e){
        return std::string();
    }
}

std::string get_file_path_from_fd(int fd) /* Zwraca ścieżkę pliku na podstawie jego deskryptora */
{
    ssize_t len;
    char buffer[PATH_MAX];

	if (fd <= 0)
		return NULL;

    /* utwórz ścieżkę do symlinku do pliku */
	sprintf(buffer, "/proc/self/fd/%d", fd);
	/* odczytaj zawartość symlinku (ścieżkę na którą wskazuje symlink) */
	if ((len = readlink(buffer, buffer, PATH_MAX - 1)) < 0)
		return NULL;

	buffer[len] = '\0';
	return std::string(buffer);
}

bool filter_out(std::string filter_config, std::string text_to_match)   /* Filtrowanie eventów dyskowych na podstawie nazw plików lub nazw programów */
{
    /* Inicjalizacja typu filtra z pliku konfiguracyjnego */
    enum FILTER_TYPE {BLACKLIST = 0, WHITELIST = 1};
    FILTER_TYPE filter;
    std::string filtering_behaviour = global_config.lookup(std::string(filter_config + ".filtering_behavior"));
    if (filtering_behaviour == "whitelist")
        filter = WHITELIST;
    else if (filtering_behaviour == "blacklist")
        filter = BLACKLIST;
    else filter = BLACKLIST;

    Setting& names = global_config.lookup(std::string(filter_config + ".filter_list"));
    /* Pętla przechodząca przez wszystkie wyrażenia regularne z pliku konfiguracyjnego */
    for (int i = 0; i < names.getLength(); ++i)
    {
        try
        {
            /* Sprawdź czy nazwa pasuje do wyrażenia regularnego */
            std::regex name_regex(names[i]);
            bool regex_matched = std::regex_match(text_to_match, name_regex);
            if (regex_matched && filter == BLACKLIST)       /* Jeśli nazwa jest na blackliście, odfiltruj */
                return true;
            else if (regex_matched && filter == WHITELIST)  /* Jeśli nazwy nie ma na blackliście, nie odfiltrowywuj */
                return false;
        }
        /* Łapanie błędów regexa, np. błędych wyrażeń regularnych */
        catch (const std::regex_error& e)
        {
            std::string name_re = names[i];
            std::cerr<<"Warning, regex "<<name_re<<" is invalid, and won't be used in the future"<<std::endl;
            names.remove(i);
        }
    }
    if (filter == BLACKLIST)        /* Jeśli nie znaleziono nazwy na blackliście, nie odfiltrowywuj */
        return false;
    else if (filter == WHITELIST)   /* Jeśli nie znaleziono nazwy na whiteliście, odfiltruj */
        return true;

    return false;
}

bool is_directory(std::string path) /* Sprawdza czy podana ścieżka jest katalogiem */
{
    /* Usuwa / na końcu ścieżki - wymagane do poprawnego działania */
    if (path.back() == '/')
        path.pop_back();

    struct stat stat_path;
    if (stat(path.c_str(), &stat_path) == 0) /* Pobierz metadane o pliku */
        if (S_ISDIR(stat_path.st_mode))      /* Sprawdź czy jest katalogiem */
            return true;
        else return false;
    else
        return false;
    return false;
}

bool is_small(std::string path) /* sprawdza czy wielkość pliku jest mniejsza niż ta ustawiona w konfiguracji w general.copy_immediately_max_size */
{
    /* sprawdź czy jest katalogiem */
    if (is_directory(path))
        return false;

    int64_t small_size = static_cast<int64_t>(global_config.lookup("general.copy_immediately_max_size"));

    /* Jeśli ujemny, nigdy nie kopiuj natychmiast */
    if (small_size < 0)
        return false;

    /* Jeśli zero, zawsze kopiuj natychmiast */
    if (small_size == 0)
        return true;

    /* Jeśli większa od zera */
    struct stat stat_path;
    if (stat(path.c_str(), &stat_path) == 0) /* pobierz metadane o pliku */
        if (stat_path.st_size <= static_cast<int64_t>(global_config.lookup("general.copy_immediately_max_size"))) /* sprawdź czy wielkość pliku jest mniejsza lub równa od tej w konfiguracji */
            return true;
    return false;
}

bool is_child(std::string parent, std::string child) /* sprawdza czy ścieżka 'child' jest w katalogu 'parent' */
{
    /* obetnij długość ścieżki 'child' do długości ścieżki 'parent' i sprawdź czy są takie same */
    child = child.substr(0, parent.length());
    if (child == parent)
        return true;
    else return false;
}

std::string target_path(std::string source_path)    /* zwraca pełną absolutną ścieżkę do której mają być kopiowane pliku */
{
    return pendrive_dir + source_path.substr(1, std::string::npos); /* łączy ścieżkę pamięci masowej ze ścieżką z której pochodzi event dyskowy */
}

void copy_file(std::string from, std::string to) /* kopiuje pojedynczy plik ze ścieżki 'from' do ścieżki 'to' */
{
    int source_file = 0;

    /* otwórz plik ze ścieżki 'from' w trybie odczytu */
    if ((source_file = open(from.c_str(), O_RDONLY)) > -1)
    {
        int target;
        struct stat stat_source;
        fstat(source_file, &stat_source); /* Pobierz metadane o pliku ze ścieżki 'from' */

        /* Jeśli program ma zachowywać uprawnienia, plików, utwórz plik w ścieżce 'to' z uprawnieniami pliku ze ścieżki 'from', w przeciwnym razie po prostu utwórz, w trybie zapisu */
        if (global_config.lookup("general.preserve_permissions"))
            target = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, stat_source.st_mode);
        else target = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC);

        if (target == -1)
            fprintf(stderr, "Error open(): %s, path: %s\n", strerror(errno), to.c_str());
        else
        {
            /* Skopiuj plik ze ścieżki 'from' do pliku o ścieżce 'to' */
            int ret = sendfile64(target, source_file, 0, stat_source.st_size);
            if (ret == -1)
                fprintf(stderr, "Error sendfile(): %s, path: %s\n", strerror(errno), to.c_str());
            else if (ret != stat_source.st_size)
            {
                std::cerr<<"Error sendfile(): count of copied bytes mismatches the file size"<<std::endl;
                std::cerr<<"File size: "<<stat_source.st_size<<", bytes written: "<<ret<<std::endl;
            }
        }

        /* zamknij deskryptory obu plików */
        close(target);
        close(source_file);
    }
    else std::cout<<"Can't open source file: "<<strerror(errno)<<", path: "<<from<<std::endl;
}

void make_dirs(std::string source_path) /* Tworzy strukturę katalogów na pamięci masowej odzwierciedlającą tą ze ścieżki z której kopiowany jest plik */
{
    /* Zignoruj event dyskowy jeśli pochodzi z pamięci masowej */
    if (source_path.find(pendrive_dir) != std::string::npos)
        return;

    std::string source_path_nofile = source_path.substr(1, source_path.rfind('/')); /* Usuń slash katalogu root na początku i nazwę pliku ze ścieżki */

    std::size_t pos = 0;
    struct stat st;
    std::string current_dir;
    std::string source_dir;

    /* Przeiteruj przez katalogi w ścieżce aby sprawdzić czy już istnieją na pendrivie */
    while (pos != std::string::npos)
    {
        pos = source_path_nofile.find('/', pos+1);      /* pos + 1 żeby znaleźć nazwę kolejnego katalogu */
        current_dir = pendrive_dir + source_dir;        /* Ścieżka pamięci masowej z dołączonym katalogiem którego istnienie w danym momencie sprawdzamy */
        source_dir = source_path_nofile.substr(0, pos); /* Źródłowa ścieżka katalogu który w danym momencie przetwarzamy */

        /* Pobierz metadane katalogu docelowego, jeśli katalog nie istnieje, utwórz */
        if (stat(current_dir.c_str(), &st) == -1)
        {
            int original_dir_st = stat(std::string("/"+source_dir).c_str(), &st); /* Pobierz metadane katalogu źródłowego */
            if (original_dir_st == -1)
            {
                fprintf(stderr, "Error stat(): %s, path: %s\n", strerror(errno), std::string("/"+source_dir).c_str());
                continue;
            }

            /* Utwórz katalog z takimi samymi uprawnieniami katalog źródłowy */
            if (mkdir(current_dir.c_str(), st.st_mode) < 0)
                std::cerr<<"Error mkdir(): "<<strerror(errno)<<", path: "<<current_dir<<std::endl;
        }
    }
}

bool is_allowed_by_paths(std::string source_path)   /* Sprawdza czy dana ścieżka jest dopuszczona przez filtr ścieżek w konfiguracji */
{
    const unsigned path_count = 3;
    const std::string path_order[path_count] = {"mounts", "files_and_dirs", "directory_trees"}; /* określa kolejność sprawdzania typów ścieżek, directory_trees zawsze muszą być sprawdzane na końcu */

    /* przeiteruj przez powyższe typy ścieżek */
    for (unsigned i = 0; i < path_count; ++i)
    {
        Setting& path = global_config.lookup(std::string("monitoring.") + path_order[i]); /* Pobierz tablicę z danym typem ścieżek */

        /* Jeśli jest niepusta */
        if (path.getLength() > 0)
        {
            /* Przeiteruj przez każdą ścieżkę */
            for (int j = 0; j < path.getLength(); ++j)
            {
                std::string parent_path = path[j];
                if (is_child(parent_path, source_path)) /* Jeśli plik jest dzieckiem jakiejś ścieżki, nie odfiltrowywuj go */
                    return true;
            }
        }
    }
    return false;   /* Jeśli plik nie jest dzieckiem żadnej ze ścieżek, odfiltruj go */
}

void add_file_to_list(const fanotify_event_metadata* metadata)  /* Przepuszcza plik przez zestaw filtrów i ewentualnie dodaje plik do listy używanych plików lub kopiuje go od razu na pamięć masową */
{
    std::string source_path = get_file_path_from_fd(metadata->fd);  /* pobierz ścieżkę pliku */

    /* Inicjalizacja typu filtrowania - soft lub hard, więcej informacji w dokumentacji pliku konfiguracyjnego */
    enum FILTER_SOFTNESS {FILTER_SOFT, FILTER_HARD};
    FILTER_SOFTNESS softness;
    std::string filter_softness = global_config.lookup("filtering.filter");
    if (filter_softness == "soft")
        softness = FILTER_SOFT;
    else if (filter_softness == "hard")
        softness = FILTER_HARD;
    else softness = FILTER_HARD;

    bool filter_extension = false;
    bool filter_program = false;

    /* Odfiltruj event jeśli ścieżka jest katalogiem, pochodzi z pamięci masowej lub jest wygenerowana przez sam program */

    std::string program_name = get_program_name_from_pid(metadata->pid);
    program_name = program_name.substr(program_name.rfind('/') + 1, std::string::npos);
    if (is_directory(source_path) || source_path.find(pendrive_dir) != std::string::npos || program_name == app_name || program_name == app_launch_dir + app_name)
        return;

    /* Odfiltruj event na podstawie filtra nazw plików z konfiguracji */
    if (global_config.lookup("filtering.extensions.filter_list").getLength() > 0)
    {
        std::string filename = source_path.substr(source_path.find_last_of("/") + 1, std::string::npos);
        filter_extension = filter_out("filtering.extensions", filename);
    }

    /* Odfiltruj event na podstawie filtra nazw programów z konfiguracj */
    if (global_config.lookup("filtering.programs.filter_list").getLength() > 0)
    {
        std::string program_name = get_program_name_from_pid(metadata->pid);
        filter_program = filter_out("filtering.programs", program_name);
    }

    /* Jeśli filtr jest w trybie hard, wystarczy że jeden filtr odrzuci event aby całkowicie odfiltrować event */
    if (softness == FILTER_HARD && (filter_extension || filter_program))
        return;
    /* Jeśli filtr jest w trybie soft, wymagane jest aby obydwa filtry odrzuciły event aby całkowicie odfiltrować event */
    if (softness == FILTER_SOFT && (filter_extension && filter_program))
        return;

    /* Odfiltruj event na podstawie filtra dozwolonych ścieżek */
    if (!is_allowed_by_paths(source_path))
        return;

    std::cout<<"Event triggered: "<<source_path<<std::endl;

    /* skopiuj od razu zamiast dodawania do listy, jeśli plik jest wystarczająco mały (wartość ustalana w konfiguracji) */
    if (is_small(source_path))
    {
        make_dirs(source_path); /* utwórz wymagane katalogi */
        copy_file(source_path, target_path(source_path)); /* skopiuj plik */
    }
    /* w przeciwnym razie, dodaj do listy używanych plików */
    else
        files_to_copy.insert(source_path);
}

void copy_files() /* Kopiuje pliki z listy używanych plików na pamięć masową */
{
    /* przeiteruj przez całą listę */
    for (std::set<std::string>::iterator it = files_to_copy.begin(); it != files_to_copy.end(); ++it)
    {
        std::string source_path = *it;
        std::string target_path = pendrive_dir + source_path.substr(1, std::string::npos); /* ścieżka pamięci masowej z dołączoną pełną ścieżką pliku (substr usuwa root slasha / na początku ścieżki pliku) */
        make_dirs(source_path);                 /* utwórz katalogi odzwierciedlające strukturą tę na dysku */
        copy_file(source_path, target_path);    /* skopiuj plik na pamięć masową */
    }
}
