#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <string.h>
#include <utils.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <algorithm>
#include <libssh/libssh.h>
#include <libssh/ssh2.h>
#include <libssh/sftp.h>
#include <StringConfig.h>
#include <ScopeHelpers.h>
#include <WordExpansion.h>
#include <Threaded.h>
#include "ProtocolSFTP.h"


//////////////////////////////////////////////////////////////////////////////////////////////////
#define SIMULATED_GETMODE_FAILS_RATE 0
#define SIMULATED_GETSIZE_FAILS_RATE 0
#define SIMULATED_GETINFO_FAILS_RATE 0
#define SIMULATED_UNLINK_FAILS_RATE 0
#define SIMULATED_RMDIR_FAILS_RATE 0
#define SIMULATED_MKDIR_FAILS_RATE 0
#define SIMULATED_RENAME_FAILS_RATE 0
#define SIMULATED_ENUM_FAILS_RATE 0
#define SIMULATED_OPEN_FAILS_RATE 0
#define SIMULATED_READ_FAILS_RATE 0
#define SIMULATED_WRITE_FAILS_RATE 0
#define SIMULATED_WRITE_COMPLETE_FAILS_RATE 0
///////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<IProtocol> CreateProtocol(const std::string &protocol, const std::string &host, unsigned int port,
	const std::string &username, const std::string &password, const std::string &options) throw (std::runtime_error)
{
	return std::make_shared<ProtocolSFTP>(host, port, username, password, options);
}

// #define MAX_SFTP_IO_BLOCK_SIZE		((size_t)32768)	// googling shows than such block size compatible with at least most of servers

static void SSHSessionDeleter(ssh_session res)
{
	if (res) {
		ssh_disconnect(res);
		ssh_free(res);
	}
}

static void SFTPSessionDeleter(sftp_session res)
{
	if (res)
		sftp_free(res);
}

static void SFTPDirDeleter(sftp_dir res)
{
	if (res)
		sftp_closedir(res);
}

static void SFTPAttributesDeleter(sftp_attributes res)
{
	if (res)
		sftp_attributes_free(res);
}

static void SFTPFileDeleter(sftp_file res)
{
	if (res)
		sftp_close(res);
}

static void SFTPChannelDeleter(ssh_channel res)
{
	if (res) {
		ssh_channel_close(res);
		ssh_channel_free(res);
	}
}



#define RESOURCE_CONTAINER(CONTAINER, RESOURCE, DELETER) 		\
class CONTAINER {						\
	RESOURCE _res = {};							\
	CONTAINER(const CONTAINER &) = delete;	\
public:									\
	operator RESOURCE() { return _res; }				\
	RESOURCE operator ->() { return _res; }				\
	CONTAINER(RESOURCE res) : _res(res) {}			\
	~CONTAINER() { DELETER(_res); }				\
};

RESOURCE_CONTAINER(SFTPDir, sftp_dir, SFTPDirDeleter);
RESOURCE_CONTAINER(SFTPAttributes, sftp_attributes, SFTPAttributesDeleter);
RESOURCE_CONTAINER(SFTPFile, sftp_file, SFTPFileDeleter);
RESOURCE_CONTAINER(SFTPChannel, ssh_channel, SFTPChannelDeleter);

//RESOURCE_CONTAINER(SSHSession, ssh_session, SSHSessionDeleter);
//RESOURCE_CONTAINER(SFTPSession, sftp_session, SFTPSessionDeleter);

class ExecutedCommand;


struct SFTPConnection
{
	ssh_session ssh = nullptr;
	sftp_session sftp = nullptr;
	size_t max_read_block = 32768; // default value
	size_t max_write_block = 32768; // default value

	std::shared_ptr<ExecutedCommand> executed_command;
	std::map<std::string, std::string> env_set {{"TERM", "xterm"}};
	std::set<std::string> env_unset;

	~SFTPConnection()
	{
		if (sftp) {
			SFTPSessionDeleter(sftp);
		}

		if (ssh) {
			SSHSessionDeleter(ssh);
		}
	}
};

struct SFTPFileNonblockinScope
{
	SFTPFile &_file;

	SFTPFileNonblockinScope(SFTPFile &file) : _file(file)
	{
		sftp_file_set_nonblocking(_file);
	}

	~SFTPFileNonblockinScope()
	{
		sftp_file_set_blocking(_file);
	}
};

////////////////////////////

static std::string GetSSHPubkeyHash(ssh_session ssh)
{
	ssh_key pub_key = {};
	int rc = ssh_get_publickey(ssh, &pub_key);
	if (rc != SSH_OK)
		throw ProtocolError("Pubkey", ssh_get_error(ssh), rc);

	unsigned char *hash = nullptr;
	size_t hlen = 0;
	rc = ssh_get_publickey_hash(pub_key, SSH_PUBLICKEY_HASH_SHA1, &hash, &hlen);
	ssh_key_free(pub_key);

	if (rc != SSH_OK)
		throw ProtocolError("Pubkey hash", ssh_get_error(ssh), rc);

	std::string out;
	for (size_t i = 0; i < hlen; ++i) {
		out+= StrPrintf("%02x", (unsigned int)hash[i]);
	}
	ssh_clean_pubkey_hash(&hash);

	return out;
}

