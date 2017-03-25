#ifndef H_FILESCOPY
#define H_FILESCOPY

#include <sys/fanotify.h> // fanotify
#include <sys/stat.h> // stat
#include <limits.h> // PATH_MAX
#include <unistd.h> // readlink
#include <fcntl.h> // open
#include <string.h> // strerror
#include <sys/sendfile.h> // sendfile64

#include <string>
#include <set>
#include <fstream>
#include <regex>
#include <iostream>

#include "settings.h"

/* Size of buffer to use when reading fanotify events */
#define FANOTIFY_BUFFER_SIZE 16384

extern std::string pendrive_dir;

int initialize_filecopier();
/* Inicjalizuje zmienne potrzebne do poprawnego działania. Musi być wywołana przed
użyciem jakiejkolwiek funkcji z tego pliku nagłówkowego lub użyciem zmiennej pendrive_dir.
Zwraca 0 w przypadku sukcesu inicjalizacji, -1 w przypadku porażki. W przypadku porażki
inicjalizacji używanie którejkolwiek funkcji z tego pliku nagłówkowego lub zmiennej
pendrive_dir skutkuje nieokreślonym zachowaniem
*/

std::string get_program_name_from_pid(int pid);
/* Zwraca nazwę procesu na podstawie jego process id
   Argumenty:
    int pid - id procesu
*/

std::string get_file_path_from_fd(int fd);
/* Zwraca ścieżkę pliku na podstawie jego deskryptora
   Argumenty:
    int fd - deskryptor pliku
*/

bool filter_out(std::string filter_config, std::string text_to_match);
/* Sprawdza czy plik powinien być odfiltrowany przez filtry na podstawie wyrażeń regularnych
   określonych w pliku konfiguracyjnym.
   Zwraca czy plik ma być przepuszczony przez filtr (true) czy odrzucony (false)
   Argumenty:
    std::string filter_config - ścieżka do grupy określonego filtra w pliku konfiguracyjnym
    std::string text_to_match - tekst pod kątem dopasowania do którego będą sprawdzane wyrażenia
                                regularne z pliku konfiguracyjnego w ścieżce określonej
                                jako filter_config
*/

bool is_directory(std::string path);
/* Sprawdza czy ścieżka odwołuje się do katalogu
   Zwraca true jeżeli ścieżka odwołuje się do katalogu, false w przeciwnym wypadku
   Argumenty:
    std::string path - ścieżka do obiektu systemu plików
*/
bool is_small(std::string path);
/* Sprawdza czy plik w podanej ścieżce ma rozmiar mniejszy lub równy niż ustalony
   w konfiguracji pod ścieżką general.copy_immediately_max_size
   Zwraca true, jeżeli plik ma mniejszy lub równy rozmiar, false w przeciwnym wypadku.
   Jeżeli wartość w konfiguracji ma wartość 0 zawsze zwróci true, jeżeli wartość w
   konfiguracji ma wartość -1 zawsze zwróci false
   Argumenty:
    std::string path - ścieżka do pliku
*/

bool is_child(std::string parent, std::string child);
/* Sprawdza czy ścieżka child jest dzieckiem (bezpośrednim lub nie) katalogu parent;
   Inaczej: Sprawdza czy ścieżka parent jest rodzicem katalogu child (czy ścieżka
   child znajduje się gdzieś w podkatalogach parent)
   Zwraca wartość true jeżeli ścieżka child jest dzieckiem, false jeżeli nie
   Argumenty:
    std::string parent - ścieżka domniemanego rodzica
    std::string child - ścieżka domniemanego dziecka
*/

mode_t interpret_string(std::string permissions);
/* Interpretuje string permissions będący w postaci "rwxrwxrwx", gdzie każdy znak
   reprezentuje uprawnienie kolejno: odczytu, zapisu i wykonania, w trójkach kolejno dla:
   właściciela, grupy właściciela i pozostałych. Każdy z tych znaków może być zastąpiony
   przez "-" w celu wyłączenia danego uprawnienia
   Zwraca zmienną typu mode_t będącą standardowym typem do określania uprawnień plików
   i katalogów w systemie GNU/Linux
   Argumenty:
    std::string permissions - tekst reprezentujący uprawnienia
*/

std::string target_path(std::string source_path);
/* Zwraca absolutną, kompletną ścieżkę do której powinien być skopiowany plik; Inaczej:
   zwraca absolutną ścieżkę do podkatalogu na pamięci masowej do której mają być kopiowane
   pliki, pobieraną z konfiguracji + ścieżkę odzwierciedlającą hierarchię drzewa katalogowego
   z którego dany plik jest kopiowany
   Argumenty:
    std::string source_path - ścieżka do pliku źródłowego
*/

void copy_file(std::string from, std::string to);
/* Kopiuje plik ze ścieżki from, do ścieżki to
   Argumenty:
    std::string from - ścieżka pliku do skopiowania
    std::string to - ścieżka do której plik ma być skopiowany
*/

void make_dirs(std::string source_path);
/* Tworzy na pamięci masowej hierarchię katalogów taką, jak w ścieżce source_path
   Argumenty:
    std::string source_path - ścieżka której hierarchia drzewa katalogowego ma zostać odtworzona
*/

bool is_allowed_by_paths(std::string source_path);
/* Sprawdza czy dana ścieżka source_path jest dopuszczona przez monitorowane ścieżki w pliku
   konfiguracyjnym. Ze względu na to, że nie jest możliwe monitorowanie drzew katalogów, a jedynie
   całych mountpointów, bezpośrednich dzieci katalogów lub pojedynczych plików, w przypadku chęci
   monitorowania drzewa katalogowego konieczne jest monitorowanie całego mountpointa. Z tego też
   powodu konieczna jest funkcja która odfiltruje eventy dyskowe z nieinteresujących nas ścieżek.
   Zwraca wartość true, jeżeli ścieżka jest dopuszczona przez ścieżki z konfiguracji, false
   w przeciwnym razie
   Argumenty:
    std::string source_path - sprawdzana ścieżka
*/

void add_file_to_list(const fanotify_event_metadata* metadata);
/* Przepuszcza event dyskowy przez zestaw filtrów, i dodaje ścieżkę pliku do ogólnej listy plików używanych
   podczas danej sesji lub kopiuje plik od razu na pamięć masową jeżeli ma wystarczająco mały rozmiar
   (Do zdeterminowania tego używa funkcji is_small()).
   Argumenty:
    const fanotify_event_metadata* metadata - wskaźnik do zdarzenia dyskowego wygenerowanego przez fanotify
*/

void copy_files();
/* Kopiuje wszystkie pliki z ogólej listy plików używanych podczas danej sesji na pamięć masową do
   ustalonej w konfiguracji podścieżki na pamięci masowej
*/

#endif // H_FILESCOPY
