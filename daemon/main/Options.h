/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2004 Sven Henkel <sidddy@users.sourceforge.net>
 *  Copyright (C) 2007-2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * $Revision$
 * $Date$
 *
 */


#ifndef OPTIONS_H
#define OPTIONS_H

#include "NString.h"
#include "Thread.h"
#include "Util.h"

class Options
{
public:
	enum EWriteLog
	{
		wlNone,
		wlAppend,
		wlReset,
		wlRotate
	};
	enum EMessageTarget
	{
		mtNone,
		mtScreen,
		mtLog,
		mtBoth
	};
	enum EOutputMode
	{
		omLoggable,
		omColored,
		omNCurses
	};
	enum EParCheck
	{
		pcAuto,
		pcAlways,
		pcForce,
		pcManual
	};
	enum EParScan
	{
		psLimited,
		psExtended,
		psFull,
		psDupe
	};
	enum EHealthCheck
	{
		hcPause,
		hcDelete,
		hcNone
	};
	enum ESchedulerCommand
	{
		scPauseDownload,
		scUnpauseDownload,
		scPausePostProcess,
		scUnpausePostProcess,
		scDownloadRate,
		scScript,
		scProcess,
		scPauseScan,
		scUnpauseScan,
		scActivateServer,
		scDeactivateServer,
		scFetchFeed
	};

	class OptEntry
	{
	private:
		CString			m_name;
		CString			m_value;
		CString			m_defValue;
		int				m_lineNo;

		void			SetLineNo(int lineNo) { m_lineNo = lineNo; }

		friend class Options;

	public:
						OptEntry();
						OptEntry(const char* name, const char* value);
		void			SetName(const char* name) { m_name = name; }
		const char*		GetName() { return m_name; }
		void			SetValue(const char* value);
		const char*		GetValue() { return m_value; }
		const char*		GetDefValue() { return m_defValue; }
		int				GetLineNo() { return m_lineNo; }
		bool			Restricted();
	};

	typedef std::vector<OptEntry*>  OptEntriesBase;

	class OptEntries: public OptEntriesBase
	{
	public:
						~OptEntries();
		OptEntry*		FindOption(const char* name);
	};

	typedef std::vector<CString>  NameList;
	typedef std::vector<const char*>  CmdOptList;

	class Category
	{
	private:
		CString			m_name;
		CString			m_destDir;
		bool			m_unpack;
		CString			m_postScript;
		NameList		m_aliases;

	public:
						Category(const char* name, const char* destDir, bool unpack, const char* postScript);
		const char*		GetName() { return m_name; }
		const char*		GetDestDir() { return m_destDir; }
		bool			GetUnpack() { return m_unpack; }
		const char*		GetPostScript() { return m_postScript; }
		NameList*		GetAliases() { return &m_aliases; }
	};

	typedef std::vector<Category*>  CategoriesBase;

	class Categories: public CategoriesBase
	{
	public:
						~Categories();
		Category*		FindCategory(const char* name, bool searchAliases);
	};

	class Extender
	{
	public:
		virtual void	AddNewsServer(int id, bool active, const char* name, const char* host,
							int port, const char* user, const char* pass, bool joinGroup,
							bool tls, const char* cipher, int maxConnections, int retention,
							int level, int group) = 0;
		virtual void	AddFeed(int id, const char* name, const char* url, int interval,
							const char* filter, bool backlog, bool pauseNzb, const char* category,
							int priority, const char* feedScript) {}
		virtual void	AddTask(int id, int hours, int minutes, int weekDaysBits, ESchedulerCommand command,
							const char* param) {}
		virtual void	SetupFirstStart() {}
	};

private:
	OptEntries			m_optEntries;
	Mutex				m_optEntriesMutex;
	Categories			m_categories;
	bool				m_noDiskAccess;
	bool				m_fatalError;
	Extender*			m_extender;

