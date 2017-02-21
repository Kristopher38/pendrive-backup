#define _GNU_SOURCE

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <string>
#include <iostream>
#include <stdexcept>
#include <set>
#include <dirent.h>
#include <libconfig.h++>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/fanotify.h>
#include <mcrypt.h>
#include <regex>
#include <pwd.h>

using namespace libconfig;

/* Structure to keep track of monitored directories */
typedef struct {
	/* Path of the directory */
	char *path;
} monitored_t;

/* Size of buffer to use when reading fanotify events */
#define FANOTIFY_BUFFER_SIZE 8192

/* Enumerate list of FDs to poll */
enum {
	FD_POLL_SIGNAL = 0,
	FD_POLL_FANOTIFY,
	FD_POLL_MAX
};

std::string pendrive_dir;
char* encryption_key;
std::set<std::string> files_to_copy;
Config config;
std::string default_file_owner;

uid_t name_to_uid(char const *name)
{
  if (!name)
    return -1;
  long const buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (buflen == -1)
    return -1;
  // requires c99
  char buf[buflen];
  struct passwd pwbuf, *pwbufp;
  if (0 != getpwnam_r(name, &pwbuf, buf, buflen, &pwbufp)
      || !pwbufp)
    return -1;
  return pwbufp->pw_uid;
}

gid_t name_to_gid(char const *name)
{
  if (!name)
    return -1;
  long const buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (buflen == -1)
    return -1;
  // requires c99
  char buf[buflen];
  struct passwd pwbuf, *pwbufp;
  if (0 != getpwnam_r(name, &pwbuf, buf, buflen, &pwbufp)
      || !pwbufp)
    return -1;
  return pwbufp->pw_gid;
}

int setegiduid(gid_t egid, uid_t euid)
{
    int resgid = setegid(egid);
    int resuid = seteuid(euid);
    if ((resgid == -1) || (resuid == -1))
        return -1;
    else return 0;
}

static char* get_program_name_from_pid (int pid, char *buffer, size_t buffer_size)
{
	int fd;
	ssize_t len;
	char *aux;

	/* Try to get program name by PID */
	sprintf(buffer, "/proc/%d/cmdline", pid);
	if ((fd = open(buffer, O_RDONLY)) < 0)
		return NULL;

	/* Read file contents into buffer */
	if ((len = read(fd, buffer, buffer_size - 1)) <= 0)
	{
		close(fd);
		return NULL;
	}
	close(fd);

	buffer[len] = '\0';
	aux = strstr(buffer, "^@");
	if (aux)
		*aux = '\0';

	return buffer;
}

static char* get_file_path_from_fd (int fd, char *buffer, size_t buffer_size)
{
	ssize_t len;

	if (fd <= 0)
		return NULL;

	sprintf(buffer, "/proc/self/fd/%d", fd);
	if ((len = readlink(buffer, buffer, buffer_size - 1)) < 0)
		return NULL;

	buffer[len] = '\0';
	return buffer;
}

/*static void event_process (struct fanotify_event_metadata *event)
{
  char path[PATH_MAX];

  printf ("Received event in path '%s'",
          get_file_path_from_fd (event->fd,
                                   path,
                                   PATH_MAX) ?
          path : "unknown");
  printf (" pid=%d (%s): \n",
          event->pid,
          (get_program_name_from_pid (event->pid,
                                        path,
                                        PATH_MAX) ?
           path : "unknown"));

  if (event->mask)
    printf ("\tFull event mask: %llu\n", event->mask);
  if (event->mask & FAN_OPEN)
    printf ("\tFAN_OPEN\n");
  if (event->mask & FAN_ACCESS)
    printf ("\tFAN_ACCESS\n");
  if (event->mask & FAN_MODIFY)
    printf ("\tFAN_MODIFY\n");
  if (event->mask & FAN_CLOSE_WRITE)
    printf ("\tFAN_CLOSE_WRITE\n");
  if (event->mask & FAN_CLOSE_NOWRITE)
    printf ("\tFAN_CLOSE_NOWRITE\n");
  fflush (stdout);

  close (event->fd);
}*/

