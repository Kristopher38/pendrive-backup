#include "ftp.h"

FTPConnection::FTPConnection(char* ip, int port, const char* login, const char* password)
{
	szIP = ip;
	szLogin = login;
	szPassword = password;
	iPort = port;
	iCurrentRetry = 0;
}

void FTPConnection::SetTimeout(int timeout)
{
	iTimeout = timeout;
}

void FTPConnection::SetMaxRetry(int maxretry)
{
	iMaxRetry = maxretry;
}

int FTPConnection::GetSocket()
{
	return iSocket;
}

int FTPConnection::HostnameToIp(char * hostname, char* ip)
{
	struct hostent *he;
	struct in_addr **addr_list;
	int i;

	if ((he = gethostbyname(hostname)) == NULL)
		return 1;

	addr_list = (struct in_addr **) he->h_addr_list;

	for (i = 0; addr_list[i] != NULL; i++)
	{
		strcpy(ip, inet_ntoa(*addr_list[i]));
		return 0;
	}

	return 1;
}

int FTPConnection::CreateSocket(char* ip, int port)
{
	int sockfd;
	struct sockaddr_in serv_addr;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;

	memset(&serv_addr, '0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	char address[100];
	if (inet_pton(AF_INET, ip, (void *)(&(serv_addr.sin_addr.s_addr))) == 0)
		HostnameToIp(ip, address);
	else
		strcpy(address, ip);

	serv_addr.sin_addr.s_addr = inet_addr(address);

	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		return -1;
	return sockfd;
}

int FTPConnection::GetSO_ERROR(int fd) {
	int err = 1;
	socklen_t len = sizeof err;
	if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len))
		return 1;
	if (err)
		errno = err;
	return err;
}

void FTPConnection::CloseSocket(int fd) {
	if (fd >= 0) {
		GetSO_ERROR(fd);
		if (shutdown(fd, SHUT_RDWR) < 0)
			if (errno != ENOTCONN && errno != EINVAL)
				return;
		if (close(fd) < 0)
			return;
	}
}

char* FTPConnection::ReadSocket(int socket, int timeout)
{
	char recvBuff[128];
	int n = 0;
	int total_size = 0;
	memset(recvBuff, '0', sizeof(recvBuff));
	char* recvText = NULL;
	char* recv_tmp_Text = NULL;
	double timediff;
	struct timeval begin, now;

	fcntl(socket, F_SETFL, O_NONBLOCK);

	gettimeofday(&begin, NULL);
	while (1)
	{
		gettimeofday(&now, NULL);

		if (total_size > 0 && timediff > timeout)
			return recvText;
		else if (timediff > timeout * 2)
			return recvText;
		n = recv(socket, recvBuff, sizeof(recvBuff) - 1, 0);
		if (n < 0)
			usleep(100000);
		else
		{
			gettimeofday(&begin, NULL);
			total_size += strlen(recvBuff);
			recvBuff[n] = 0;
			bool found = (strchr(recvBuff, '\n') != 0);
			if (recvText != NULL)
			{
				recv_tmp_Text = (char*)realloc(recvText, strlen(recvText) + strlen(recvBuff) + 1);
				if (recv_tmp_Text != NULL)
				{
					recvText = recv_tmp_Text;
					strcat(recvText, recvBuff);
				}
			}
			else
			{
				recvText = (char*)malloc(strlen(recvBuff) + 1);
				strcpy(recvText, recvBuff);
			}
			if (found)
				return recvText;
		}
	}
	return NULL;
}

bool FTPConnection::SendData(const char* tpl, ...)
{
	char buf[8192];

	va_list list;
	va_start(list, tpl);
	vsnprintf(buf, sizeof(buf) - 1, tpl, list);
	va_end(list);
	buf[sizeof(buf) - 1] = 0;

	if (send(iSocket, buf, strlen(buf), 0) < 0)
		return false;

	szBuffer = ReadSocket(iSocket, iTimeout);

	if(szBuffer != NULL)
	{
		std::string ServerMessage = std::string(szBuffer);

		if(ServerMessage.find("500") == 0)
			return false;
		else
			return true;
	}
	else
		return false;
}


bool FTPConnection::Login()
{
	bLastCommand = false;

	if (iSocket == -1)
		return false;

	if(SendData("CWD %s\r\n", "TEST"))
	{
        std::cout<<std::string(szBuffer)<<std::endl;
        std::cout<<std::string(szBuffer).find("530")<<std::endl;
		if(std::string(szBuffer).find("530") == std::string::npos)
		{
			if(SendData("USER %s\r\n", szLogin))
			{
				if(std::string(szBuffer).find("331") == std::string::npos)
				{
					if(SendData("PASS %s\r\n", szPassword))
					{
						if(std::string(szBuffer).find("230") == std::string::npos)
							return true;
						else if(std::string(szBuffer).find("530") == std::string::npos)
							return false;
                    }
                    else
                        return true;
                }

            }
        }
		else
			return true;
	}
	return false;
}