	// Options
	bool				m_configErrors;
	int					m_configLine;
	CString				m_appDir;
	CString				m_configFilename;
	CString				m_destDir;
	CString				m_interDir;
	CString				m_tempDir;
	CString				m_queueDir;
	CString				m_nzbDir;
	CString				m_webDir;
	CString				m_configTemplate;
	CString				m_scriptDir;
	CString				m_requiredDir;
	EMessageTarget		m_infoTarget;
	EMessageTarget		m_warningTarget;
	EMessageTarget		m_errorTarget;
	EMessageTarget		m_debugTarget;
	EMessageTarget		m_detailTarget;
	bool				m_decode;
	bool				m_brokenLog;
	bool				m_nzbLog;
	int					m_articleTimeout;
	int					m_urlTimeout;
	int					m_terminateTimeout;
	bool				m_appendCategoryDir;
	bool				m_continuePartial;
	int					m_retries;
	int					m_retryInterval;
	bool				m_saveQueue;
	bool				m_flushQueue;
	bool				m_dupeCheck;
	CString				m_controlIp;
	CString				m_controlUsername;
	CString				m_controlPassword;
	CString				m_restrictedUsername;
	CString				m_restrictedPassword;
	CString				m_addUsername;
	CString				m_addPassword;
	int					m_controlPort;
	bool				m_secureControl;
	int					m_securePort;
	CString				m_secureCert;
	CString				m_secureKey;
	CString				m_authorizedIp;
	CString				m_lockFile;
	CString				m_daemonUsername;
	EOutputMode			m_outputMode;
	bool				m_reloadQueue;
	int					m_urlConnections;
	int					m_logBufferSize;
	EWriteLog			m_writeLog;
	int					m_rotateLog;
	CString				m_logFile;
	EParCheck			m_parCheck;
	bool				m_parRepair;
	EParScan			m_parScan;
	bool				m_parQuick;
	bool				m_parRename;
	int					m_parBuffer;
	int					m_parThreads;
	EHealthCheck		m_healthCheck;
	CString				m_postScript;
	CString				m_scriptOrder;
	CString				m_scanScript;
	CString				m_queueScript;
	CString				m_feedScript;
	bool				m_noConfig;
	int					m_umask;
	int					m_updateInterval;
	bool				m_cursesNzbName;
	bool				m_cursesTime;
	bool				m_cursesGroup;
	bool				m_crcCheck;
	bool				m_directWrite;
	int					m_writeBuffer;
	int					m_nzbDirInterval;
	int					m_nzbDirFileAge;
	bool				m_parCleanupQueue;
	int					m_diskSpace;
	bool				m_tls;
	bool				m_dumpCore;
	bool				m_parPauseQueue;
	bool				m_scriptPauseQueue;
	bool				m_nzbCleanupDisk;
	bool				m_deleteCleanupDisk;
	int					m_parTimeLimit;
	int					m_keepHistory;
	bool				m_accurateRate;
	bool				m_unpack;
	bool				m_unpackCleanupDisk;
	CString				m_unrarCmd;
	CString				m_sevenZipCmd;
	CString				m_unpackPassFile;
	bool				m_unpackPauseQueue;
	CString				m_extCleanupDisk;
	CString				m_parIgnoreExt;
	int					m_feedHistory;
	bool				m_urlForce;
	int					m_timeCorrection;
	int					m_propagationDelay;
	int					m_articleCache;
	int					m_eventInterval;

	// Current state
	bool				m_serverMode;
	bool				m_remoteClientMode;
	bool				m_pauseDownload;
	bool				m_pausePostProcess;
	bool				m_pauseScan;
	bool				m_tempPauseDownload;
	int					m_downloadRate;
	time_t				m_resumeTime;
	int					m_localTimeOffset;
	bool				m_tempPausePostprocess;

	void				Init(const char* exeName, const char* configFilename, bool noConfig,
							 CmdOptList* commandLineOptions, bool noDiskAccess, Extender* extender);
	void				InitDefaults();
	void				InitOptions();
	void				InitOptFile();
	void				InitServers();
	void				InitCategories();
	void				InitScheduler();
	void				InitFeeds();
	void				InitCommandLineOptions(CmdOptList* commandLineOptions);
	void				CheckOptions();
	void				Dump();
	int					ParseEnumValue(const char* OptName, int argc, const char* argn[], const int argv[]);
	int					ParseIntValue(const char* OptName, int base);
	OptEntry*			FindOption(const char* optname);
	const char*			GetOption(const char* optname);
	void				SetOption(const char* optname, const char* value);
	bool				SetOptionString(const char* option);
	bool				ValidateOptionName(const char* optname, const char* optvalue);
	void				LoadConfigFile();
	void				CheckDir(CString* dir, const char* optionName, const char* parentDir,
							bool allowEmpty, bool create);
	bool				ParseTime(const char* time, int* hours, int* minutes);
	bool				ParseWeekDays(const char* weekDays, int* weekDaysBits);
	void				ConfigError(const char* msg, ...);
	void				ConfigWarn(const char* msg, ...);
	void				LocateOptionSrcPos(const char *optionName);
	void				ConvertOldOption(char *option, int optionBufLen, char *value, int valueBufLen);

public:
						Options(const char* exeName, const char* configFilename, bool noConfig,
							CmdOptList* commandLineOptions, Extender* extender);
						Options(CmdOptList* commandLineOptions, Extender* extender);
						~Options();