uint64_t initialize_event_mask()
{
    uint64_t event_mask = 0;

    if (config.lookup("monitored_events.access"))
        event_mask |= FAN_ACCESS;
    if (config.lookup("monitored_events.open"))
        event_mask |= FAN_OPEN;
    if (config.lookup("monitored_events.modify"))
        event_mask |= FAN_MODIFY;
    if (config.lookup("monitored_events.close_write"))
        event_mask |= FAN_CLOSE_WRITE;
    if (config.lookup("monitored_events.close_nowrite"))
        event_mask |= FAN_CLOSE_NOWRITE;
    if (config.lookup("monitored_events.close"))
        event_mask |= FAN_CLOSE;

    return event_mask;
}

static void shutdown_fanotify (int fanotify_fd)
{
    close(fanotify_fd);
}

static int initialize_fanotify (int argc, const char **argv)
{
    // check if lists of mounts, files and directories to monitor are empty
    if (config.lookup("monitoring.mounts").getLength() == 0 && config.lookup("monitoring.files_and_dirs").getLength() == 0)
    {
        std::cerr<<"Monitoring lists are empty, edit \"monitoring\" section in config.cfg"<<std::endl;
        return -1;
    }

    // create fanotify device
    int fanotify_fd = fanotify_init(FAN_CLOEXEC | FAN_NONBLOCK, O_RDONLY | O_CLOEXEC | O_LARGEFILE | O_NOATIME);
    if (fanotify_fd < 0)
    {
        fprintf (stderr, "Couldn't setup new fanotify device: %s\n", strerror (errno));
        return -1;
    }

    uint64_t event_mask = initialize_event_mask(); // get event mask

    for (int i = 0; i < config.lookup("monitoring.mounts").getLength(); ++i)    // add mount to be monitored
    {
        const char* monitored_mount = config.lookup("monitoring.mounts")[i];
        if (fanotify_mark(fanotify_fd, FAN_MARK_ADD | FAN_MARK_MOUNT, event_mask, 0, monitored_mount) < 0)
            std::cerr<<"Couldn't add monitor on mount "<<monitored_mount<<": "<<strerror(errno)<<std::endl;
        else std::cout<<"Started monitoring mount "<<monitored_mount<<std::endl;
    }
    for (int i = 0; i < config.lookup("monitoring.files_and_dirs").getLength(); ++i)    // add files and directories to be monitored
    {
        const char* monitored_file = config.lookup("monitoring.files_and_dirs")[i];
        if (fanotify_mark(fanotify_fd, FAN_MARK_ADD, event_mask | FAN_EVENT_ON_CHILD, 0, monitored_file) < 0)
            std::cerr<<"Couldn't add monitor on file or directory "<<monitored_file<<": "<<strerror(errno)<<std::endl;
        else std::cout<<"Started monitoring file or directory "<<monitored_file<<std::endl;
    }

    return fanotify_fd;
}

static void shutdown_signals (int signal_fd)
{
	close(signal_fd);
}

static int initialize_signals (void)
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
		fprintf(stderr,
			"Couldn't block signals: '%s'\n",
			strerror(errno));
		return -1;
	}

	/* Get new FD to read signals from it */
	if ((signal_fd = signalfd(-1, &sigmask, 0)) < 0)
	{
		fprintf(stderr,
			"Couldn't setup signal FD: '%s'\n",
			strerror(errno));
		return -1;
	}

	return signal_fd;
}

bool replace(std::string& str, const std::string& from, const std::string& to) {
	size_t start_pos = str.find(from);
	if (start_pos == std::string::npos)
		return false;
	str.replace(start_pos, from.length(), to);
	return true;
}