int FTPConnection::UploadFile(const char* file)
{
	bLastCommand = false;
	if (iSocket == -1)
		return CONNECTION_TIMEOUT;
	if (iPasvSocket != -1)
	{
		CloseSocket(iPasvSocket);
		iPasvSocket = -1;
	}
	if (!SendData("PASV\r\n"))
		return CONNECTION_TIMEOUT;

	if(std::string(szBuffer).find("530") == 0)
		return LOGIN_REQUIRED;
	if (std::string(szBuffer).find("227") != 0)
		return PASV_MODE_ERROR;

	const char *p1 = strstr(szBuffer, "(") + 1;
	const char *p2 = strstr(p1, ")");
	size_t len = p2 - p1;
	char *res = (char*)malloc(sizeof(char)*(len + 1));
	strncpy(res, p1, len);
	res[len] = '\0';

	int i = 0;
	char *p = strtok(res, ",");
	char *array[6];

	while (p != NULL)
	{
		array[i++] = p;
		p = strtok(NULL, ",");
	}
	int port = (atoi(array[4]) * 256) + atoi(array[5]);

	char* ip_tpl = "%s.%s.%s.%s";

	char* ip_buffer;
	ip_buffer = (char*)malloc(strlen(ip_tpl) + strlen(array[0]) + strlen(array[1]) + strlen(array[2]) + strlen(array[3]) - 4);
	sprintf(ip_buffer, ip_tpl, array[0], array[1], array[2], array[3]);

	iPasvSocket = CreateSocket(ip_buffer, port);
	free(ip_buffer);
	free(res);
	if(iPasvSocket == -1)
		return CONNECTION_TIMEOUT;
	printf("### Uploading %s ###\n" , basename((char*)file));
	if(!SendData("STOR %s\r\n", basename((char*)file)))
	{
	    return CONNECTION_TIMEOUT;
	}
	FILE *fp;
	fp = fopen(file, "rb");
	int blocksize = 1024;
	char* block_buffer = (char *)malloc(blocksize);
	int readbytes = 0;
	int total_send = 0;
	int proc_complete = 0;

	struct stat st;
	stat(file, &st);
	int total_bytes = st.st_size;

	printf("%i% \n" , proc_complete);
	do
	{
		readbytes = fread(block_buffer, 1, blocksize, fp);
		if (send(iPasvSocket, block_buffer, readbytes, 0) < 0)
		{
			CloseSocket(iPasvSocket);
			iPasvSocket = -1;
			free(block_buffer);
			fclose(fp);
			return CONNECTION_TIMEOUT;
		}
		total_send += readbytes;
		int current_proc = (int)(((float)total_send / (float)total_bytes) * 100.f);
		if(current_proc != proc_complete)
		{
			proc_complete = current_proc;
			if(proc_complete > 100)
				proc_complete = 100;
			printf("%i% \n" , proc_complete);
		}
	} while (readbytes > 0);
	CloseSocket(iPasvSocket);
	iPasvSocket = -1;
	free(block_buffer);
	fclose(fp);
	szBuffer = ReadSocket(iSocket, iTimeout);
	if(szBuffer == NULL)
		return CONNECTION_TIMEOUT;
	if (!strstr(szBuffer, "226"))
		return PASV_MODE_ERROR;
	return NO_ERROR;
}

int FTPConnection::CreateDirectory(const char* directory)
{
	bLastCommand = false;

	if (iSocket == -1)
		return CONNECTION_TIMEOUT;

		if(SendData("MKD %s\r\n", directory))
	{
		if(std::string(szBuffer).find("530") == 0)
			return LOGIN_REQUIRED;
		if(std::string(szBuffer).find("550") == 0 && strstr(szBuffer, "exist"))
			return DIRECTORY_EXIST;
		if(std::string(szBuffer).find("257") == 0)
		{
			bLastCommand = true;
			return NO_ERROR;
		}
		if(std::string(szBuffer).find("550") == 0)
			return DIRECTORY_DOSENT_EXIST;
	}
	else
		return CONNECTION_TIMEOUT;
}