	bool				SplitOptionString(const char* option, char** optName, char** optValue);
	bool				GetFatalError() { return m_fatalError; }
	OptEntries*			LockOptEntries();
	void				UnlockOptEntries();

	// Options
	const char*			GetConfigFilename() { return m_configFilename; }
	bool				GetConfigErrors() { return m_configErrors; }
	const char*			GetAppDir() { return m_appDir; }
	const char*			GetDestDir() { return m_destDir; }
	const char*			GetInterDir() { return m_interDir; }
	const char*			GetTempDir() { return m_tempDir; }
	const char*			GetQueueDir() { return m_queueDir; }
	const char*			GetNzbDir() { return m_nzbDir; }
	const char*			GetWebDir() { return m_webDir; }
	const char*			GetConfigTemplate() { return m_configTemplate; }
	const char*			GetScriptDir() { return m_scriptDir; }
	const char*			GetRequiredDir() { return m_requiredDir; }
	bool				GetBrokenLog() const { return m_brokenLog; }
	bool				GetNzbLog() const { return m_nzbLog; }
	EMessageTarget		GetInfoTarget() const { return m_infoTarget; }
	EMessageTarget		GetWarningTarget() const { return m_warningTarget; }
	EMessageTarget		GetErrorTarget() const { return m_errorTarget; }
	EMessageTarget		GetDebugTarget() const { return m_debugTarget; }
	EMessageTarget		GetDetailTarget() const { return m_detailTarget; }
	int					GetArticleTimeout() { return m_articleTimeout; }
	int					GetUrlTimeout() { return m_urlTimeout; }
	int					GetTerminateTimeout() { return m_terminateTimeout; }
	bool				GetDecode() { return m_decode; };
	bool				GetAppendCategoryDir() { return m_appendCategoryDir; }
	bool				GetContinuePartial() { return m_continuePartial; }
	int					GetRetries() { return m_retries; }
	int					GetRetryInterval() { return m_retryInterval; }
	bool				GetSaveQueue() { return m_saveQueue; }
	bool				GetFlushQueue() { return m_flushQueue; }
	bool				GetDupeCheck() { return m_dupeCheck; }
	const char*			GetControlIp() { return m_controlIp; }
	const char*			GetControlUsername() { return m_controlUsername; }
	const char*			GetControlPassword() { return m_controlPassword; }
	const char*			GetRestrictedUsername() { return m_restrictedUsername; }
	const char*			GetRestrictedPassword() { return m_restrictedPassword; }
	const char*			GetAddUsername() { return m_addUsername; }
	const char*			GetAddPassword() { return m_addPassword; }
	int					GetControlPort() { return m_controlPort; }
	bool				GetSecureControl() { return m_secureControl; }
	int					GetSecurePort() { return m_securePort; }
	const char*			GetSecureCert() { return m_secureCert; }
	const char*			GetSecureKey() { return m_secureKey; }
	const char*			GetAuthorizedIp() { return m_authorizedIp; }
	const char*			GetLockFile() { return m_lockFile; }
	const char*			GetDaemonUsername() { return m_daemonUsername; }
	EOutputMode			GetOutputMode() { return m_outputMode; }
	bool				GetReloadQueue() { return m_reloadQueue; }
	int					GetUrlConnections() { return m_urlConnections; }
	int					GetLogBufferSize() { return m_logBufferSize; }
	EWriteLog			GetWriteLog() { return m_writeLog; }
	const char*			GetLogFile() { return m_logFile; }
	int					GetRotateLog() { return m_rotateLog; }
	EParCheck			GetParCheck() { return m_parCheck; }
	bool				GetParRepair() { return m_parRepair; }
	EParScan			GetParScan() { return m_parScan; }
	bool				GetParQuick() { return m_parQuick; }
	bool				GetParRename() { return m_parRename; }
	int					GetParBuffer() { return m_parBuffer; }
	int					GetParThreads() { return m_parThreads; }
	EHealthCheck		GetHealthCheck() { return m_healthCheck; }
	const char*			GetScriptOrder() { return m_scriptOrder; }
	const char*			GetPostScript() { return m_postScript; }
	const char*			GetScanScript() { return m_scanScript; }
	const char*			GetQueueScript() { return m_queueScript; }
	const char*			GetFeedScript() { return m_feedScript; }
	int					GetUMask() { return m_umask; }
	int					GetUpdateInterval() {return m_updateInterval; }
	bool				GetCursesNzbName() { return m_cursesNzbName; }
	bool				GetCursesTime() { return m_cursesTime; }
	bool				GetCursesGroup() { return m_cursesGroup; }
	bool				GetCrcCheck() { return m_crcCheck; }
	bool				GetDirectWrite() { return m_directWrite; }
	int					GetWriteBuffer() { return m_writeBuffer; }
	int					GetNzbDirInterval() { return m_nzbDirInterval; }
	int					GetNzbDirFileAge() { return m_nzbDirFileAge; }
	bool				GetParCleanupQueue() { return m_parCleanupQueue; }
	int					GetDiskSpace() { return m_diskSpace; }
	bool				GetTls() { return m_tls; }
	bool				GetDumpCore() { return m_dumpCore; }
	bool				GetParPauseQueue() { return m_parPauseQueue; }
	bool				GetScriptPauseQueue() { return m_scriptPauseQueue; }
	bool				GetNzbCleanupDisk() { return m_nzbCleanupDisk; }
	bool				GetDeleteCleanupDisk() { return m_deleteCleanupDisk; }
	int					GetParTimeLimit() { return m_parTimeLimit; }
	int					GetKeepHistory() { return m_keepHistory; }
	bool				GetAccurateRate() { return m_accurateRate; }
	bool				GetUnpack() { return m_unpack; }
	bool				GetUnpackCleanupDisk() { return m_unpackCleanupDisk; }
	const char*			GetUnrarCmd() { return m_unrarCmd; }
	const char*			GetSevenZipCmd() { return m_sevenZipCmd; }
	const char*			GetUnpackPassFile() { return m_unpackPassFile; }
	bool				GetUnpackPauseQueue() { return m_unpackPauseQueue; }
	const char*			GetExtCleanupDisk() { return m_extCleanupDisk; }
	const char*			GetParIgnoreExt() { return m_parIgnoreExt; }
	int					GetFeedHistory() { return m_feedHistory; }
	bool				GetUrlForce() { return m_urlForce; }
	int					GetTimeCorrection() { return m_timeCorrection; }
	int					GetPropagationDelay() { return m_propagationDelay; }
	int					GetArticleCache() { return m_articleCache; }
	int					GetEventInterval() { return m_eventInterval; }

