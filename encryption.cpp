#include "encryption.h"

char* encryption_key;

void password_process()
{
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
		crypt_files(false, pendrive_dir);
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

				crypt_files(true, pendrive_dir);
				printf("COMPLETE!\n");
			}
			else
				printf("FAILED TRY AGAIN\n");
		}
	}
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

void crypt_files(bool is_decrypt, std::string directory)
{
	get_directory(directory.c_str(), is_decrypt);
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