char* encrypt_password(char* IV, char* key_org, char* buffer, bool is_decrypt)
{
	MCRYPT td = mcrypt_module_open("twofish", NULL, "cbc", NULL);

	int keysize = 19;
	char* key;
	key = (char *)calloc(1, keysize);
	//mhash_keygen( KEYGEN_MCRYPT, MHASH_MD5, key, keysize, NULL, 0, key_org, strlen(key_org));
	memmove(key, key_org, keysize);

	int blocksize = 1024;

	char* block_buffer = (char *)malloc(blocksize);


	mcrypt_generic_init(td, key, keysize, IV);

	strncpy(block_buffer, buffer, blocksize);

	if (!is_decrypt)
		mcrypt_generic(td, block_buffer, blocksize);
	else
		mdecrypt_generic(td, block_buffer, blocksize);


	mcrypt_generic_deinit(td);
	mcrypt_module_close(td);
	return block_buffer;
}
int encrypt(
	char* IV,
	char* key_org,
	const char* path,
	bool isDecrypt
) {
	MCRYPT td = mcrypt_module_open("twofish", NULL, "cbc", NULL);
	int keysize = 19;
	char* key;
	key = (char *)calloc(1, keysize);
	//mhash_keygen( KEYGEN_MCRYPT, MHASH_MD5, key, keysize, NULL, 0, key_org, strlen(key_org));
	memmove(key, key_org, keysize);

	int blocksize = mcrypt_enc_get_block_size(td);

	char* block_buffer = (char *)malloc(blocksize);

	mcrypt_generic_init(td, key, keysize, IV);

	FILE *fileptr;
	FILE *write_ptr;

	fileptr = fopen(path, "rb");

	std::string str(path);
	if (isDecrypt && !strstr(str.c_str(), ".enc"))
		return 0;
	else
		if (!isDecrypt && strstr(str.c_str(), ".enc"))
			return 0;
	if (isDecrypt)
		replace(str, ".enc", "");
	else
		str = str + ".enc";

	write_ptr = fopen(str.c_str(), "wb");
	int readbytes = 0;

	do
	{
		readbytes = fread(block_buffer, 1, blocksize, fileptr);

		if (readbytes == blocksize)
		{
			if (!isDecrypt)
				mcrypt_generic(td, block_buffer, blocksize);
			else
				mdecrypt_generic(td, block_buffer, blocksize);
			int iToWrite = blocksize;
			for (int i = blocksize - 1; i >= 0; i--)
			{
				if (block_buffer[i] == '\0')
					iToWrite--;
				else
					break;
			}
			fwrite(block_buffer, 1, iToWrite, write_ptr);
		}
		else if (readbytes > 0)
		{
			for (int i = readbytes; i < blocksize; i++)
				block_buffer[i] = '\0';

			mcrypt_generic(td, block_buffer, blocksize);

			fwrite(block_buffer, 1, blocksize, write_ptr);
		}
	} while (readbytes > 0);

	fclose(write_ptr);
	fclose(fileptr);

	mcrypt_generic_deinit(td);
	mcrypt_module_close(td);

	struct stat st;
	stat(path, &st);
	chmod(str.c_str(), st.st_mode);

	remove(path);

	return 0;
}

void crypt_file(const char* file, bool is_decrypt)
{
	char* IV = "ghlvkycdfncsoitd";

	encrypt(IV, encryption_key, file, is_decrypt);
}

void get_directory(const char* directory, bool is_decrypt)
{
	DIR *dir = opendir(directory);
	if (dir)
	{
		struct dirent *entry = readdir(dir);

		while (entry != NULL)
		{
			std::string str2(entry->d_name);

			std::string str(directory);

			std::string file = str + "/" + str2;

			if (entry->d_type == DT_DIR && str2 != ".." && str2 != ".")
				get_directory(file.c_str(), is_decrypt);
			else if (entry->d_type == DT_REG)
				crypt_file(file.c_str(), is_decrypt);

			entry = readdir(dir);
		}

		closedir(dir);
	}
}

void crypt_files(bool is_decrypt)
{
	get_directory(pendrive_dir.c_str(), is_decrypt);
	//get_directory("/home/ozon/Videos",is_decrypt);
}