int FTPConnection::GoToDirectory(const char* directory, bool create_if_doenst_exist)
{
	bLastCommand = false;

	if (iSocket == -1)
		return CONNECTION_TIMEOUT;

	if(SendData("CWD %s\r\n", directory))
	{
		if(std::string(szBuffer).find("530") == 0)
			return LOGIN_REQUIRED;
		if(std::string(szBuffer).find("550") == 0 && !strstr(szBuffer, "exist"))
		{
			if(!create_if_doenst_exist)
				return DIRECTORY_DOSENT_EXIST;
			else
			{
				if((strchr(directory, '/') == 0))
				{
					int create = CreateDirectory(directory);
					if(create == NO_ERROR)
						return GoToDirectory(directory, false);
					else
						return create;
				}
				else
				{
					while(1)
					{
						std::string dir_str = std::string(directory);
						std::string last_dir = dir_str;
						int err = DIRECTORY_DOSENT_EXIST;
						while(err == DIRECTORY_DOSENT_EXIST)
						{
							std::size_t index = dir_str.find_last_of("/\\");
							dir_str = dir_str.substr(0, index);
							err = GoToDirectory(dir_str.c_str(), false);
							if(err == NO_ERROR)
							{
								std::size_t index_last = last_dir.find_last_of("/\\");
								int cre_err = CreateDirectory(last_dir.substr(index_last+1).c_str());
								if(cre_err != NO_ERROR && cre_err != DIRECTORY_EXIST)
									return cre_err;
							}
							last_dir = dir_str;
						}
						if(err != NO_ERROR)
							return err;
						int can = GoToDirectory(directory, false);
						if(can == NO_ERROR)
							return can;
					}
				}
			}
		}
		if(std::string(szBuffer).find("250") == 0)
		{
			bLastCommand = true;
			return NO_ERROR;
		}
	}
	else
		return CONNECTION_TIMEOUT;
}

bool FTPConnection::Connect()
{
	bLastCommand = false;

	if (iSocket != -1)
	{
		CloseSocket(iSocket);
		iSocket = -1;
	}
	if (iPasvSocket != -1)
	{
		CloseSocket(iPasvSocket);
		iPasvSocket = -1;
	}
	iSocket = CreateSocket(szIP, iPort);

	if (iSocket == -1)
		return false;

	szBuffer = ReadSocket(iSocket, iTimeout);

	if (szBuffer != NULL)
		bLastCommand = true;

	if(bLastCommand)
		bLastCommand = Login();

	return bLastCommand;
}

bool FTPConnection::RetryConnection()
{
	iCurrentRetry++;

	if(iCurrentRetry > iMaxRetry)
	{
		iCurrentRetry = 0;
		return false;
	}
	printf ("Connection failed! Reconnecting...\n");

	if(Connect())
	{
		iCurrentRetry = 0;
		return true;
	}
	else
	  return RetryConnection();
}


int FTPConnection::SendDirectory(const char* directory, const char* ftp_directory)
{
	DIR *dir = opendir(directory);
	if (dir)
	{
		std::string cur_dir_name(basename((char*)directory));

		std::string cur_ftp_dir_name(ftp_directory);

		if(cur_ftp_dir_name != "")
		{
			if(cur_ftp_dir_name != "/")
				cur_ftp_dir_name = cur_ftp_dir_name + "/";
		}
		std::string final_path = cur_ftp_dir_name + cur_dir_name;
		struct dirent *entry = readdir(dir);
		bool b_can = true;
		while (entry != NULL)
		{
			std::string dir_name(entry->d_name);

			std::string full_dir_name(directory);

			std::string file = full_dir_name + "/" + dir_name;

			if (entry->d_type == DT_DIR && dir_name != ".." && dir_name != ".")
			{
				if(SendDirectory(file.c_str(), final_path.c_str()) == -1)
					b_can = false;
			}
			else if (entry->d_type == DT_REG)
			{
				bool b_success = false;
				while(!b_success)
				{
					bool b_connectiontimeout = false;
					int err_dir = GoToDirectory(final_path.c_str(), true);
					if(err_dir == CONNECTION_TIMEOUT)
						b_connectiontimeout = true;
					if(!b_connectiontimeout)
					{
						int err_upload = UploadFile(file.c_str());
						if(err_upload == CONNECTION_TIMEOUT)
							b_connectiontimeout = true;
					}
					if(!b_connectiontimeout)
						b_success = true;
					else if(!RetryConnection())
					{
						closedir(dir);
						return -1;
					}
				}
			}
			if(!b_can)
				break;
			entry = readdir(dir);
		}
		closedir(dir);
		if(b_can)
			return 0;
		else
			return -1;
	}
	else
		return 0;
}
