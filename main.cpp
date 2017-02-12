/*
 *   File:   fanotify-example-mount.c
 *   Date:   Thu Nov 14 13:47:37 2013
 *   Author: Aleksander Morgado <aleksander@lanedo.com>
 *
 *   A simple tester of fanotify in the Linux kernel.
 *
 *   This program is released in the Public Domain.
 *
 *   Compile with:
 *     $> gcc -o fanotify-example-mount fanotify-example-mount.c
 *
 *   Run as:
 *     $> ./fanotify-example-mount /mount/path /another/mount/path ...
 */

/* Define _GNU_SOURCE, Otherwise we don't get O_LARGEFILE */
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
#include <vector>
#include <dirent.h>

//#include <linux/fanotify.h>
#include <sys/fanotify.h>

//mcrypt
#include <mcrypt.h>

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

/* Setup fanotify notifications (FAN) mask. All these defined in fanotify.h. */

static uint64_t event_mask =
  (FAN_MODIFY |         /* File modified */
   FAN_CLOSE_WRITE |    /* Writtable file closed */
   FAN_CLOSE_NOWRITE |  /* Unwrittable file closed */
   FAN_ONDIR |          /* We want to be reported of events in the directory */
   FAN_EVENT_ON_CHILD); /* We want to be reported of events in files of the directory */

/* Array of directories being monitored */
static monitored_t *monitors;
static int n_monitors;

std::string pendrive_dir;
char* encryption_key;

static char *
get_program_name_from_pid (int     pid,
                             char   *buffer,
                             size_t  buffer_size)
{
  int fd;
  ssize_t len;
  char *aux;

  /* Try to get program name by PID */
  sprintf (buffer, "/proc/%d/cmdline", pid);
  if ((fd = open (buffer, O_RDONLY)) < 0)
    return NULL;

  /* Read file contents into buffer */
  if ((len = read (fd, buffer, buffer_size - 1)) <= 0)
    {
      close (fd);
      return NULL;
    }
  close (fd);

  buffer[len] = '\0';
  aux = strstr (buffer, "^@");
  if (aux)
    *aux = '\0';

  return buffer;
}

static char *
get_file_path_from_fd (int     fd,
                         char   *buffer,
                         size_t  buffer_size)
{
  ssize_t len;

  if (fd <= 0)
    return NULL;

  sprintf (buffer, "/proc/self/fd/%d", fd);
  if ((len = readlink (buffer, buffer, buffer_size - 1)) < 0)
    return NULL;

  buffer[len] = '\0';
  return buffer;
}

static void
event_process (struct fanotify_event_metadata *event)
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
    printf ("\tFull event mask: %u\n", event->mask);
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
}

static void
shutdown_fanotify (int fanotify_fd)
{
  int i;

  for (i = 0; i < n_monitors; ++i)
    {
      /* Remove the mark, using same event mask as when creating it */
      fanotify_mark (fanotify_fd,
                     FAN_MARK_REMOVE,
                     event_mask,
                     AT_FDCWD,
                     monitors[i].path);
      free (monitors[i].path);
    }
  free (monitors);
  close (fanotify_fd);
}

static int
initialize_fanotify (int          argc,
                       const char **argv)
{
  int i;
  int fanotify_fd;

  /* Create new fanotify device */
  if ((fanotify_fd = fanotify_init (FAN_CLOEXEC|FAN_NONBLOCK,
                                    O_RDONLY | O_CLOEXEC | O_LARGEFILE | O_NOATIME)) < 0)
    {
      fprintf (stderr,
               "Couldn't setup new fanotify device: %s\n",
               strerror (errno));
      return -1;
    }

  /* Allocate array of monitor setups */
  n_monitors = argc - 2;
  monitors = (monitored_t*)malloc (n_monitors * sizeof (monitored_t));

  /* Loop all input directories, setting up marks */
  for (i = 0; i < n_monitors; ++i)
    {
      monitors[i].path = strdup (argv[i + 2]);
      /* Add new fanotify mark */
      if (fanotify_mark (fanotify_fd,
                         FAN_MARK_ADD | FAN_MARK_MOUNT,
                         event_mask,
                         AT_FDCWD,
                         monitors[i].path) < 0)
        {
          fprintf (stderr,
                   "Couldn't add monitor in mount '%s': '%s'\n",
                   monitors[i].path,
                   strerror (errno));
          return -1;
        }

      printf ("Started monitoring mount '%s'...\n",
              monitors[i].path);
    }

  return fanotify_fd;
}

