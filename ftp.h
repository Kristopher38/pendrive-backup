#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <cerrno>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>

enum FTP_ERROR
{
	CONNECTION_TIMEOUT = 0,
	LOGIN_REQUIRED,
	DIRECTORY_DOSENT_EXIST,
	DIRECTORY_EXIST,
	PASV_MODE_ERROR,
	NO_PERMISSIONS,
	NO_ERROR
};


class FTPConnection
{
	public:
		FTPConnection(char* ip, int port, const char* login, const char* password);
		void SetTimeout(int timeout);
		void SetMaxRetry(int maxretry);
		int GetSocket();
		bool Login();
		int UploadFile(const char* file);
		int CreateDirectory(const char* directory);
		int GoToDirectory(const char* directory, bool create_if_doenst_exist);
		bool Connect();
		bool RetryConnection();
		int SendDirectory(const char* directory, const char* ftp_directory);
	protected:
		const char* szLogin;
		const char* szPassword;
		char* szIP;
		char* szBuffer;
		int iPort;
		int iTimeout;
		int iMaxRetry;
		int iSocket;
		int iPasvSocket;
		int iCurrentRetry;
		bool bLastCommand;
		int HostnameToIp(char * hostname, char* ip);
		int CreateSocket(char* ip, int port);
		int GetSO_ERROR(int fd);
		void CloseSocket(int fd);
		char* ReadSocket(int socket, int timeout);
		bool SendData(const char* tpl, ...);
};