ProtocolSFTP::ProtocolSFTP(const std::string &host, unsigned int port,
	const std::string &username, const std::string &password, const std::string &options) throw (std::runtime_error)
	: _conn(std::make_shared<SFTPConnection>())
{
	_conn->ssh = ssh_new();
	if (!_conn->ssh)
		throw ProtocolError("SSH session");

	ssh_options_set(_conn->ssh, SSH_OPTIONS_HOST, host.c_str());
	if (port > 0)
		ssh_options_set(_conn->ssh, SSH_OPTIONS_PORT, &port);	

	ssh_options_set(_conn->ssh, SSH_OPTIONS_USER, &port);

	StringConfig protocol_options(options);

#if (LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 8, 0))
	if (protocol_options.GetInt("TcpNoDelay") ) {
		int nodelay = 1;
		ssh_options_set(_conn->ssh, SSH_OPTIONS_NODELAY, &nodelay);
	}
#endif

	_conn->max_read_block = (size_t)std::max(protocol_options.GetInt("MaxReadBlock", _conn->max_read_block), 512);
	_conn->max_write_block = (size_t)std::max(protocol_options.GetInt("MaxWriteBlock", _conn->max_write_block), 512);

	int ssh_verbosity = SSH_LOG_NOLOG;
	if (g_netrocks_verbosity == 2) {
		ssh_verbosity = SSH_LOG_WARNING;
	} else if (g_netrocks_verbosity > 2) {
		ssh_verbosity = SSH_LOG_PROTOCOL;
	}
	ssh_options_set(_conn->ssh, SSH_OPTIONS_LOG_VERBOSITY, &ssh_verbosity);

	ssh_key priv_key {};
	std::string key_path_spec;
	if (protocol_options.GetInt("PrivKeyEnable", 0) != 0) {
		key_path_spec = protocol_options.GetString("PrivKeyPath");
		WordExpansion we(key_path_spec);
		if (we.empty()) {
			throw std::runtime_error(StrPrintf("No key file specified: \'%s\'", key_path_spec.c_str()));
		}
		int key_import_result = -1;
		for (const auto &key_path : we) {
			key_import_result = ssh_pki_import_privkey_file(key_path.c_str(), password.c_str(), nullptr, nullptr, &priv_key);
			if (key_import_result == SSH_ERROR && password.empty()) {
				key_import_result = ssh_pki_import_privkey_file(key_path.c_str(), nullptr, nullptr, nullptr, &priv_key);
			}
			if (key_import_result == SSH_OK) {
				break;
			}
		}
		switch (key_import_result) {
			case SSH_EOF:
				throw std::runtime_error(StrPrintf("Cannot read key file \'%s\'", key_path_spec.c_str()));

			case SSH_ERROR:
				throw ProtocolAuthFailedError();

			case SSH_OK:
				break;

			default:
				throw std::runtime_error(
					StrPrintf("Unexpected error %u while loading key from \'%s\'",
					key_import_result, key_path_spec.c_str()));
		}
	}

	// TODO: seccomp: if (protocol_options.GetInt("Sandbox") ) ...

	int rc = ssh_connect(_conn->ssh);
	if (rc != SSH_OK)
		throw ProtocolError("Connection", ssh_get_error(_conn->ssh), rc);


	int socket_fd = ssh_get_fd(_conn->ssh);
	if (socket_fd != -1) {
#if (LIBSSH_VERSION_INT < SSH_VERSION_INT(0, 8, 0))
		if (protocol_options.GetInt("TcpNoDelay") ) {
			int nodelay = 1;
			if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (void *)&nodelay, sizeof(nodelay)) == -1) {
				perror("ProtocolSFTP - TCP_NODELAY");
			}
		}
