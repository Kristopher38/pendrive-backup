#ifndef H_SETTINGS
#define H_SETTINGS

#include <iostream>
#include <string>
#include <limits.h> // PATH_MAX
#include <unistd.h> // readlink

#include <libconfig.h++>

extern libconfig::Config global_config;        /* Obiekt konfiguracji programu */
extern std::string app_launch_dir;             /* Ścieżka z której uruchamiany jest program (argv[0]) */

template <typename T=int> bool check_and_make_setting(libconfig::Config& cfg,
                                                      std::string name,
                                                      libconfig::Setting::Type val_type,
                                                      T value = 0,
                                                      libconfig::Setting::Type list_type = libconfig::Setting::TypeString);
/* Funkcja sprawdzająca poprawność obiektu w obiekcie konfiguracji i tworząca nowy obiekt z domyślną
   wartością w przypadku stwierdzenia braku poprawności tegoż obiektu.
   Zwraca true jeżeli obiekt istnieje w konfiguracji poprawnie, lub false w przypadku gdy funkcja musiała podejmować
   akcje tworzenia nowego obiektu z powodu jego braku lub niepoprawności typu
   Argumenty:
    libconfig::Config& cfg - referencja do obiektu konfiguracji na którym działamy
    std::string name - ścieżka do obiektu w konfiguracji
    libconfig::Setting::Type val_type - typ, który powinien mieć obiekt konfiguracji
    T value - wartość domyślna dla danego obiektu w przypadku stwierdzenia jego braku lub niepoprawności typu
    libconfig::Setting:Type list_type - domyślny tym dla danego obiektu w przypadku stwierdzenia jego braku lub niepoprawności typu
*/

int init_settings();
/* Funkcja inicjalizująca globalne zmienne global_config i app_launch_dir, oraz odczytująca plik konfiguracyjny
   i sprawdzająca istnienie i poprawność wszystkich wymaganych przez program opcji konfiguracyjnych
   Zwraca 0 w przypadku sukcesu inicjalizacji, lub -1 w przypadku porażki. W przypadku porażki inicjalizacji używanie
   zmiennych global_config i app_launch_dir skutkuje niezdefiniowanym zachowaniem.
*/

#endif // H_SETTINGS