char *trim(char *str)
{
	size_t len = 0;
	char *frontp = str;
	char *endp = NULL;

	if (str == NULL) { return NULL; }
	if (str[0] == '\0') { return str; }

	len = strlen(str);
	endp = str + len;

	/* Move the front and back pointers to address the first non-whitespace
	* characters from each end.
	*/
	while (isspace((unsigned char)*frontp)) { ++frontp; }
	if (endp != frontp)
	{
		while (isspace((unsigned char) *(--endp)) && endp != frontp) {}
	}

	if (str + len - 1 != endp)
		*(endp + 1) = '\0';
	else if (frontp != str &&  endp == frontp)
		*str = '\0';

	/* Shift the string so that it starts at str so that if it's dynamically
	* allocated, we can still free it on the returned pointer.  Note the reuse
	* of endp to mean the front of the string buffer now.
	*/
	endp = str;
	if (frontp != str)
	{
		while (*frontp) { *endp++ = *frontp++; }
		*endp = '\0';
	}


	return str;
}

bool filter_out(std::string filter_config, std::string text_to_match)
{
    enum FILTER_TYPE {BLACKLIST = 0, WHITELIST = 1};
    FILTER_TYPE filter;
    std::string filtering_behaviour = config.lookup(std::string(filter_config + ".filtering_behavior"));
    if (filtering_behaviour == "whitelist")
        filter = WHITELIST;
    else if (filtering_behaviour == "blacklist")
        filter = BLACKLIST;
    else filter = BLACKLIST;

    Setting& filenames = config.lookup(std::string(filter_config + ".filter_list"));
    for (int i = 0; i < filenames.getLength(); ++i)
    {
        try
        {
            std::regex filename_regex(filenames[i]);
            bool regex_matched = std::regex_match(text_to_match, filename_regex);
            if (regex_matched && filter == BLACKLIST)       // if item is on blacklist - filter it out
                return true;
            else if (regex_matched && filter == WHITELIST)  // if item is on blacklist - don't filter it out
                return false;
        }
        catch (const std::regex_error& e)
        {
            std::string file_re = filenames[i];
            std::cerr<<"Warning, regex "<<file_re<<" is invalid, and won't be used in the future"<<std::endl;
            filenames.remove(i);
        }
    }
    if (filter == BLACKLIST)        // if item wasn't found on blacklist, don't filter it out
        return false;
    else if (filter == WHITELIST)   // if item wasn't found on whitelist, filter it out
        return true;

    return false;
}

bool is_directory(std::string path)
{
    if (path.back() == '/')
        path.pop_back();
    struct stat stat_path;
    if (stat(path.c_str(), &stat_path) == 0)
        if (S_ISDIR(stat_path.st_mode))
            return true;
        else return false;
    else
        return false;
    return false;
}

void add_file_to_list(const fanotify_event_metadata* metadata)
{
    char target_path_c[PATH_MAX];
    get_file_path_from_fd(metadata->fd, target_path_c, PATH_MAX);
    std::string target_path(target_path_c);

    enum FILTER_SOFTNESS {FILTER_SOFT, FILTER_HARD};
    FILTER_SOFTNESS softness;
    std::string filter_softness = config.lookup("filtering.filter");

    if (filter_softness == "soft")
        softness = FILTER_SOFT;
    else if (filter_softness == "hard")
        softness = FILTER_HARD;
    else softness = FILTER_HARD;

    bool filter_extension = false;
    bool filter_program = false;

    // filter out event if path is a directory or if it comes from the pendrive
    if (is_directory(target_path) || target_path.find(pendrive_dir) != std::string::npos)
        return;

    // filter event based on extension filter from config
    if (config.lookup("filtering.extensions.filter_list").getLength() > 0)
    {
        std::string filename = target_path.substr(target_path.find_last_of("/") + 1, std::string::npos);
        filter_extension = filter_out("filtering.extensions", filename);
    }

    // filter event based on program filter from config
    if (config.lookup("filtering.programs.filter_list").getLength() > 0)
    {
        char program_name_c[PATH_MAX];
        get_program_name_from_pid(metadata->pid, program_name_c, PATH_MAX);
        std::string program_name(program_name_c);
        filter_program = filter_out("filtering.programs", program_name);
    }

    if (softness == FILTER_HARD && (filter_extension || filter_program))
        return;
    if (softness == FILTER_SOFT && (filter_extension && filter_program))
        return;


    std::cout<<target_path<<std::endl;
    files_to_copy.insert(target_path);
}