#endif
		if (protocol_options.GetInt("TcpQuickAck") ) {
#ifdef TCP_QUICKACK
			int quickack = 1;
			if (setsockopt(socket_fd, IPPROTO_TCP, TCP_QUICKACK , (void *)&quickack, sizeof(quickack)) == -1) {
				perror("ProtocolSFTP - TCP_QUICKACK ");
			}
#else
			fprintf(stderr, "ProtocolSFTP: TCP_QUICKACK requested but not supported\n");
#endif
		}
	}

	const std::string &pub_key_hash = GetSSHPubkeyHash(_conn->ssh);
	if (pub_key_hash != protocol_options.GetString("ServerIdentity"))
		throw ServerIdentityMismatchError(pub_key_hash);

	if (priv_key) {
		rc = ssh_userauth_publickey(_conn->ssh, username.empty() ? nullptr : username.c_str(), priv_key);
		if (rc != SSH_AUTH_SUCCESS) {
			fprintf(stderr, "ssh_userauth_publickey: %d '%s'\n" , rc, ssh_get_error(_conn->ssh));
			throw std::runtime_error("Key file authentification failed");
		}

	} else {
		rc = ssh_userauth_password(_conn->ssh, username.empty() ? nullptr : username.c_str(), password.c_str());
  		if (rc != SSH_AUTH_SUCCESS)
			throw ProtocolAuthFailedError();//"Authentification failed", ssh_get_error(_conn->ssh), rc);
	}

	const std::string &subsystem = protocol_options.GetString("CustomSubsystem");
	if (!subsystem.empty() && protocol_options.GetInt("UseCustomSubsystem", 0) != 0) {
		ssh_channel channel = ssh_channel_new(_conn->ssh);
		if (channel == nullptr)
			throw ProtocolError("SSH channel new", ssh_get_error(_conn->ssh));

		int rc = ssh_channel_open_session(channel);
		if (rc != SSH_OK) {
			ssh_channel_free(channel);
			throw ProtocolError("SFTP channel open", ssh_get_error(_conn->ssh));
		}
		if (subsystem.find('/') == std::string::npos) {
			rc = ssh_channel_request_subsystem(channel, subsystem.c_str());
		} else {
			rc = ssh_channel_request_exec(channel, subsystem.c_str());
		}

		if (rc != SSH_OK) {
			ssh_channel_free(channel);
			throw ProtocolError("SFTP custom subsystem", ssh_get_error(_conn->ssh));
		}

		_conn->sftp = sftp_new_channel(_conn->ssh, channel);
		if (!_conn->sftp) {
			ssh_channel_free(channel);
			throw ProtocolError("SFTP channel", ssh_get_error(_conn->ssh));
		}

#if (LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 8, 3))
		if (!_conn->sftp->read_packet) {
			_conn->sftp->read_packet = (struct sftp_packet_struct *)calloc(1, sizeof(struct sftp_packet_struct));
			_conn->sftp->read_packet->payload = ssh_buffer_new();
		}
#endif
	} else {
		_conn->sftp = sftp_new(_conn->ssh);
	}

	if (_conn->sftp == nullptr)
		throw ProtocolError("SFTP session", ssh_get_error(_conn->ssh));

	rc = sftp_init(_conn->sftp);
	if (rc != SSH_OK)
		throw ProtocolError("SFTP init", ssh_get_error(_conn->ssh), rc);

	//_dir = directory;
}

ProtocolSFTP::~ProtocolSFTP()
{
	_conn->executed_command.reset();
}

bool ProtocolSFTP::IsBroken()
{
	return (!_conn || !_conn->ssh || !_conn->sftp || (ssh_get_status(_conn->ssh) & (SSH_CLOSED|SSH_CLOSED_ERROR)) != 0);
}

static sftp_attributes SFTPGetAttributes(sftp_session sftp, const std::string &path, bool follow_symlink) throw (std::runtime_error)
{
	sftp_attributes out = follow_symlink ? sftp_stat(sftp, path.c_str()) : sftp_lstat(sftp, path.c_str());
	if (!out)
		throw ProtocolError("stat", sftp_get_error(sftp));

	return out;
}

static mode_t SFTPModeFromAttributes(sftp_attributes attributes)
{
	mode_t out = attributes->permissions;
	switch (attributes->type) {
		case SSH_FILEXFER_TYPE_REGULAR: out|= S_IFREG; break;
		case SSH_FILEXFER_TYPE_DIRECTORY: out|= S_IFDIR; break;
		case SSH_FILEXFER_TYPE_SYMLINK: out|= S_IFLNK; break;
		case SSH_FILEXFER_TYPE_SPECIAL: out|= S_IFBLK; break;
		case SSH_FILEXFER_TYPE_UNKNOWN:
		default:
			break;
	}
	return out;
}

static void SftpFileInfoFromAttributes(FileInformation &file_info, sftp_attributes attributes)
{
	file_info.access_time.tv_sec = attributes->atime64 ? attributes->atime64 : attributes->atime;
	file_info.access_time.tv_nsec = attributes->atime_nseconds;
	file_info.modification_time.tv_sec = attributes->mtime64 ? attributes->mtime64 : attributes->mtime;
	file_info.modification_time.tv_nsec = attributes->mtime_nseconds;
	file_info.status_change_time.tv_sec = attributes->createtime;
	file_info.status_change_time.tv_nsec = attributes->createtime_nseconds;
	file_info.size = attributes->size;
	file_info.mode = SFTPModeFromAttributes(attributes);
}



mode_t ProtocolSFTP::GetMode(const std::string &path, bool follow_symlink) throw (std::runtime_error)
{
#if SIMULATED_GETMODE_FAILS_RATE
	if ( (rand() % 100) + 1 <= SIMULATED_GETMODE_FAILS_RATE)
		throw ProtocolError("Simulated getmode error");
#endif

	_conn->executed_command.reset();

	SFTPAttributes  attributes(SFTPGetAttributes(_conn->sftp, path, follow_symlink));
	return SFTPModeFromAttributes(attributes);
}

unsigned long long ProtocolSFTP::GetSize(const std::string &path, bool follow_symlink) throw (std::runtime_error)
{
#if SIMULATED_GETSIZE_FAILS_RATE
	if ( (rand() % 100) + 1 <= SIMULATED_GETSIZE_FAILS_RATE)
		throw ProtocolError("Simulated getsize error");
#endif

	_conn->executed_command.reset();

	SFTPAttributes  attributes(SFTPGetAttributes(_conn->sftp, path, follow_symlink));
	return attributes->size;
}

