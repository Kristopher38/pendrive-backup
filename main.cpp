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

#include <fstream>

using namespace libconfig;

/* Size of buffer to use when reading fanotify events */
#define FANOTIFY_BUFFER_SIZE 8192

/* Enumerate list of FDs to poll */
enum {
	FD_POLL_SIGNAL = 0,
	FD_POLL_FANOTIFY,
	FD_POLL_MAX
};

const std::string app_name("pbackup");

std::string app_launch_dir;
std::string pendrive_dir;
char* encryption_key;
std::set<std::string> files_to_copy;
Config config;
std::string user_perm;

int setegiduid(gid_t egid, uid_t euid)
{
    int resgid = setegid(egid);
    int resuid = seteuid(euid);
    if ((resgid == -1) || (resuid == -1))
        return -1;
    else return 0;
}

static std::string get_program_name_from_pid (int pid)
{
    char buffer[PATH_MAX];
	int fd;
	ssize_t len;
	char *aux;

	/* Try to get program name by PID */
	sprintf(buffer, "/proc/%d/cmdline", pid);
	if ((fd = open(buffer, O_RDONLY)) < 0)
		return std::string("");

	/* Read file contents into buffer */
	if ((len = read(fd, buffer, PATH_MAX - 1)) <= 0)
	{
		close(fd);
		return std::string("");
	}
	close(fd);

	buffer[len] = '\0';
	aux = strstr(buffer, "^@");
	if (aux)
		*aux = '\0';

	return std::string(buffer);
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

static int initialize_fanotify()
{
    // check if lists of mounts, files and directories to monitor are empty
    if (config.lookup("monitoring.mounts").getLength() == 0 && config.lookup("monitoring.files_and_dirs").getLength() == 0 && config.lookup("monitoring.directory_trees").getLength() == 0)
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
    for (int i = 0; i < config.lookup("monitoring.directory_trees").getLength(); ++i)    // add files and directories to be monitored
    {
        const char* monitored_dirtree = config.lookup("monitoring.directory_trees")[i];
        if (fanotify_mark(fanotify_fd, FAN_MARK_ADD | FAN_MARK_MOUNT, event_mask, 0, monitored_dirtree) < 0)
            std::cerr<<"Couldn't add monitor on directory tree "<<monitored_dirtree<<": "<<strerror(errno)<<std::endl;
        else std::cout<<"Started monitoring directory tree "<<monitored_dirtree<<std::endl;
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

bool is_directory(std::string path) // checks if specified path is a directory
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

bool is_small(std::string path) // checks if file size is smaller than specified in config in general.copy_immediately_max_size
{
    if (is_directory(path))
        return false;
    struct stat stat_path;
    if (stat(path.c_str(), &stat_path) == 0)
        if (stat_path.st_size <= static_cast<int64_t>(config.lookup("general.copy_immediately_max_size")))
            return true;
    return false;
}

bool is_child(std::string parent, std::string child) // checks if child is a somewhere inside parent directory in the filesystem tree hierarchy
{
    child = child.substr(0, parent.length()); // match the lengths of both strings and check if they match each other
    if (child == parent)
        return true;
    else return false;
}

std::string target_path(std::string source_path)    // returns path to which file shall be copied
{
    return pendrive_dir + source_path.substr(1, std::string::npos);
}

void copy_file(std::string from, std::string to)
{
    int source_file = 0;
    if ((source_file = open(from.c_str(), O_RDONLY)) > -1)
    {
        int target;
        struct stat stat_source;
        fstat(source_file, &stat_source);
        if (config.lookup("general.preserve_permissions"))
            target = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, stat_source.st_mode); // create new file at full_path with stat_source.st_mode permissions
        else target = open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC);

        if (target == -1)
            fprintf(stderr, "Error open(): %s, path: %s\n", strerror(errno), to.c_str());
        else
        {
            int ret = sendfile64(target, source_file, 0, stat_source.st_size); // copy the source_file to target path
            if (ret == -1)
                fprintf(stderr, "Error sendfile(): %s, path: %s\n", strerror(errno), to.c_str());
            else if (ret != stat_source.st_size)
            {
                std::cerr<<"Error sendfile(): count of copied bytes mismatches the file size"<<std::endl;
                std::cerr<<"File size: "<<stat_source.st_size<<", bytes written: "<<ret<<std::endl;
            }
        }
        close(target);
        close(source_file);
    }
    else std::cout<<"Can't open source file: "<<strerror(errno)<<", path: "<<from<<std::endl;
}

void make_dirs(std::string source_path)
{
    if (source_path.find(pendrive_dir) != std::string::npos) // ignore event if it comes from the pendrive
        return;

    std::string source_path_nofile = source_path.substr(1, source_path.rfind('/')); // remove the root slash at the beggining and file name form the path

    std::size_t pos = 0;
    struct stat st;
    std::string current_dir; // pendrive directory with appended current working directory (the one we are checking for existence)
    std::string source_dir; // current source directory which we are working on

    while (pos != std::string::npos) // iterate through directories to check if they exist on pendrive
    {
        pos = source_path_nofile.find('/', pos+1); // pos + 1 to find next dir
        current_dir = pendrive_dir + source_dir;
        source_dir = source_path_nofile.substr(0, pos);

        if (stat(current_dir.c_str(), &st) == -1) // if directory doesn't exist, create
        {
            int original_dir_st = stat(std::string("/"+source_dir).c_str(), &st); // get original folder's stat...
            if (original_dir_st == -1)
            {
                fprintf(stderr, "Error stat(): %s, path: %s\n", strerror(errno), std::string("/"+source_dir).c_str());
                continue;
            }
            if (mkdir(current_dir.c_str(), st.st_mode) < 0)
                std::cerr<<"Error mkdir(): "<<strerror(errno)<<", path: "<<current_dir<<std::endl;
            stat(current_dir.c_str(), &st);
        }
    }
}

bool is_allowed_by_paths(std::string source_path)
{
    const unsigned path_count = 3;
    const std::string path_order[path_count] = {"mounts", "files_and_dirs", "directory_trees"}; // defines in which order paths shall be checked, directory trees must be always on the end

    for (unsigned i = 0; i < path_count; ++i)   // loop through above path types
    {
        Setting& path = config.lookup(std::string("monitoring.") + path_order[i]);
        if (path.getLength() > 0)
        {
            for (int j = 0; j < path.getLength(); ++j)  // loop through each path in path type
            {
                std::string parent_path = path[j];
                if (is_child(parent_path, source_path))
                    return true;    // if file is a child of some path, let it be
            }
        }
    }
    return false;   // if file wasn't found to be child of any of specified paths, drop it
}

void add_file_to_list(const fanotify_event_metadata* metadata)
{
    char source_path_c[PATH_MAX];
    get_file_path_from_fd(metadata->fd, source_path_c, PATH_MAX);
    std::string source_path(source_path_c);

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

    // filter out event if path is a directory or if it comes from the pendrive or if it's generated by the program itself
    std::string program_name = get_program_name_from_pid(metadata->pid);
    if (is_directory(source_path) || source_path.find(pendrive_dir) != std::string::npos || program_name == app_name || program_name == app_launch_dir)
        return;

    // filter event based on extension filter from config
    if (config.lookup("filtering.extensions.filter_list").getLength() > 0)
    {
        std::string filename = source_path.substr(source_path.find_last_of("/") + 1, std::string::npos);
        filter_extension = filter_out("filtering.extensions", filename);
    }

    // filter event based on program filter from config
    if (config.lookup("filtering.programs.filter_list").getLength() > 0)
    {
        std::string program_name = get_program_name_from_pid(metadata->pid);
        filter_program = filter_out("filtering.programs", program_name);
    }

    if (softness == FILTER_HARD && (filter_extension || filter_program))
        return;
    if (softness == FILTER_SOFT && (filter_extension && filter_program))
        return;

    if (!is_allowed_by_paths(source_path))
        return;

    // copy immediately instead of adding to the list if it's small enough (value from config)
    std::cout<<source_path<<std::endl;
    if (is_small(source_path))
    {
        make_dirs(source_path);
        copy_file(source_path, target_path(source_path));
    }
    else
        files_to_copy.insert(source_path);
}

void copy_files()
{
    for (std::set<std::string>::iterator it = files_to_copy.begin(); it != files_to_copy.end(); ++it)
    {
        std::string source_path = *it;
        std::string target_path = pendrive_dir + source_path.substr(1, std::string::npos); // pendrive directory with full source directory with filename (substr removes root slash at the beggining)
        make_dirs(source_path);
        copy_file(source_path, target_path);
    }
}

template <typename T=int> bool check_and_make_setting(Config& cfg, std::string name, Setting::Type val_type, T value = 0, Setting::Type list_type = Setting::TypeString)
{
    if (!(cfg.exists(name) && cfg.lookup(name).getType() == val_type) ||
        (val_type == Setting::TypeArray && cfg.lookup(name).getLength() > 0 && cfg.lookup(name)[0].getType() != list_type))
    {
        std::size_t pos = name.find_last_of(".", std::string::npos);
        std::cerr<<name<<" setting not specified or wrong type, using default value"<<std::endl;
        if (pos == std::string::npos)
        {
            try {
                cfg.getRoot().remove(name);
            } catch (const SettingNotFoundException& e) {}
            cfg.getRoot().add(name, val_type);
        }
        else
        {
            std::string group = name.substr(0, pos);
            std::string setting = name.substr(pos+1, std::string::npos);
            try {
                cfg.lookup(group).remove(setting);
            } catch (const SettingNotFoundException& e) {}
            cfg.lookup(group).add(setting, val_type);
        }
        try {
            if (!(val_type == Setting::TypeGroup || val_type == Setting::TypeArray || val_type == Setting::TypeList))
                cfg.lookup(name) = value;
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
        std::cerr<<"Warning: I/O error while reading configuration file (does config.cfg exists?), using default settings"<<std::endl;
    }
    catch(const ParseException &pex)
    {
        std::cerr<<"Warning: Configuration file parsing error at "<<pex.getFile()<<":"<<pex.getLine()<<" - "<<pex.getError()<<", using default settings"<<std::endl;
    }
    check_and_make_setting(config, "general", Setting::TypeGroup);
    check_and_make_setting(config, "general.preserve_permissions", Setting::TypeBoolean, true);
    check_and_make_setting(config, "general.encrypt_files", Setting::TypeBoolean, true);
    check_and_make_setting(config, "general.send_to_ftp", Setting::TypeBoolean, false);
    check_and_make_setting(config, "general.send_to_phone", Setting::TypeBoolean, false);
    check_and_make_setting(config, "general.copy_immediately_max_size", Setting::TypeInt64, 4096L);
    check_and_make_setting(config, "general.copy_directory", Setting::TypeString, "");

    check_and_make_setting(config, "filtering", Setting::TypeGroup);
    check_and_make_setting(config, "filtering.extensions", Setting::TypeGroup);
    check_and_make_setting(config, "filtering.extensions.filter_list", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting(config, "filtering.extensions.filtering_behavior", Setting::TypeString, "blacklist");
    check_and_make_setting(config, "filtering.programs", Setting::TypeGroup);
    check_and_make_setting(config, "filtering.programs.filter_list", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting(config, "filtering.programs.filtering_behavior", Setting::TypeString, "blacklist");
    check_and_make_setting(config, "filtering.filter", Setting::TypeString, "soft");

    check_and_make_setting(config, "monitoring", Setting::TypeGroup);
    check_and_make_setting(config, "monitoring.mounts", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting(config, "monitoring.directory_trees", Setting::TypeArray, 0, Setting::TypeString);
    check_and_make_setting(config, "monitoring.files_and_dirs", Setting::TypeArray, 0, Setting::TypeString);

    check_and_make_setting(config, "monitored_events", Setting::TypeGroup);
    check_and_make_setting(config, "monitored_events.access", Setting::TypeBoolean, true);
    check_and_make_setting(config, "monitored_events.open", Setting::TypeBoolean, true);
    check_and_make_setting(config, "monitored_events.modify", Setting::TypeBoolean, true);
    check_and_make_setting(config, "monitored_events.close_write", Setting::TypeBoolean, true);
    check_and_make_setting(config, "monitored_events.close_nowrite", Setting::TypeBoolean, true);
    check_and_make_setting(config, "monitored_events.close", Setting::TypeBoolean, true);
}

std::string get_user_perm()
{
    std::ifstream userfile("userperm", std::ifstream::in);
    std::string user;
    if (userfile.good())
        userfile>>user;
    else
        throw std::runtime_error("I/O error while reading user permission file (does userperm file exists?)");
    return user;
}

int drop_root()
{
    std::string name = user_perm;

    if (name.length() == 0)
        return -1;
    long const buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buflen == -1)
        return -1;
    // requires c99
    char buf[buflen];
    struct passwd pwbuf, *pwbufp;
    if (0 != getpwnam_r(name.c_str(), &pwbuf, buf, buflen, &pwbufp) || !pwbufp)
        return -1;

    int resgid = setgid(pwbufp->pw_gid);
    int resuid = setuid(pwbufp->pw_uid);
    if ((resgid == -1) || (resuid == -1))
        return -1;
    else return 0;
}

int
main(int          argc,
	const char **argv)
{
    try {
        user_perm = get_user_perm();
    } catch (const std::runtime_error& e) {
        std::cerr<<e.what()<<std::endl;
        exit(EXIT_FAILURE);
    }

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

    /* Drop root previleges to desired user */
    if (drop_root() < 0)
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

    if (argc > 0)
        app_launch_dir = argv[0];
    else app_launch_dir = app_name;

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