void copy_files()
{
    for (std::set<std::string>::iterator it = files_to_copy.begin(); it != files_to_copy.end(); ++it)
    {
        std::string source_path = *it;
        //std::cout<<"Source path: "<<source_path<<std::endl;
        if (source_path.find(pendrive_dir) != std::string::npos) // ignore event if it comes from the pendrive
            return;

        std::string source_path_noroot = source_path.substr(1, std::string::npos); // substr to remove root dir form the path
        std::string source_path_nofile = source_path_noroot.substr(0, source_path_noroot.rfind('/') + 1); // remove the file name form the path

        std::string pendrive_dir_str = pendrive_dir;
        std::size_t pos = 0;
        struct stat st;
        std::string current_dir; // pendrive directory with appended current working directory (the one we are cheching for existence)
        std::string full_path = pendrive_dir_str + source_path_noroot; // pendrive directory with full source directory with filename
        std::string source_dir; // current source directory which we are working on

        while (pos != std::string::npos) // iterate through directories to check if they exist on pendrive
        {
            pos = source_path_nofile.find('/', pos+1); // pos + 1 to find next dir
            current_dir = pendrive_dir_str + source_dir;
            source_dir = source_path_nofile.substr(0, pos);

            if (stat(current_dir.c_str(), &st) == -1) // if directory doesn't exist, create
            {
                int original_dir_st = stat(std::string("/"+source_dir).c_str(), &st); // get original folder's stat...
                if (original_dir_st == -1)
                {
                    fprintf(stderr, "Error stat(): %s, path: %s\n", strerror(errno), std::string("/"+source_dir).c_str());
                    continue;
                }
                //std::cout<<"Original folder stat: "<<std::oct<<st.st_mode<<std::endl;
                if (mkdir(current_dir.c_str(), st.st_mode) < 0)
                    std::cerr<<"Error mkdir(): "<<strerror(errno)<<", path: "<<current_dir<<std::endl;
                stat(current_dir.c_str(), &st);
                //std::cout<<"Resulting folder stat: "<<std::oct<<st.st_mode<<std::endl; // ...and use it to make folder on pendrive with identical permissions
            }
        }

        int source_file = 0;
        if ((source_file = open(source_path.c_str(), O_RDONLY)) > -1)
        {
            int target;
            struct stat stat_source;
            fstat(source_file, &stat_source);
            if (config.lookup("general.preserve_permissions"))
            {
                /*if (setegiduid(stat_source.st_gid, stat_source.st_uid) == -1)                     // preserve group and user id ???
                    fprintf(stderr, "Error setegiduid(): %s", strerror(errno));*/

                target = open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, stat_source.st_mode); // create new file at full_path with stat_source.st_mode permissions

                /*if (setegiduid(name_to_gid(default_file_owner.c_str()), name_to_uid(default_file_owner.c_str())) == -1)
                    fprintf(stderr, "Error setegiduid(): %s", strerror(errno));*/
            }
            else target = open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);

            if (target == -1)
                fprintf(stderr, "Error open(): %s, path: %s\n", strerror(errno), full_path.c_str());
            else
            {
                int ret = sendfile(target, source_file, 0, stat_source.st_size); // copy the source_file to target path
                if (ret == -1)
                    fprintf(stderr, "Error sendfile(): %s, path: %s\n", strerror(errno), full_path.c_str());
                else if (ret != stat_source.st_size)
                {
                    std::cerr<<"Error sendfile(): count of copied bytes mismatches the file size"<<std::endl;
                    std::cerr<<"File size: "<<stat_source.st_size<<", bytes written: "<<ret<<std::endl;
                }
            }
            close(target);
            close(source_file);
        }
        else std::cout<<"Can't open source file: "<<strerror(errno)<<", path: "<<source_path<<std::endl;
    }
}