void ProtocolSFTP::GetInformation(FileInformation &file_info, const std::string &path, bool follow_symlink) throw (std::runtime_error)
{
#if SIMULATED_GETINFO_FAILS_RATE
	if ( (rand() % 100) + 1 <= SIMULATED_GETINFO_FAILS_RATE)
		throw ProtocolError("Simulated getinfo error");
#endif

	_conn->executed_command.reset();

	SFTPAttributes  attributes(SFTPGetAttributes(_conn->sftp, path, follow_symlink));
	SftpFileInfoFromAttributes(file_info, attributes);
}

void ProtocolSFTP::FileDelete(const std::string &path) throw (std::runtime_error)
{
#if SIMULATED_UNLINK_FAILS_RATE
	if ( (rand() % 100) + 1 <= SIMULATED_UNLINK_FAILS_RATE)
		throw ProtocolError("Simulated unlink error");
#endif

	_conn->executed_command.reset();

	int rc = sftp_unlink(_conn->sftp, path.c_str());
	if (rc != 0)
		throw ProtocolError(ssh_get_error(_conn->ssh), rc);
}

void ProtocolSFTP::DirectoryDelete(const std::string &path) throw (std::runtime_error)
{
#if SIMULATED_RMDIR_FAILS_RATE
	if ( (rand() % 100) + 1 <= SIMULATED_RMDIR_FAILS_RATE)
		throw ProtocolError("Simulated rmdir error");
#endif

	_conn->executed_command.reset();

	int rc = sftp_rmdir(_conn->sftp, path.c_str());
	if (rc != 0)
		throw ProtocolError(ssh_get_error(_conn->ssh), rc);
}

void ProtocolSFTP::DirectoryCreate(const std::string &path, mode_t mode) throw (std::runtime_error)
{
#if SIMULATED_MKDIR_FAILS_RATE
	if ( (rand() % 100) + 1 <= SIMULATED_MKDIR_FAILS_RATE)
		throw ProtocolError("Simulated mkdir error");
#endif

	_conn->executed_command.reset();

	int rc = sftp_mkdir(_conn->sftp, path.c_str(), mode);
	if (rc != 0)
		throw ProtocolError(ssh_get_error(_conn->ssh), rc);
}

void ProtocolSFTP::Rename(const std::string &path_old, const std::string &path_new) throw (std::runtime_error)
{
#if SIMULATED_RENAME_FAILS_RATE
	if ( (rand() % 100) + 1 <= SIMULATED_RENAME_FAILS_RATE)
		throw ProtocolError("Simulated rename error");
#endif

	_conn->executed_command.reset();

	int rc = sftp_rename(_conn->sftp, path_old.c_str(), path_new.c_str());
	if (rc != 0)
		throw ProtocolError(ssh_get_error(_conn->ssh), rc);
}

void ProtocolSFTP::SetTimes(const std::string &path, const timespec &access_time, const timespec &modification_time) throw (std::runtime_error)
{
	_conn->executed_command.reset();

	struct timeval times[2] = {};
	times[0].tv_sec = access_time.tv_sec;
	times[0].tv_usec = access_time.tv_nsec / 1000;
	times[1].tv_sec = modification_time.tv_sec;
	times[1].tv_usec = modification_time.tv_nsec / 1000;

	int rc = sftp_utimes(_conn->sftp, path.c_str(), times);
	if (rc != 0)
		throw ProtocolError(ssh_get_error(_conn->ssh), rc);
}

void ProtocolSFTP::SetMode(const std::string &path, mode_t mode) throw (std::runtime_error)
{
	_conn->executed_command.reset();

	int rc = sftp_chmod(_conn->sftp, path.c_str(), mode);
	if (rc != 0)
		throw ProtocolError(ssh_get_error(_conn->ssh), rc);
}


void ProtocolSFTP::SymlinkCreate(const std::string &link_path, const std::string &link_target) throw (std::runtime_error)
{
	_conn->executed_command.reset();

	int rc = sftp_symlink(_conn->sftp, link_target.c_str(), link_path.c_str());
	if (rc != 0)
		throw ProtocolError(ssh_get_error(_conn->ssh), rc);
}

void ProtocolSFTP::SymlinkQuery(const std::string &link_path, std::string &link_target) throw (std::runtime_error)
{
	_conn->executed_command.reset();

	char *target = sftp_readlink(_conn->sftp, link_path.c_str());
	if (target == NULL)
		throw ProtocolError(ssh_get_error(_conn->ssh));

	link_target = target;
	ssh_string_free_char(target);
}

class SFTPDirectoryEnumer : public IDirectoryEnumer
{
	std::shared_ptr<SFTPConnection> _conn;
	SFTPDir _dir;

public:
	SFTPDirectoryEnumer(std::shared_ptr<SFTPConnection> &conn, const std::string &path)
		: _conn(conn), _dir(sftp_opendir(conn->sftp, path.c_str()))
	{
		if (!_dir)
			throw ProtocolError(ssh_get_error(_conn->ssh));
	}

