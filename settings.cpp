#include "settings.h"

using namespace libconfig;

Config global_config;
std::string app_launch_dir;

/* Sprawdza w konfiguracji 'cfg' czy dana opcja o nazwie 'name' i typie 'val_type' istnieje (a jeśli jest listą, to czy typ jej elementów jest taki sam jak 'list_type'), jeśli nie to usuwa wadliwą opcję i tworzy nową o właściwym typie z wartością domyślną 'value'. Zwraca true w przypadku konieczności "naprawienia" opcji, false gdy opcja była zdefiniowana poprawnie */
template <typename T=int> bool check_and_make_setting(Config& cfg, std::string name, Setting::Type val_type, T value, Setting::Type list_type)
{
    /* Sprawdzenie czy opcja istnieje, czy jest właściwego typu, i czy elementy listy (jeśli jest listą) są właściwego typu - jeśli coś jest nie tak, kontynuuj */
    if (!(cfg.exists(name) && cfg.lookup(name).getType() == val_type) ||
        (val_type == Setting::TypeArray && cfg.lookup(name).getLength() > 0 && cfg.lookup(name)[0].getType() != list_type))
    {
        std::cerr<<name<<" setting not specified or wrong type, using default value"<<std::endl;

        std::size_t pos = name.find_last_of(".", std::string::npos);
        /* Jeśli opcja nie ma rodzica */
        if (pos == std::string::npos)
        {
            /* usuń wadliwą opcję */
            try {
                cfg.getRoot().remove(name);
            } catch (const SettingNotFoundException& e) {} /* ciche łapanie ewentualnych wyjątków o nieistnieniu opcji, nie obchodzi nas czy opcja istnieje czy nie, ma zostać usunięta */
            cfg.getRoot().add(name, val_type); /* i utwórz ją z poprawnym typem */
        }
        /* Jeśli opcja ma rodzica */
        else
        {
            std::string group = name.substr(0, pos); /* ścieżka rodzica opcji */
            std::string setting = name.substr(pos+1, std::string::npos); /* nazwa opcji */
            /* Usuń wadliwą opcję */
            try {
                cfg.lookup(group).remove(setting);
            } catch (const SettingNotFoundException& e) {} /* ciche łapanie wyjątków jw. */
            cfg.lookup(group).add(setting, val_type); /* i utwórz ją z poprawnym typem */
        }

        try {
            /* Sprawdzenie czy opcja nie jest grupą, tablicą lub listą i nadanie jej domyślnej wartości */
            if (!(val_type == Setting::TypeGroup || val_type == Setting::TypeArray || val_type == Setting::TypeList))
                cfg.lookup(name) = value;
        } catch (const SettingTypeException& e) {
            /* Wyjątek rzucany jako informacja dla programisty o błędnym typie wartości domyślnej dla danej opcji */
            std::cerr<<"Warning: no setting initial value specified or value with wrong type supplied to "<<name<<std::endl;
            throw e;
        }
        return true;
    }
    return false;
}

int init_settings() /* Inicjalizacja konfiguracji programu */
{
    /* ustaw ścieżkę z której jest uruchomiony program */
    ssize_t len;
    char buffer[PATH_MAX];
	/* odczytaj zawartość symlinku (ścieżkę na którą wskazuje symlink) */
	if ((len = readlink("/proc/self/exe", buffer, PATH_MAX - 1)) < 0)
		return -1;
    buffer[len] = '\0';
    app_launch_dir = std::string(buffer);
    app_launch_dir = app_launch_dir.substr(0, app_launch_dir.rfind('/') + 1);

    /* Odczyt z pliku konfiguracyjnego config.cfg, w przypadku błędów odczytu program użyje wartości domyślnych */
    try
    {
        global_config.readFile(std::string(app_launch_dir + "/config.cfg").c_str());
    }
    catch(const FileIOException &fioex)
    {
        std::cerr<<"Warning: I/O error while reading configuration file (does config.cfg exists?), using default settings"<<std::endl;
    }
    catch(const ParseException &pex)
    {
        std::cerr<<"Warning: Configuration file parsing error at "<<pex.getFile()<<":"<<pex.getLine()<<" - "<<pex.getError()<<", using default settings"<<std::endl;
    }

    /* Sprawdzenie poprawności wszystkich opcji konfiguracyjnych (czy istnieje, czy ma poprawny typ) - po więcej informacji dot. poszczególnych opcji patrz dokumentacja pliku konfiguracyjnego */
    check_and_make_setting(global_config, "general", Setting::TypeGroup);
    check_and_make_setting(global_config, "general.preserve_permissions", Setting::TypeBoolean, true);
    check_and_make_setting(global_config, "general.follow_symlinks", Setting::TypeBoolean, true);
    check_and_make_setting(global_config, "general.default_permissions", Setting::TypeString, "rwx------");
    check_and_make_setting(global_config, "general.encrypt_files", Setting::TypeBoolean, true);
    check_and_make_setting(global_config, "general.send_to_ftp", Setting::TypeBoolean, false);
    check_and_make_setting(global_config, "general.send_to_phone", Setting::TypeBoolean, false);
    check_and_make_setting(global_config, "general.copy_immediately_max_size", Setting::TypeInt64, 4096L);
    check_and_make_setting(global_config, "general.copy_directory", Setting::TypeString, "");

    check_and_make_setting(global_config, "filtering", Setting::TypeGroup);
    check_and_make_setting(global_config, "filtering.extensions", Setting::TypeGroup);
    check_and_make_setting(global_config, "filtering.extensions.filter_list", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting(global_config, "filtering.extensions.filtering_behavior", Setting::TypeString, "blacklist");
    check_and_make_setting(global_config, "filtering.programs", Setting::TypeGroup);
    check_and_make_setting(global_config, "filtering.programs.filter_list", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting(global_config, "filtering.programs.filtering_behavior", Setting::TypeString, "blacklist");
    check_and_make_setting(global_config, "filtering.filter", Setting::TypeString, "soft");

    check_and_make_setting(global_config, "monitoring", Setting::TypeGroup);
    check_and_make_setting(global_config, "monitoring.mounts", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting(global_config, "monitoring.directory_trees", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting(global_config, "monitoring.files_and_dirs", Setting::TypeArray, 0, Setting::TypeString);

    check_and_make_setting(global_config, "monitored_events", Setting::TypeGroup);
    check_and_make_setting(global_config, "monitored_events.access", Setting::TypeBoolean, true);
    check_and_make_setting(global_config, "monitored_events.open", Setting::TypeBoolean, true);
    check_and_make_setting(global_config, "monitored_events.modify", Setting::TypeBoolean, true);
    check_and_make_setting(global_config, "monitored_events.close_write", Setting::TypeBoolean, true);
    check_and_make_setting(global_config, "monitored_events.close_nowrite", Setting::TypeBoolean, true);
    check_and_make_setting(global_config, "monitored_events.close", Setting::TypeBoolean, true);

    check_and_make_setting(global_config, "ftp.address", Setting::TypeString, "");
    check_and_make_setting(global_config, "ftp.port", Setting::TypeString, "21");
    check_and_make_setting(global_config, "ftp.username", Setting::TypeString, "anonymous");
    check_and_make_setting(global_config, "ftp.password", Setting::TypeString, "");

    return 0;
}
