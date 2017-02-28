#include "filescopy.h"

using namespace libconfig;

std::string pendrive_dir;
const std::string app_name("pbackup");
std::string app_launch_dir;
std::set<std::string> files_to_copy;

int initialize_filecopier(int argc, const char** argv)
{
    if (argc > 0)
        app_launch_dir = argv[0];
    else app_launch_dir = app_name;

    char cwd[1024];
    std::string copy_dir = global_config.lookup("general.copy_directory");
    // check if copy directory is directory or exists
    if (!is_directory(copy_dir))
    {
        std::cerr<<"Copy directory isn't a directory or doesn't exist"<<std::endl;
        return -1;
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
        return -1;
    }
}

std::string get_program_name_from_pid(int pid)
{
    std::string program_name;
    std::string path = std::string("/proc/") + std::to_string(pid) + std::string("/cmdline");
    std::ifstream procfile(path, std::ifstream::in);
    if (procfile.good())
    {
        procfile>>program_name;
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
}

std::string get_file_path_from_fd(int fd)
{
    ssize_t len;
    char buffer[PATH_MAX];

	if (fd <= 0)
		return NULL;

	sprintf(buffer, "/proc/self/fd/%d", fd);
	if ((len = readlink(buffer, buffer, PATH_MAX - 1)) < 0)
		return NULL;

	buffer[len] = '\0';
	return std::string(buffer);
}

bool filter_out(std::string filter_config, std::string text_to_match)
{
    enum FILTER_TYPE {BLACKLIST = 0, WHITELIST = 1};
    FILTER_TYPE filter;
    std::string filtering_behaviour = global_config.lookup(std::string(filter_config + ".filtering_behavior"));
    if (filtering_behaviour == "whitelist")
        filter = WHITELIST;
    else if (filtering_behaviour == "blacklist")
        filter = BLACKLIST;
    else filter = BLACKLIST;

    Setting& filenames = global_config.lookup(std::string(filter_config + ".filter_list"));
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
        if (stat_path.st_size <= static_cast<int64_t>(global_config.lookup("general.copy_immediately_max_size")))
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
        if (global_config.lookup("general.preserve_permissions"))
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
        Setting& path = global_config.lookup(std::string("monitoring.") + path_order[i]);
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
    std::string source_path = get_file_path_from_fd(metadata->fd);
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

    // filter out event if path is a directory or if it comes from the pendrive or if it's generated by the program itself
    std::string program_name = get_program_name_from_pid(metadata->pid);
    if (is_directory(source_path) || source_path.find(pendrive_dir) != std::string::npos || program_name == app_name || program_name == app_launch_dir)
        return;

    // filter event based on extension filter from config
    if (global_config.lookup("filtering.extensions.filter_list").getLength() > 0)
    {
        std::string filename = source_path.substr(source_path.find_last_of("/") + 1, std::string::npos);
        filter_extension = filter_out("filtering.extensions", filename);
    }

    // filter event based on program filter from config
    if (global_config.lookup("filtering.programs.filter_list").getLength() > 0)
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