	virtual bool Enum(std::string &name, std::string &owner, std::string &group, FileInformation &file_info) throw (std::runtime_error)
	{
		for (;;) {
#if SIMULATED_ENUM_FAILS_RATE
		if ( (rand() % 100) + 1 <= SIMULATED_ENUM_FAILS_RATE)
			throw ProtocolError("Simulated enum dir error");
#endif

			SFTPAttributes  attributes(sftp_readdir(_conn->sftp, _dir));
			if (!attributes) {
				if (!sftp_dir_eof(_dir))
					throw ProtocolError(ssh_get_error(_conn->ssh));
				return false;
			}
			if (attributes->name == nullptr || attributes->name[0] == 0) {
				return false;
			}

			if (FILENAME_ENUMERABLE(attributes->name)) {
				name = attributes->name ? attributes->name : "";
				owner = attributes->owner ? attributes->owner : "";
				group = attributes->group ? attributes->group : "";

				SftpFileInfoFromAttributes(file_info, attributes);
				return true;
			}
		}
  	}
};

std::shared_ptr<IDirectoryEnumer> ProtocolSFTP::DirectoryEnum(const std::string &path) throw (std::runtime_error)
{
	_conn->executed_command.reset();

	return std::make_shared<SFTPDirectoryEnumer>(_conn, path);
}

class SFTPFileIO
{
protected:
	std::shared_ptr<SFTPConnection> _conn;
	SFTPFile _file;

public:
	SFTPFileIO(std::shared_ptr<SFTPConnection> &conn, const std::string &path, int flags, mode_t mode, unsigned long long resume_pos)
		: _conn(conn), _file(sftp_open(conn->sftp, path.c_str(), flags, mode))
	{
		if (!_file)
			throw ProtocolError("open",  ssh_get_error(_conn->ssh));

		if (resume_pos) {
			int rc = sftp_seek64(_file, resume_pos);
			if (rc != 0)
				throw ProtocolError("seek",  ssh_get_error(_conn->ssh), rc);
		}
	}
};

class SFTPFileReader : protected SFTPFileIO, public IFileReader
{
	// pipeline of read requests for which async_begin had been called but not yet replied
	std::deque<uint32_t> _pipeline;

	// partial bytes remained by last previous read that were not fit into caller's buffer
	std::vector<char> _partial;

	// count of blocks of size _conn->max_read_block that must be requested til reaching guessed EOF
	unsigned long long _unrequested_count = 0;

	// true if previous reply contained less bytes than requests
	bool _truncated_reply = false;

	int AsyncReadComplete(void *data)
	{
		int r = sftp_async_read(_file, data, _conn->max_read_block, _pipeline.front());
		if (r != SSH_AGAIN) {
			_pipeline.pop_front();
		}

		if (r >= 0) {
			// Fail if on some block server returned less data than was requested
			// and that truncated block appeared not to be last in the file
			// if this situation will be ignored then position of file's blocks
			// pipelined after truncated block was guessed incorrectly that will
			// lead to data corruption
			// It could be possile to silently recover from such situation by
			// discarding wrong blocks, adjusting position and reducing block size
			// however such automation would lead to performance degradation so just
			// let user know about that he needs to reduce selected read block size.
			if (r > 0 && _truncated_reply) {
				throw ProtocolError("Read block too big");
			}
			if ((size_t)r < _conn->max_read_block) {
				_truncated_reply = true;
			}
		}

		return r;
	}

	void EnsurePipelinedRequestsInner(size_t pipeline_size_limit)
	{
		while (_pipeline.size() < pipeline_size_limit
		 && (_unrequested_count || _pipeline.empty())) {
			_pipeline.emplace_back();
			int id = sftp_async_read_begin(_file, _conn->max_read_block);
			if (id < 0)  {
				_pipeline.pop_back();
				throw ProtocolError("sftp_async_read_begin", ssh_get_error(_conn->ssh));
			}
			_pipeline.back() = (uint32_t)id;
			if (_unrequested_count) {
				--_unrequested_count;
			}
		}
	}

	// - ensures that pipeline contains at least one request
	// - adds more requests to pipeline if its size less then limit and _unrequested_count not zero
	// - reduces_unrequested_count by amount of requested blocks
	void EnsurePipelinedRequests(size_t pipeline_size_limit)
	{
		for (;;) try {
			if (pipeline_size_limit > 16) {
				EnsurePipelinedRequestsInner(16);
				SFTPFileNonblockinScope file_nonblock_scope(_file);
				EnsurePipelinedRequestsInner(pipeline_size_limit);
			} else {
				EnsurePipelinedRequestsInner(pipeline_size_limit);
			}
			break;

		} catch (std::exception &ex) {
			fprintf(stderr, "SFTPFileReader::EnqueueReadRequests: [%ld] %s\n", (unsigned long)_pipeline.size(), ex.what());
			if (!_pipeline.empty()) {
				break;
			}
			usleep(1000);
		}
	}

public:
	SFTPFileReader(std::shared_ptr<SFTPConnection> &conn, const std::string &path, unsigned long long resume_pos)
		: SFTPFileIO(conn, path, O_RDONLY, 0640, resume_pos)
	{
		SFTPAttributes  attributes(sftp_fstat(_file));
		if (attributes && attributes->size >= resume_pos) {
			_unrequested_count = attributes->size - resume_pos;
			_unrequested_count = (_unrequested_count / _conn->max_read_block) + ((_unrequested_count % _conn->max_read_block) ? 1 : 0);
		} else  {
			_unrequested_count = std::numeric_limits<unsigned long long>::max();
		}
		EnsurePipelinedRequests( (_unrequested_count >= 32) ? 32 : (size_t)_unrequested_count);
	}