	Categories*			GetCategories() { return &m_categories; }
	Category*			FindCategory(const char* name, bool searchAliases) { return m_categories.FindCategory(name, searchAliases); }

	// Current state
	void				SetServerMode(bool serverMode) { m_serverMode = serverMode; }
	bool				GetServerMode() { return m_serverMode; }
	void				SetRemoteClientMode(bool remoteClientMode) { m_remoteClientMode = remoteClientMode; }
	bool				GetRemoteClientMode() { return m_remoteClientMode; }
	void				SetPauseDownload(bool pauseDownload) { m_pauseDownload = pauseDownload; }
	bool				GetPauseDownload() const { return m_pauseDownload; }
	void				SetPausePostProcess(bool pausePostProcess) { m_pausePostProcess = pausePostProcess; }
	bool				GetPausePostProcess() const { return m_pausePostProcess; }
	void				SetPauseScan(bool pauseScan) { m_pauseScan = pauseScan; }
	bool				GetPauseScan() const { return m_pauseScan; }
	void				SetTempPauseDownload(bool tempPauseDownload) { m_tempPauseDownload = tempPauseDownload; }
	bool				GetTempPauseDownload() const { return m_tempPauseDownload; }
	bool				GetTempPausePostprocess() const { return m_tempPausePostprocess; }
	void				SetTempPausePostprocess(bool tempPausePostprocess) { m_tempPausePostprocess = tempPausePostprocess; }
	void				SetDownloadRate(int rate) { m_downloadRate = rate; }
	int					GetDownloadRate() const { return m_downloadRate; }
	void				SetResumeTime(time_t resumeTime) { m_resumeTime = resumeTime; }
	time_t				GetResumeTime() const { return m_resumeTime; }
	void				SetLocalTimeOffset(int localTimeOffset) { m_localTimeOffset = localTimeOffset; }
	int					GetLocalTimeOffset() { return m_localTimeOffset; }
};

extern Options* g_Options;

#endif