template <typename T=int> bool check_and_make_setting(std::string name, Setting::Type val_type, T value = 0, Setting::Type list_type = Setting::TypeString)
{
    if (!(config.exists(name) && config.lookup(name).getType() == val_type) ||
        (val_type == Setting::TypeArray && config.lookup(name).getLength() > 0 && config.lookup(name)[0].getType() != list_type))
    {
        std::size_t pos = name.find_last_of(".", std::string::npos);
        std::cerr<<name<<" setting not specified or wrong type, using default value"<<std::endl;
        if (pos == std::string::npos)
        {
            try {
                config.getRoot().remove(name);
            } catch (const SettingNotFoundException& e) {}
            config.getRoot().add(name, val_type);
        }
        else
        {
            std::string group = name.substr(0, pos);
            std::string setting = name.substr(pos+1, std::string::npos);
            try {
                config.lookup(group).remove(setting);
            } catch (const SettingNotFoundException& e) {}
            config.lookup(group).add(setting, val_type);
        }
        try {
            if (!(val_type == Setting::TypeGroup || val_type == Setting::TypeArray || val_type == Setting::TypeList))
                config.lookup(name) = value;
        } catch (const SettingTypeException& e) {
            std::cerr<<"Warning: no setting initial value specified or value with wrong type supplied to "<<name<<std::endl;
            throw e;
        }
        return true;
    }
    return false;
}