	~SFTPFileReader()
	{
		if (!_pipeline.empty()) try {
			if (g_netrocks_verbosity > 0) {
				fprintf(stderr, "~SFTPFileReader: still pipelined %u\n", (unsigned int)_pipeline.size());
			}
			_partial.resize(_conn->max_read_block);
			do {
				AsyncReadComplete(&_partial[0]);
			} while (!_pipeline.empty());

		} catch (std::exception &ex) {
			fprintf(stderr, "~SFTPFileReader: %s\n", ex.what());
		}
	}


	virtual size_t Read(void *buf, size_t len) throw (std::runtime_error)
	{
#if SIMULATED_READ_FAILS_RATE
		if ( (rand() % 100) + 1 <= SIMULATED_READ_FAILS_RATE)
			throw ProtocolError("Simulated read file error");
#endif

		EnsurePipelinedRequests( (len / _conn->max_read_block) + ((len % _conn->max_read_block) ? 1 : 0) );

		size_t out = _partial.size();
		if (out) {
			out = std::min(len, out);
			memcpy(buf, &_partial[0], out);
			if (out < _partial.size()) {
				_partial.erase(_partial.begin(), _partial.begin() + out);
			} else {
				_partial.clear();
			}

			len-= out;
			buf = (char *)buf + out;
			if (len < _conn->max_read_block) {
				return out;
			}
		}

		assert(_partial.empty());

		bool failed = false;
		if (len < _conn->max_read_block) {
			_partial.resize(_conn->max_read_block);
			int r = AsyncReadComplete(&_partial[0]);
			if (r > 0) {
				_partial.resize((size_t)r);
				size_t rfit = std::min(_partial.size(), len);
				memcpy(buf, &_partial[0], rfit);
				_partial.erase(_partial.begin(), _partial.begin() + rfit);
				out+= rfit;
				len-= rfit;
				buf = (char *)buf + rfit;
			} else {
				_partial.clear();
				if (r < 0) {
					failed = true;
				}
			}

		} else {
			int r = AsyncReadComplete(buf);
			if (r < 0) {
				failed = true;

			} else if (r == 0) {
				;

			} else {
				out+= r;
				len-= r;
				buf = (char *)buf + r;

				if (!_pipeline.empty() && len >= _conn->max_read_block) {
					SFTPFileNonblockinScope file_nonblock_scope(_file);
					do {
						r = AsyncReadComplete(buf);
						if (r <= 0) {
							if (r != SSH_AGAIN) {
								if (r < 0) {
									failed = true;
								}
							}
							break;
						}

						out+= r;
						len-= r;
						buf = (char *)buf + r;
					} while (!_pipeline.empty() && len >= _conn->max_read_block);
				}
			}
		}

		if (failed && out == 0) {
			throw ProtocolError("read error",  ssh_get_error(_conn->ssh));
		}

		return out;
	}
};


class SFTPFileWriter : protected SFTPFileIO, public IFileWriter
{
public:
	SFTPFileWriter(std::shared_ptr<SFTPConnection> &conn, const std::string &path, int flags, mode_t mode, unsigned long long resume_pos)
		: SFTPFileIO(conn, path, flags, mode, resume_pos)
	{
	}

	virtual void Write(const void *buf, size_t len) throw (std::runtime_error)
	{
#if SIMULATED_WRITE_FAILS_RATE
		if ( (rand() % 100) + 1 <= SIMULATED_WRITE_FAILS_RATE)
			throw ProtocolError("Simulated write file error");
#endif
		// somewhy libssh doesnt have async write yet
		if (len > 0) for (;;) {
			size_t piece = (len >= _conn->max_write_block) ? _conn->max_write_block : len;
			ssize_t written = sftp_write(_file, buf, piece);

			if (written <= 0)
				throw ProtocolError("write error",  ssh_get_error(_conn->ssh));

			if (written >= len)
				break;

			len-= (size_t)written;
			buf = (const char *)buf + written;
		}
	}

	virtual void WriteComplete() throw (std::runtime_error)
	{
#if SIMULATED_WRITE_COMPLETE_FAILS_RATE
		if ( (rand() % 100) + 1 <= SIMULATED_WRITE_COMPLETE_FAILS_RATE)
			throw ProtocolError("Simulated write-complete file error");
#endif
		// what?
	}
};


std::shared_ptr<IFileReader> ProtocolSFTP::FileGet(const std::string &path, unsigned long long resume_pos) throw (std::runtime_error)
{
	_conn->executed_command.reset();

	return std::make_shared<SFTPFileReader>(_conn, path, resume_pos);
}