static void
shutdown_signals (int signal_fd)
{
  close (signal_fd);
}

static int
initialize_signals (void)
{
  int signal_fd;
  sigset_t sigmask;

  /* We want to handle SIGINT and SIGTERM in the signal_fd, so we block them. */
  sigemptyset (&sigmask);
  sigaddset (&sigmask, SIGINT);
  sigaddset (&sigmask, SIGTERM);
  sigaddset (&sigmask, SIGUSR1);

  if (sigprocmask (SIG_BLOCK, &sigmask, NULL) < 0)
    {
      fprintf (stderr,
               "Couldn't block signals: '%s'\n",
               strerror (errno));
      return -1;
    }

  /* Get new FD to read signals from it */
  if ((signal_fd = signalfd (-1, &sigmask, 0)) < 0)
    {
      fprintf (stderr,
               "Couldn't setup signal FD: '%s'\n",
               strerror (errno));
      return -1;
    }

  return signal_fd;
}
bool replace(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);
    if(start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

char* encrypt_password(char* IV,char* key_org,char* buffer,bool is_decrypt)
{
  MCRYPT td = mcrypt_module_open("twofish", NULL, "cbc", NULL);
  int keysize = 32;
  char* key;
  key = (char *)calloc(1,keysize);
  //mhash_keygen( KEYGEN_MCRYPT, MHASH_MD5, key, keysize, NULL, 0, key_org, strlen(key_org));
  memmove(key,key_org,keysize);  
  int blocksize = 1024;
  char* block_buffer = (char *)malloc(blocksize);
  mcrypt_generic_init(td, key, keysize, IV);
  strncpy(block_buffer,buffer,blocksize);
   if(!is_decrypt)
       mcrypt_generic(td, block_buffer, blocksize);
   else
       mdecrypt_generic(td, block_buffer, blocksize);
  mcrypt_generic_deinit (td);
  mcrypt_module_close(td);
  return block_buffer;
}
int encrypt(
    char* IV, 
    char* key_org,
    const char* path,
    bool isDecrypt
){
  MCRYPT td = mcrypt_module_open("twofish", NULL, "cbc", NULL);
  int keysize = 32;
  char* key;
  key = (char *)calloc(1,keysize);
  //mhash_keygen( KEYGEN_MCRYPT, MHASH_MD5, key, keysize, NULL, 0, key_org, strlen(key_org));
  memmove(key,key_org,keysize);
  int blocksize = mcrypt_enc_get_block_size(td);
  char* block_buffer = (char *)malloc(blocksize);
  mcrypt_generic_init(td, key, keysize, IV);
  FILE *fileptr;
  FILE *write_ptr;
  fileptr = fopen(path, "rb");  
  std::string str(path);
  if(isDecrypt)
     replace(str,".enc","");
  else
     str = str + ".enc";
  write_ptr = fopen(str.c_str(),"wb");
int readbytes = 0;
do
{
   readbytes = fread(block_buffer,1,blocksize,fileptr);
   if(readbytes == blocksize)
   {
   if(!isDecrypt)
     mcrypt_generic(td, block_buffer, blocksize);
   else
     mdecrypt_generic(td, block_buffer, blocksize);
   fwrite(block_buffer,1,blocksize,write_ptr); 
   }else if(readbytes > 0)
   {
    fwrite(block_buffer,1,readbytes,write_ptr); 
   }
}while(readbytes > 0);  

  fclose(write_ptr);
  fclose(fileptr);
  mcrypt_generic_deinit (td);
  mcrypt_module_close(td);
  struct stat st;
  stat(path,&st);
  chmod(str.c_str(),st.st_mode);
  remove(path);
  return 0;
}

void crypt_file(const char* file,bool is_decrypt)
{
 char* IV = "ghlvkycdfncsoitd";
 encrypt(IV, encryption_key,file,is_decrypt);
}

void get_directory(const char* directory,bool is_decrypt)
{
    DIR *dir = opendir(directory);

    struct dirent *entry = readdir(dir);

    while (entry != NULL)
    {
	std::string str2(entry->d_name);
 	std::string str(directory);
	std::string file = str + "/" + str2;
        if (entry->d_type == DT_DIR && str2 != ".." && str2 != ".")
            get_directory(file.c_str(),is_decrypt);	
	else if (entry->d_type == DT_REG)
	    crypt_file(file.c_str(),is_decrypt);
        entry = readdir(dir);
    }

    closedir(dir);
}
void crypt_files(bool is_decrypt)
{
    get_directory(pendrive_dir.c_str(),is_decrypt);
    //get_directory("/home/kamil/Wideo",is_decrypt);
}

char *trim(char *str)
{
    size_t len = 0;
    char *frontp = str;
    char *endp = NULL;

    if( str == NULL ) { return NULL; }
    if( str[0] == '\0' ) { return str; }

    len = strlen(str);
    endp = str + len;

    /* Move the front and back pointers to address the first non-whitespace
     * characters from each end.
     */
    while( isspace((unsigned char) *frontp) ) { ++frontp; }
    if( endp != frontp )
    {
        while( isspace((unsigned char) *(--endp)) && endp != frontp ) {}
    }

    if( str + len - 1 != endp )
            *(endp + 1) = '\0';
    else if( frontp != str &&  endp == frontp )
            *str = '\0';

    /* Shift the string so that it starts at str so that if it's dynamically
     * allocated, we can still free it on the returned pointer.  Note the reuse
     * of endp to mean the front of the string buffer now.
     */
    endp = str;
    if( frontp != str )
    {
            while( *frontp ) { *endp++ = *frontp++; }
            *endp = '\0';
    }


    return str;
}

int
main (int          argc,
      const char **argv)
{
  int signal_fd;
  int fanotify_fd;
  struct pollfd fds[FD_POLL_MAX];
  if(access("enc.key",F_OK) == -1)
  {
    printf ("Please enter password to set\n");
    char pass[128];
    fgets(pass, sizeof(pass),stdin);
    char* IV = "ghlvkycdfncsoitd";
    char* enc_pass = encrypt_password(IV,pass,pass,false);
    FILE *write_ptr;
    write_ptr = fopen("enc.key","wb");
    fwrite(enc_pass,1,strlen(enc_pass),write_ptr); 
    fclose(write_ptr);
    encryption_key = trim(pass);
  }else
  {    
   enter_pass:
    printf ("Please enteer password\n");
    char pass[128];
    fgets(pass, sizeof(pass),stdin);
    char* IV = "ghlvkycdfncsoitd";
    char enc_pass_file[1024];
    FILE *fileptr;
    fileptr = fopen("enc.key", "rb");
    fread(enc_pass_file,1,1024,fileptr);
    fclose(fileptr);
    char* dec_pass_file = encrypt_password(IV,pass,enc_pass_file,true);
    std::string str(pass);
    std::string str2(dec_pass_file);
    if(str == str2)
    {
       printf("SUCCESS! DECRYPTING FILES...\n");
       char* cstr = new char[str.length() + 1];
       strcpy(cstr,str.c_str());
       encryption_key = trim(dec_pass_file);
       crypt_files(true);
       printf("COMPLETE!\n");
    }
    else
    {
       printf("FAILED TRY AGAIN\n");
       goto enter_pass;
    }  

 }
  /* Input arguments... */
  if (argc < 3)
    {
      fprintf (stderr, "Usage: %s pendrive_directory monitored_mount1 [monitored_mount2 ...]\n", argv[0]);
      exit (EXIT_FAILURE);
    }

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

  /* Setup polling */
  fds[FD_POLL_SIGNAL].fd = signal_fd;
  fds[FD_POLL_SIGNAL].events = POLLIN;
  fds[FD_POLL_FANOTIFY].fd = fanotify_fd;
  fds[FD_POLL_FANOTIFY].events = POLLIN;

  mode_t previous = umask(0);
  pendrive_dir = strdup(argv[1]);

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
              break;
            }

            if (fdsi.ssi_signo == SIGUSR1)
            {
                /* KOD SZYFROWANIA HERE */
		crypt_files(false);
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
                    char target_path[PATH_MAX];
                    get_file_path_from_fd(metadata->fd, target_path, PATH_MAX);

                    struct stat stat_target;
                    if (stat(target_path, &stat_target) == 0)
                        if (S_ISDIR(stat_target.st_mode))
                        {
                            metadata = FAN_EVENT_NEXT (metadata, length);
                            continue;
                        }

                    std::string source_path = std::string(target_path);
                    std::cout<<"Source path: "<<source_path<<std::endl;
                    /*if (source_path.find(pendrive_dir()) != std::string::npos) // ignore event if it comes from the pendrive
                    {
                        metadata = FAN_EVENT_NEXT (metadata, length);
                        continue;
                    }*/

                    source_path = source_path.substr(1, std::string::npos); // substr to remove root dir form the path
                    std::string source_path_nofile = source_path.substr(0, source_path.rfind('/') + 1); // remove the file name form the path

                    std::string pendrive_dir_str = pendrive_dir;
                    std::size_t pos = 0;
                    struct stat st;
                    std::string current_dir; // pendrive directory with appended current working directory (the one we are cheching for existence)
                    std::string full_path = pendrive_dir_str + source_path; // pendrive directory with full source directory with filename
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
                                fprintf(stderr, "Error stat(): %s, path: %s\n", strerror(errno), std::string("/"+source_dir).c_str());
                            //std::cout<<"Original folder stat: "<<std::oct<<st.st_mode<<std::endl;
                            mkdir(current_dir.c_str(), st.st_mode);
                            //stat(current_dir.c_str(), &st);
                            //std::cout<<"Resulting folder stat: "<<std::oct<<st.st_mode<<std::endl; // ...and use it to make folder on pendrive with identical permissions
                        }
                    }

                    struct stat stat_source;
                    fstat(metadata->fd, &stat_source);
                    int target = open(full_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, stat_source.st_mode);

                    if (target == -1)
                        fprintf(stderr, "Error open(): %s, path: %s\n", strerror(errno), full_path.c_str());
                    else
                    {
                        int ret = sendfile(target, metadata->fd, 0, stat_source.st_size); // copy the file metadata->fd to target path
                        if (ret == -1)
                            fprintf(stderr, "Error sendfile(): %s, path: %s\n", strerror(errno), full_path.c_str());
                        else if (ret != stat_source.st_size)
                        {
                            fprintf(stderr, "Error sendfile(): count of copied bytes mismatches the file size");
                            std::cout<<"File size: "<<stat_source.st_size<<", bytes written: "<<ret<<std::endl;
                        }
                    }
                    //event_process (metadata);
                    close(metadata->fd);
                    close(target);
                    metadata = FAN_EVENT_NEXT (metadata, length);
                }
            }
        }
    }
  /* Clean exit */
  shutdown_fanotify (fanotify_fd);
  shutdown_signals (signal_fd);

  printf ("Exiting fanotify example...\n");

  return EXIT_SUCCESS;
}
   