void init_settings()
{
    try
    {
        config.readFile("config.cfg");
    }
    catch(const FileIOException &fioex)
    {
        std::cerr<<"Warning: I/O error while reading configuration file (does config.cfg exist?), using default settings"<<std::endl;
    }
    catch(const ParseException &pex)
    {
        std::cerr<<"Warning: Configuration file parsing error at "<<pex.getFile()<<":"<<pex.getLine()<<" - "<<pex.getError()<<", using default settings"<<std::endl;
    }
    check_and_make_setting("general", Setting::TypeGroup);
    check_and_make_setting("general.preserve_permissions", Setting::TypeBoolean, true);
    check_and_make_setting("general.encrypt_files", Setting::TypeBoolean, true);
    check_and_make_setting("general.send_to_ftp", Setting::TypeBoolean, false);
    check_and_make_setting("general.send_to_phone", Setting::TypeBoolean, false);
    check_and_make_setting("general.copy_immediately_max_size", Setting::TypeInt64, 4096L);
    check_and_make_setting("general.permissions_user", Setting::TypeString, "root");
    check_and_make_setting("general.copy_directory", Setting::TypeString, "");

    check_and_make_setting("filtering", Setting::TypeGroup);
    check_and_make_setting("filtering.extensions", Setting::TypeGroup);
    check_and_make_setting("filtering.extensions.filter_list", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting("filtering.extensions.filtering_behavior", Setting::TypeString, "blacklist");
    check_and_make_setting("filtering.programs", Setting::TypeGroup);
    check_and_make_setting("filtering.programs.filter_list", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting("filtering.programs.filtering_behavior", Setting::TypeString, "blacklist");
    check_and_make_setting("filtering.filter", Setting::TypeString, "soft");

    check_and_make_setting("monitoring", Setting::TypeGroup);
    check_and_make_setting("monitoring.mounts", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting("monitoring.directory_trees", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting("monitoring.files_and_dirs", Setting::TypeArray, 0, Setting::TypeString);

    check_and_make_setting("monitored_events", Setting::TypeGroup);
    check_and_make_setting("monitored_events.access", Setting::TypeBoolean, true);
    check_and_make_setting("monitored_events.open", Setting::TypeBoolean, true);
    check_and_make_setting("monitored_events.modify", Setting::TypeBoolean, true);
    check_and_make_setting("monitored_events.close_write", Setting::TypeBoolean, true);
    check_and_make_setting("monitored_events.close_nowrite", Setting::TypeBoolean, true);
    check_and_make_setting("monitored_events.close", Setting::TypeBoolean, true);

    //config.writeFile("modconfig.cfg");
}

int
main(int          argc,
	const char **argv)
{
    init_settings();
    std::string default_file_owner_temp = config.lookup("general.permissions_user");
    default_file_owner = default_file_owner_temp;

  int signal_fd;
  int fanotify_fd;
  struct pollfd fds[FD_POLL_MAX];

    char cwd[1024];
    std::string copy_dir = config.lookup("general.copy_directory");
    // check if copy directory is directory or exists
    if (!is_directory(copy_dir))
    {
        std::cerr<<"Copy directory isn't a directory or doesn't exist"<<std::endl;
        exit(EXIT_FAILURE);
    }
    // check if it has / on the end - it's required standarized way for the program to work
    if (copy_dir.back() != '/')
        copy_dir.append("/");
    // check if it's absolute
    if (copy_dir.length() > 0 && copy_dir[0] == '/')
        pendrive_dir = copy_dir;
    // if it's relative, get current working directory and append copy subdirectory to id
    else if (getcwd(cwd, sizeof(cwd)) != NULL)
        pendrive_dir = std::string(cwd) + std::string("/") + copy_dir;
    else
    {
        std::cerr<<"Failed to get current working directory"<<std::endl;
        exit(EXIT_FAILURE);
    }
    std::cout<<pendrive_dir<<std::endl;

  	if (access("enc.key", F_OK) == -1)
	{
		printf("Please enter password to set\n");
		char pass[128];
		fgets(pass, sizeof(pass), stdin);

		char* IV = "ghlvkycdfncsoitd";
		char* enc_pass = encrypt_password(IV, pass, pass, false);

		FILE *write_ptr;
		write_ptr = fopen("enc.key", "wb");
		fwrite(enc_pass, 1, strlen(enc_pass), write_ptr);
		fclose(write_ptr);

		encryption_key = trim(pass);
		crypt_files(false);
	}
	else
	{
		bool bSuccess = false;
		while (!bSuccess)
		{
			printf("Please enter password\n");

			char pass[128];
			fgets(pass, sizeof(pass), stdin);

			char* IV = "ghlvkycdfncsoitd";
			char enc_pass_file[1024];

			FILE *fileptr;
			fileptr = fopen("enc.key", "rb");
			fread(enc_pass_file, 1, 1024, fileptr);
			fclose(fileptr);

			char* dec_pass_file = encrypt_password(IV, pass, enc_pass_file, true);

			std::string str(pass);
			std::string str2(dec_pass_file);
			if (str == str2)
			{
				bSuccess = true;
				printf("SUCCESS! DECRYPTING FILES...\n");
				char* cstr = new char[str.length() + 1];
				strcpy(cstr, str.c_str());

				encryption_key = trim(dec_pass_file);

				crypt_files(true);
				printf("COMPLETE!\n");
			}
			else
				printf("FAILED TRY AGAIN\n");
		}

	}

  bool do_exit_procedures = false;

  /* Initialize signals FD */
  if ((signal_fd = initialize_signals ()) < 0)
    {
      fprintf (stderr, "Couldn't initialize signals\n");
      exit (EXIT_FAILURE);
    }

  /* Initialize fanotify FD and the marks */
  if ((fanotify_fd = initialize_fanotify (argc, argv)) < 0)
    {
      fprintf (stderr, "Couldn't initialize fanotify\n");
      exit (EXIT_FAILURE);
    }

    //if (setegiduid(name_to_gid(default_file_owner.c_str()), name_to_uid(default_file_owner.c_str())) == -1) // set gid and uid to desired user???
    //    fprintf(stderr, "Error setegiduid(): %s", strerror(errno));

  /* Setup polling */
  fds[FD_POLL_SIGNAL].fd = signal_fd;
  fds[FD_POLL_SIGNAL].events = POLLIN;
  fds[FD_POLL_FANOTIFY].fd = fanotify_fd;
  fds[FD_POLL_FANOTIFY].events = POLLIN;

  umask(0);



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
                    //event_process (metadata);
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

        if (config.lookup("general.encrypt_files"))
        {
            std::cout<<"Encrypting files"<<std::endl;
            crypt_files(false);
        }

        if (config.lookup("general.send_to_ftp"))
        {
            // std::cout<<"Sending files to ftp server"<<std::endl;
            // send_to_ftp();
        }

        if (config.lookup("general.send_to_phone"))
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