std::shared_ptr<IFileWriter> ProtocolSFTP::FilePut(const std::string &path, mode_t mode, unsigned long long resume_pos) throw (std::runtime_error)
{
	_conn->executed_command.reset();

	return std::make_shared<SFTPFileWriter>(_conn, path, O_WRONLY | O_CREAT | (resume_pos ? 0 : O_TRUNC), mode, resume_pos);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ExecutedCommand : protected Threaded
{
	std::shared_ptr<SFTPConnection> _conn;
	std::string _working_dir;
	std::string _command_line;
	std::string _fifo;
	SFTPChannel _channel;
	int _kickass[2] {-1, -1};
	bool _pty = false;
	bool _succeess = false;

	void OnReadFDIn(const char *buf, size_t len)
	{
		for (size_t ofs = 0; ofs < len; ) {
			uint32_t piece = (len - ofs >= 32768) ? 32768 : (uint32_t)len - ofs;
			ssize_t slen = ssh_channel_write(_channel, &buf[ofs], piece);
			if (slen <= 0) {
				throw std::runtime_error("channel write failed");
			}
			ofs+= std::min((size_t)slen, (size_t)piece);
		}
	}

	virtual void SendSignal(int sig)
	{
		const char *sig_name;
		switch (sig) {
			case SIGALRM: sig_name = "ALRM"; break;
			case SIGFPE: sig_name = "FPE"; break;
			case SIGHUP: sig_name = "HUP"; break;
			case SIGILL: sig_name = "ILL"; break;
			case SIGINT: sig_name = "INT"; break;
			case SIGKILL: sig_name = "KILL"; break;
			case SIGPIPE: sig_name = "PIPE"; break;
			case SIGQUIT: sig_name = "QUIT"; break;
			case SIGSEGV: sig_name = "SEGV"; break;
			case SIGTERM: sig_name = "TERM"; break;
			case SIGUSR1: sig_name = "USR1"; break;
			case SIGUSR2: sig_name = "USR2"; break;
			case SIGABRT: default: sig_name = "ABRT"; break;
		}
		int rc = ssh_channel_request_send_signal(_channel, sig_name);
		if (rc == SSH_ERROR ) {
			throw ProtocolError("ssh send signal",  ssh_get_error(_conn->ssh));
		}
	}


	void OnReadFDCtl(int fd)
	{
		ExecFIFO_CtlMsg m;
		if (ReadAll(fd, &m, sizeof(m)) != sizeof(m)) {
			throw std::runtime_error("ctl read failed");
		}

		switch (m.cmd) {
			case ExecFIFO_CtlMsg::CMD_PTY_SIZE: {
				if (_pty) {
					ssh_channel_change_pty_size(_channel, m.u.pty_size.cols, m.u.pty_size.rows);
				}
			} break;

			case ExecFIFO_CtlMsg::CMD_SIGNAL: {
				SendSignal(m.u.signum);
			} break;
		}
	}

	void IOLoop()
	{
		FDScope fd_err(open((_fifo + ".err").c_str(), O_WRONLY));
		FDScope fd_out(open((_fifo + ".out").c_str(), O_WRONLY));
		FDScope fd_in(open((_fifo + ".in").c_str(), O_RDONLY));
		FDScope fd_ctl(open((_fifo + ".ctl").c_str(), O_RDONLY));

		if (!fd_err.Valid() || !fd_out.Valid() || !fd_in.Valid() || !fd_ctl.Valid())
			throw ProtocolError("fifo");

		// get PTY size that is sent immediately
		OnReadFDCtl(fd_ctl);

		std::string cmd;
		if (!_working_dir.empty() && _working_dir != "." && _working_dir != "./") {
			cmd = _working_dir;
			QuoteCmdArg(cmd);
			cmd.insert(0, "cd ");
			cmd+= " && ";
		}

		for (const auto &i : _conn->env_set) {
			cmd+= "export ";
			cmd+= i.first;
			cmd+= "=";
			std::string val = i.second;
			QuoteCmdArg(val);
			cmd+= val;
			cmd+= " && ";
		}
		for (const auto &i : _conn->env_unset) {
			cmd+= "unset ";
			cmd+= i;
			cmd+= " && ";
		}


		cmd+= _command_line;

		int rc = ssh_channel_request_exec(_channel, cmd.c_str());
		if (rc != SSH_OK) {
			throw ProtocolError("ssh execute",  ssh_get_error(_conn->ssh));
		}

		fcntl(fd_in, F_SETFL, fcntl(fd_in, F_GETFL, 0) | O_NONBLOCK);

		for (unsigned int idle = 0;;) {
			char buf[0x8000];
			fd_set fdr, fde;
			FD_ZERO(&fdr);
			FD_ZERO(&fde);
			FD_SET(fd_ctl, &fdr);
			FD_SET(fd_in, &fdr);
			FD_SET(_kickass[0], &fdr);
			FD_SET(fd_ctl, &fde);
			FD_SET(fd_in, &fde);
			FD_SET(_kickass[0], &fde);
			struct timeval tv = {0, (idle > 1000) ? 100000 : ((idle > 0) ? 10000 : 0)};
			if (idle < 100000) {
				++idle;
			}
			int r = select(std::max(std::max((int)fd_ctl, (int)fd_in), _kickass[0]) + 1, &fdr, nullptr, &fde, &tv);
			if ( r < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;

				throw std::runtime_error("select failed");
			}

			if (FD_ISSET(_kickass[0], &fdr) || FD_ISSET(_kickass[0], &fde)) {
				throw std::runtime_error("got kickass");
			}

			if (FD_ISSET(fd_in, &fdr)) { // || FD_ISSET(fd_in, &fde)
				ssize_t rlen = read(fd_in, buf, sizeof(buf));
				if (rlen <= 0) {
					if (errno != EAGAIN) {
						throw std::runtime_error("fd_in read failed");
					}
				} else {
					OnReadFDIn(buf, (size_t)rlen);
					idle = 0;
				}
			}
			if (FD_ISSET(fd_ctl, &fdr)) {// || FD_ISSET(fd_ctl, &fde)
				OnReadFDCtl(fd_ctl);
				idle = 0;
			}

			if (ssh_channel_is_eof(_channel)) {
				int status = ssh_channel_get_exit_status(_channel);
				if (status == 0) {
					_succeess = true;
				}
				FDScope fd_status(open((_fifo + ".status").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600));
				if (fd_status.Valid()) {
					WriteAll(fd_status, &status, sizeof(status));
				}
				throw std::runtime_error("channel EOF");
			}

			int rlen = ssh_channel_read_nonblocking(_channel, buf, sizeof(buf), 0);
			if (rlen > 0) {
				if (WriteAll(fd_out, buf, rlen) != (size_t)rlen) {
					throw std::runtime_error("output write failed");
				}
				idle = 0;
			}

			rlen = ssh_channel_read_nonblocking(_channel, buf, sizeof(buf), 1);
			if (rlen > 0) {
				if (WriteAll(fd_err, buf, rlen) != (size_t)rlen) {
					throw std::runtime_error("error write failed");
				}
				idle = 0;
			}
		}
	}

	virtual void *ThreadProc()
	{
		try {
			fprintf(stderr, "ProtocolSFTP::ExecutedCommand: ENTERING\n");
			IOLoop();
			fprintf(stderr, "ProtocolSFTP::ExecutedCommand: LEAVING\n");
		} catch (std::exception &ex) {
			fprintf(stderr, "ProtocolSFTP::ExecutedCommand: %s\n", ex.what());
		}

		return nullptr;
	}

	public:
	ExecutedCommand(std::shared_ptr<SFTPConnection> &conn, const std::string &working_dir, const std::string &command_line, const std::string &fifo)
		:
		_conn(conn),
		_working_dir(working_dir),
		_command_line(command_line),
		_fifo(fifo),
		_channel(ssh_channel_new(conn->ssh))
	{
		if (!_channel)
			throw ProtocolError("ssh channel",  ssh_get_error(_conn->ssh));

		int rc = ssh_channel_open_session(_channel);
		if (rc != SSH_OK) {
			throw ProtocolError("ssh channel session",  ssh_get_error(_conn->ssh));
		}

		rc = ssh_channel_request_pty(_channel);
		if (rc == SSH_OK) {
			_pty = true;
		}

		if (pipe_cloexec(_kickass) == -1) {
			throw ProtocolError("pipe",  ssh_get_error(_conn->ssh));
		}

		fcntl(_kickass[1], F_SETFL, fcntl(_kickass[1], F_GETFL, 0) | O_NONBLOCK);

		if (!StartThread()) {
			CheckedCloseFDPair(_kickass);
			throw std::runtime_error("start thread");
		}
	}

	virtual ~ExecutedCommand()
	{
		if (!WaitThread(0)) {
			char c = 1;
			if (write (_kickass[1], &c, sizeof(c)) != 1) {
				perror("write kickass");
			}
			WaitThread();
		}
		CheckedCloseFDPair(_kickass);

		if (_succeess) {
			std::vector<std::string> parts;
			StrExplode(parts, _command_line, " ");
			if (parts.size() > 1) {
				if (parts[0] == "export") {
					std::vector<std::string> var_parts;
					StrExplode(var_parts, parts[1], "=");
					if (var_parts.size() > 0) {
						StrTrim(var_parts[0]);
						if (var_parts.size() > 1) {
							StrTrim(var_parts[1]);
							_conn->env_set[var_parts[0]] = var_parts[1];
						} else {
							_conn->env_set[var_parts[0]].clear();
						}
						_conn->env_unset.erase(var_parts[0]);
					}

				} else if (parts[0] == "unset") {
					StrTrim(parts[1]);
					_conn->env_unset.insert(parts[1]);
					_conn->env_set.erase(parts[1]);
				}
			}
		}
	}

};

void ProtocolSFTP::ExecuteCommand(const std::string &working_dir, const std::string &command_line, const std::string &fifo) throw (std::runtime_error)
{
	_conn->executed_command.reset();
	_conn->executed_command = std::make_shared<ExecutedCommand>(_conn, working_dir, command_line, fifo);
}
