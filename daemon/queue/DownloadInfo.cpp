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


#include "nzbget.h"
#include "DownloadInfo.h"
#include "ArticleWriter.h"
#include "DiskState.h"
#include "Options.h"
#include "Util.h"
#include "FileSystem.h"

int FileInfo::m_idGen = 0;
int FileInfo::m_idMax = 0;
int NzbInfo::m_idGen = 0;
int NzbInfo::m_idMax = 0;
DownloadQueue* DownloadQueue::g_DownloadQueue = nullptr;
bool DownloadQueue::g_Loaded = false;

NzbParameterList::~NzbParameterList()
{
	Clear();
}

void NzbParameterList::Clear()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
	clear();
}

void NzbParameterList::SetParameter(const char* name, const char* value)
{
	NzbParameter* parameter = nullptr;
	bool deleteObj = !value || !*value;

	for (iterator it = begin(); it != end(); it++)
	{
		NzbParameter* lookupParameter = *it;
		if (!strcmp(lookupParameter->GetName(), name))
		{
			if (deleteObj)
			{
				delete lookupParameter;
				erase(it);
				return;
			}
			parameter = lookupParameter;
			break;
		}
	}

	if (deleteObj)
	{
		return;
	}

	if (!parameter)
	{
		parameter = new NzbParameter(name);
		push_back(parameter);
	}

	parameter->SetValue(value);
}

NzbParameter* NzbParameterList::Find(const char* name, bool caseSensitive)
{
	for (iterator it = begin(); it != end(); it++)
	{
		NzbParameter* parameter = *it;
		if ((caseSensitive && !strcmp(parameter->GetName(), name)) ||
			(!caseSensitive && !strcasecmp(parameter->GetName(), name)))
		{
			return parameter;
		}
	}

	return nullptr;
}

void NzbParameterList::CopyFrom(NzbParameterList* sourceParameters)
{
	for (iterator it = sourceParameters->begin(); it != sourceParameters->end(); it++)
	{
		NzbParameter* parameter = *it;
		SetParameter(parameter->GetName(), parameter->GetValue());
	}
}


ScriptStatusList::~ScriptStatusList()
{
	Clear();
}

void ScriptStatusList::Clear()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
	clear();
}

void ScriptStatusList::Add(const char* scriptName, ScriptStatus::EStatus status)
{
	push_back(new ScriptStatus(scriptName, status));
}

ScriptStatus::EStatus ScriptStatusList::CalcTotalStatus()
{
	ScriptStatus::EStatus status = ScriptStatus::srNone;

	for (iterator it = begin(); it != end(); it++)
	{
		ScriptStatus* scriptStatus = *it;
		// Failure-Status overrides Success-Status
		if ((scriptStatus->GetStatus() == ScriptStatus::srSuccess && status == ScriptStatus::srNone) ||
			(scriptStatus->GetStatus() == ScriptStatus::srFailure))
		{
			status = scriptStatus->GetStatus();
		}
	}

	return status;
}


ServerStat::ServerStat(int serverId)
{
	m_serverId = serverId;
	m_successArticles = 0;
	m_failedArticles = 0;
}


ServerStatList::~ServerStatList()
{
	Clear();
}

void ServerStatList::Clear()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
	clear();
}

void ServerStatList::StatOp(int serverId, int successArticles, int failedArticles, EStatOperation statOperation)
{
	ServerStat* serverStat = nullptr;
	for (iterator it = begin(); it != end(); it++)
	{
		ServerStat* serverStat1 = *it;
		if (serverStat1->GetServerId() == serverId)
		{
			serverStat = serverStat1;
			break;
		}
	}

	if (!serverStat)
	{
		serverStat = new ServerStat(serverId);
		push_back(serverStat);
	}

	switch (statOperation)
	{
		case soSet:
			serverStat->SetSuccessArticles(successArticles);
			serverStat->SetFailedArticles(failedArticles);
			break;

		case soAdd:
			serverStat->SetSuccessArticles(serverStat->GetSuccessArticles() + successArticles);
			serverStat->SetFailedArticles(serverStat->GetFailedArticles() + failedArticles);
			break;

		case soSubtract:
			serverStat->SetSuccessArticles(serverStat->GetSuccessArticles() - successArticles);
			serverStat->SetFailedArticles(serverStat->GetFailedArticles() - failedArticles);
			break;
	}
}

void ServerStatList::ListOp(ServerStatList* serverStats, EStatOperation statOperation)
{
	for (iterator it = serverStats->begin(); it != serverStats->end(); it++)
	{
		ServerStat* serverStat = *it;
		StatOp(serverStat->GetServerId(), serverStat->GetSuccessArticles(), serverStat->GetFailedArticles(), statOperation);
	}
}


NzbInfo::NzbInfo() : m_fileList(true)
{
	debug("Creating NZBInfo");

	m_kind = nkNzb;
	m_url = "";
	m_filename = "";
	m_destDir = "";
	m_finalDir = "";
	m_category = "";
	m_name = nullptr;
	m_fileCount = 0;
	m_parkedFileCount = 0;
	m_size = 0;
	m_successSize = 0;
	m_failedSize = 0;
	m_currentSuccessSize = 0;
	m_currentFailedSize = 0;
	m_parSize = 0;
	m_parSuccessSize = 0;
	m_parFailedSize = 0;
	m_parCurrentSuccessSize = 0;
	m_parCurrentFailedSize = 0;
	m_totalArticles = 0;
	m_successArticles = 0;
	m_failedArticles = 0;
	m_currentSuccessArticles = 0;
	m_currentFailedArticles = 0;
	m_renameStatus = rsNone;
	m_parStatus = psNone;
	m_unpackStatus = usNone;
	m_cleanupStatus = csNone;
	m_moveStatus = msNone;
	m_deleteStatus = dsNone;
	m_markStatus = ksNone;
	m_urlStatus = lsNone;
	m_extraParBlocks = 0;
	m_addUrlPaused = false;
	m_deleting = false;
	m_deletePaused = false;
	m_manyDupeFiles = false;
	m_avoidHistory = false;
	m_healthPaused = false;
	m_parCleanup = false;
	m_cleanupDisk = false;
	m_unpackCleanedUpDisk = false;
	m_queuedFilename = "";
	m_dupeKey = "";
	m_dupeScore = 0;
	m_dupeMode = dmScore;
	m_fullContentHash = 0;
	m_filteredContentHash = 0;
	m_pausedFileCount = 0;
	m_remainingSize = 0;
	m_pausedSize = 0;
	m_remainingParCount = 0;
	m_minTime = 0;
	m_maxTime = 0;
	m_priority = 0;
	m_activeDownloads = 0;
	m_messages.clear();
	m_postInfo = nullptr;
	m_idMessageGen = 0;
	m_id = ++m_idGen;
	m_downloadedSize = 0;
	m_downloadSec = 0;
	m_postTotalSec = 0;
	m_parSec = 0;
	m_repairSec = 0;
	m_unpackSec = 0;
	m_downloadStartTime = 0;
	m_reprocess = false;
	m_queueScriptTime = 0;
	m_parFull = false;
	m_messageCount = 0;
	m_cachedMessageCount = 0;
	m_feedId = 0;
}

NzbInfo::~NzbInfo()
{
	debug("Destroying NZBInfo");

	delete m_postInfo;

	ClearCompletedFiles();

	m_fileList.Clear();
}

void NzbInfo::SetId(int id)
{
	m_id = id;
	if (m_idMax < m_id)
	{
		m_idMax = m_id;
	}
}

void NzbInfo::ResetGenId(bool max)
{
	if (max)
	{
		m_idGen = m_idMax;
	}
	else
	{
		m_idGen = 0;
		m_idMax = 0;
	}
}

int NzbInfo::GenerateId()
{
	return ++m_idGen;
}

void NzbInfo::ClearCompletedFiles()
{
	for (CompletedFiles::iterator it = m_completedFiles.begin(); it != m_completedFiles.end(); it++)
	{
		delete *it;
	}
	m_completedFiles.clear();
}

void NzbInfo::SetUrl(const char* url)
{
	m_url = url;

	if (!m_name)
	{
		CString nzbNicename = MakeNiceUrlName(url, m_filename);
		SetName(nzbNicename);
	}
}

void NzbInfo::SetFilename(const char* filename)
{
	bool hadFilename = !Util::EmptyStr(m_filename);
	m_filename = filename;

	if ((!m_name || !hadFilename) && !Util::EmptyStr(filename))
	{
		CString nzbNicename = MakeNiceNzbName(m_filename, true);
		SetName(nzbNicename);
	}
}

CString NzbInfo::MakeNiceNzbName(const char * nzbFilename, bool removeExt)
{
	CString nicename = FileSystem::BaseFileName(nzbFilename);
	if (removeExt)
	{
		// wipe out ".nzb"
		char* p = strrchr(nicename, '.');
		if (p && !strcasecmp(p, ".nzb")) *p = '\0';
	}
	FileSystem::MakeValidFilename(nicename, '_', false);
	return nicename;
}

CString NzbInfo::MakeNiceUrlName(const char* urlStr, const char* nzbFilename)
{
	CString urlNicename;
	URL url(urlStr);

	if (!Util::EmptyStr(nzbFilename))
	{
		CString nzbNicename = MakeNiceNzbName(nzbFilename, true);
		urlNicename.Format("%s @ %s", *nzbNicename, url.GetHost());
	}
	else if (url.IsValid())
	{
		urlNicename.Format("%s%s", url.GetHost(), url.GetResource());
	}
	else
	{
		urlNicename = urlStr;
	}

	return urlNicename;
}

void NzbInfo::BuildDestDirName()
{
	CString destDir;

	if (Util::EmptyStr(g_Options->GetInterDir()))
	{
		destDir = BuildFinalDirName();
	}
	else
	{
		destDir.Format("%s%s.#%i", g_Options->GetInterDir(), GetName(), GetId());
	}

	SetDestDir(destDir);
}

CString NzbInfo::BuildFinalDirName()
{
	CString finalDir = g_Options->GetDestDir();
	bool useCategory = m_category && m_category[0] != '\0';

	if (useCategory)
	{
		Options::Category *category = g_Options->FindCategory(m_category, false);
		if (category && category->GetDestDir() && category->GetDestDir()[0] != '\0')
		{
			finalDir = category->GetDestDir();
			useCategory = false;
		}
	}

	if (g_Options->GetAppendCategoryDir() && useCategory)
	{
		BString<1024> categoryDir;
		categoryDir = m_category;
		FileSystem::MakeValidFilename(categoryDir, '_', true);
		// we can't format using "finalDir.Format" because one of the parameter is "finalDir" itself.
		finalDir = BString<1024>("%s%s%c", *finalDir, *categoryDir, PATH_SEPARATOR);
	}

	finalDir.Append(GetName());

	return finalDir;
}

int NzbInfo::CalcHealth()
{
	if (m_currentFailedSize == 0 || m_size == m_parSize)
	{
		return 1000;
	}

	int health = (int)((m_size - m_parSize -
		(m_currentFailedSize - m_parCurrentFailedSize)) * 1000 / (m_size - m_parSize));

	if (health == 1000 && m_currentFailedSize - m_parCurrentFailedSize > 0)
	{
		health = 999;
	}

	return health;
}

int NzbInfo::CalcCriticalHealth(bool allowEstimation)
{
	if (m_size == 0)
	{
		return 1000;
	}

	if (m_size == m_parSize)
	{
		return 0;
	}

	int64 goodParSize = m_parSize - m_parCurrentFailedSize;
	int criticalHealth = (int)((m_size - goodParSize*2) * 1000 / (m_size - goodParSize));

	if (goodParSize*2 > m_size)
	{
		criticalHealth = 0;
	}
	else if (criticalHealth == 1000 && m_parSize > 0)
	{
		criticalHealth = 999;
	}

	if (criticalHealth == 1000 && allowEstimation)
	{
		// using empirical critical health 85%, to avoid false alarms for downloads with renamed par-files
		criticalHealth = 850;
	}

	return criticalHealth;
}

void NzbInfo::UpdateMinMaxTime()
{
	m_minTime = 0;
	m_maxTime = 0;

	bool first = true;
	for (FileList::iterator it = m_fileList.begin(); it != m_fileList.end(); it++)
	{
		FileInfo* fileInfo = *it;
		if (first)
		{
			m_minTime = fileInfo->GetTime();
			m_maxTime = fileInfo->GetTime();
			first = false;
		}
		if (fileInfo->GetTime() > 0)
		{
			if (fileInfo->GetTime() < m_minTime)
			{
				m_minTime = fileInfo->GetTime();
			}
			if (fileInfo->GetTime() > m_maxTime)
			{
				m_maxTime = fileInfo->GetTime();
			}
		}
	}
}

MessageList* NzbInfo::LockCachedMessages()
{
	m_logMutex.Lock();
	return &m_messages;
}

void NzbInfo::UnlockCachedMessages()
{
	m_logMutex.Unlock();
}

void NzbInfo::AddMessage(Message::EKind kind, const char * text)
{
	switch (kind)
	{
		case Message::mkDetail:
			detail("%s", text);
			break;

		case Message::mkInfo:
			info("%s", text);
			break;

		case Message::mkWarning:
			warn("%s", text);
			break;

		case Message::mkError:
			error("%s", text);
			break;

		case Message::mkDebug:
			debug("%s", text);
			break;
	}

	m_logMutex.Lock();
	Message* message = new Message(++m_idMessageGen, kind, time(nullptr), text);
	m_messages.push_back(message);

	if (g_Options->GetSaveQueue() && g_Options->GetServerMode() && g_Options->GetNzbLog())
	{
		g_DiskState->AppendNzbMessage(m_id, kind, text);
		m_messageCount++;
	}

	while (m_messages.size() > (uint32)g_Options->GetLogBufferSize())
	{
		Message* message = m_messages.front();
		delete message;
		m_messages.pop_front();
	}

	m_cachedMessageCount = m_messages.size();
	m_logMutex.Unlock();
}

void NzbInfo::PrintMessage(Message::EKind kind, const char* format, ...)
{
	char tmp2[1024];

	va_list ap;
	va_start(ap, format);
	vsnprintf(tmp2, 1024, format, ap);
	tmp2[1024-1] = '\0';
	va_end(ap);

	AddMessage(kind, tmp2);
}

void NzbInfo::ClearMessages()
{
	m_logMutex.Lock();
	m_messages.Clear();
	m_cachedMessageCount = 0;
	m_logMutex.Unlock();
}

void NzbInfo::CopyFileList(NzbInfo* srcNzbInfo)
{
	m_fileList.Clear();

	for (FileList::iterator it = srcNzbInfo->GetFileList()->begin(); it != srcNzbInfo->GetFileList()->end(); it++)
	{
		FileInfo* fileInfo = *it;
		fileInfo->SetNzbInfo(this);
		m_fileList.push_back(fileInfo);
	}

	srcNzbInfo->GetFileList()->clear(); // only remove references

	SetFullContentHash(srcNzbInfo->GetFullContentHash());
	SetFilteredContentHash(srcNzbInfo->GetFilteredContentHash());

	SetFileCount(srcNzbInfo->GetFileCount());
	SetPausedFileCount(srcNzbInfo->GetPausedFileCount());
	SetRemainingParCount(srcNzbInfo->GetRemainingParCount());

	SetSize(srcNzbInfo->GetSize());
	SetRemainingSize(srcNzbInfo->GetRemainingSize());
	SetPausedSize(srcNzbInfo->GetPausedSize());
	SetSuccessSize(srcNzbInfo->GetSuccessSize());
	SetCurrentSuccessSize(srcNzbInfo->GetCurrentSuccessSize());
	SetFailedSize(srcNzbInfo->GetFailedSize());
	SetCurrentFailedSize(srcNzbInfo->GetCurrentFailedSize());

	SetParSize(srcNzbInfo->GetParSize());
	SetParSuccessSize(srcNzbInfo->GetParSuccessSize());
	SetParCurrentSuccessSize(srcNzbInfo->GetParCurrentSuccessSize());
	SetParFailedSize(srcNzbInfo->GetParFailedSize());
	SetParCurrentFailedSize(srcNzbInfo->GetParCurrentFailedSize());

	SetTotalArticles(srcNzbInfo->GetTotalArticles());
	SetSuccessArticles(srcNzbInfo->GetSuccessArticles());
	SetFailedArticles(srcNzbInfo->GetFailedArticles());
	SetCurrentSuccessArticles(srcNzbInfo->GetSuccessArticles());
	SetCurrentFailedArticles(srcNzbInfo->GetFailedArticles());

	SetMinTime(srcNzbInfo->GetMinTime());
	SetMaxTime(srcNzbInfo->GetMaxTime());
}

void NzbInfo::EnterPostProcess()
{
	m_postInfo = new PostInfo();
	m_postInfo->SetNzbInfo(this);
}

void NzbInfo::LeavePostProcess()
{
	delete m_postInfo;
	m_postInfo = nullptr;
	ClearMessages();
}

void NzbInfo::SetActiveDownloads(int activeDownloads)
{
	if (((m_activeDownloads == 0 && activeDownloads > 0) ||
		 (m_activeDownloads > 0 && activeDownloads == 0)) &&
		m_kind == NzbInfo::nkNzb)
	{
		if (activeDownloads > 0)
		{
			m_downloadStartTime = time(nullptr);
		}
		else
		{
			m_downloadSec += time(nullptr) - m_downloadStartTime;
			m_downloadStartTime = 0;
		}
	}
	m_activeDownloads = activeDownloads;
}

bool NzbInfo::IsDupeSuccess()
{
	bool failure =
		m_markStatus != NzbInfo::ksSuccess &&
		m_markStatus != NzbInfo::ksGood &&
		(m_deleteStatus != NzbInfo::dsNone ||
		m_markStatus == NzbInfo::ksBad ||
		m_parStatus == NzbInfo::psFailure ||
		m_unpackStatus == NzbInfo::usFailure ||
		m_unpackStatus == NzbInfo::usPassword ||
		(m_parStatus == NzbInfo::psSkipped &&
		 m_unpackStatus == NzbInfo::usSkipped &&
		 CalcHealth() < CalcCriticalHealth(true)));
	return !failure;
}

const char* NzbInfo::MakeTextStatus(bool ignoreScriptStatus)
{
	const char* status = "FAILURE/INTERNAL_ERROR";

	if (m_kind == NzbInfo::nkNzb)
	{
		int health = CalcHealth();
		int criticalHealth = CalcCriticalHealth(false);
		ScriptStatus::EStatus scriptStatus = ignoreScriptStatus ? ScriptStatus::srSuccess : m_scriptStatuses.CalcTotalStatus();

		if (m_markStatus == NzbInfo::ksBad)
		{
			status = "FAILURE/BAD";
		}
		else if (m_markStatus == NzbInfo::ksGood)
		{
			status = "SUCCESS/GOOD";
		}
		else if (m_markStatus == NzbInfo::ksSuccess)
		{
			status = "SUCCESS/MARK";
		}
		else if (m_deleteStatus == NzbInfo::dsHealth)
		{
			status = "FAILURE/HEALTH";
		}
		else if (m_deleteStatus == NzbInfo::dsManual)
		{
			status = "DELETED/MANUAL";
		}
		else if (m_deleteStatus == NzbInfo::dsDupe)
		{
			status = "DELETED/DUPE";
		}
		else if (m_deleteStatus == NzbInfo::dsBad)
		{
			status = "FAILURE/BAD";
		}
		else if (m_deleteStatus == NzbInfo::dsGood)
		{
			status = "DELETED/GOOD";
		}
		else if (m_deleteStatus == NzbInfo::dsCopy)
		{
			status = "DELETED/COPY";
		}
		else if (m_deleteStatus == NzbInfo::dsScan)
		{
			status = "FAILURE/SCAN";
		}
		else if (m_parStatus == NzbInfo::psFailure)
		{
			status = "FAILURE/PAR";
		}
		else if (m_unpackStatus == NzbInfo::usFailure)
		{
			status = "FAILURE/UNPACK";
		}
		else if (m_moveStatus == NzbInfo::msFailure)
		{
			status = "FAILURE/MOVE";
		}
		else if (m_parStatus == NzbInfo::psManual)
		{
			status = "WARNING/DAMAGED";
		}
		else if (m_parStatus == NzbInfo::psRepairPossible)
		{
			status = "WARNING/REPAIRABLE";
		}
		else if ((m_parStatus == NzbInfo::psNone || m_parStatus == NzbInfo::psSkipped) &&
				 (m_unpackStatus == NzbInfo::usNone || m_unpackStatus == NzbInfo::usSkipped) &&
				 health < criticalHealth)
		{
			status = "FAILURE/HEALTH";
		}
		else if ((m_parStatus == NzbInfo::psNone || m_parStatus == NzbInfo::psSkipped) &&
				 (m_unpackStatus == NzbInfo::usNone || m_unpackStatus == NzbInfo::usSkipped) &&
				 health < 1000 && health >= criticalHealth)
		{
			status = "WARNING/HEALTH";
		}
		else if ((m_parStatus == NzbInfo::psNone || m_parStatus == NzbInfo::psSkipped) &&
				 (m_unpackStatus == NzbInfo::usNone || m_unpackStatus == NzbInfo::usSkipped) &&
				 scriptStatus != ScriptStatus::srFailure && health == 1000)
		{
			status = "SUCCESS/HEALTH";
		}
		else if (m_unpackStatus == NzbInfo::usSpace)
		{
			status = "WARNING/SPACE";
		}
		else if (m_unpackStatus == NzbInfo::usPassword)
		{
			status = "WARNING/PASSWORD";
		}
		else if ((m_unpackStatus == NzbInfo::usSuccess ||
				  ((m_unpackStatus == NzbInfo::usNone || m_unpackStatus == NzbInfo::usSkipped) &&
				   m_parStatus == NzbInfo::psSuccess)) &&
				 scriptStatus == ScriptStatus::srSuccess)
		{
			status = "SUCCESS/ALL";
		}
		else if (m_unpackStatus == NzbInfo::usSuccess && scriptStatus == ScriptStatus::srNone)
		{
			status = "SUCCESS/UNPACK";
		}
		else if (m_parStatus == NzbInfo::psSuccess && scriptStatus == ScriptStatus::srNone)
		{
			status = "SUCCESS/PAR";
		}
		else if (scriptStatus == ScriptStatus::srFailure)
		{
			status = "WARNING/SCRIPT";
		}
	}
	else if (m_kind == NzbInfo::nkUrl)
	{
		if (m_deleteStatus == NzbInfo::dsManual)
		{
			status = "DELETED/MANUAL";
		}
		else if (m_deleteStatus == NzbInfo::dsDupe)
		{
			status = "DELETED/DUPE";
		}
		else
		{
			const char* urlStatusName[] = { "FAILURE/INTERNAL_ERROR", "FAILURE/INTERNAL_ERROR", "FAILURE/INTERNAL_ERROR",
				"FAILURE/FETCH", "FAILURE/INTERNAL_ERROR", "WARNING/SKIPPED", "FAILURE/SCAN" };
			status = urlStatusName[m_urlStatus];
		}
	}

	return status;
}


NzbList::~NzbList()
{
	if (m_ownObjects)
	{
		Clear();
	}
}

void NzbList::Clear()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
	clear();
}

void NzbList::Add(NzbInfo* nzbInfo, bool addTop)
{
	if (addTop)
	{
		push_front(nzbInfo);
	}
	else
	{
		push_back(nzbInfo);
	}
}

void NzbList::Remove(NzbInfo* nzbInfo)
{
	iterator it = std::find(begin(), end(), nzbInfo);
	if (it != end())
	{
		erase(it);
	}
}

NzbInfo* NzbList::Find(int id)
{
	for (iterator it = begin(); it != end(); it++)
	{
		NzbInfo* nzbInfo = *it;
		if (nzbInfo->GetId() == id)
		{
			return nzbInfo;
		}
	}

	return nullptr;
}


ArticleInfo::ArticleInfo()
{
	//debug("Creating ArticleInfo");
	m_size = 0;
	m_segmentContent = nullptr;
	m_segmentOffset = 0;
	m_segmentSize = 0;
	m_status = aiUndefined;
	m_crc = 0;
}

ArticleInfo::~ ArticleInfo()
{
	//debug("Destroying ArticleInfo");
	DiscardSegment();
}

void ArticleInfo::AttachSegment(char* content, int64 offset, int size)
{
	DiscardSegment();
	m_segmentContent = content;
	m_segmentOffset = offset;
	m_segmentSize = size;
}

void ArticleInfo::DiscardSegment()
{
	if (m_segmentContent)
	{
		free(m_segmentContent);
		m_segmentContent = nullptr;
		g_ArticleCache->Free(m_segmentSize);
	}
}


FileInfo::FileInfo(int id)
{
	debug("Creating FileInfo");

	m_mutexOutputFile = nullptr;
	m_filenameConfirmed = false;
	m_size = 0;
	m_remainingSize = 0;
	m_missedSize = 0;
	m_successSize = 0;
	m_failedSize = 0;
	m_totalArticles = 0;
	m_missedArticles = 0;
	m_failedArticles = 0;
	m_successArticles = 0;
	m_time = 0;
	m_paused = false;
	m_deleted = false;
	m_completedArticles = 0;
	m_parFile = false;
	m_outputInitialized = false;
	m_nzbInfo = nullptr;
	m_extraPriority = false;
	m_activeDownloads = 0;
	m_autoDeleted = false;
	m_cachedArticles = 0;
	m_partialChanged = false;
	m_id = id ? id : ++m_idGen;
}

FileInfo::~ FileInfo()
{
	debug("Destroying FileInfo");

	delete m_mutexOutputFile;

	ClearArticles();
}

void FileInfo::ClearArticles()
{
	for (Articles::iterator it = m_articles.begin(); it != m_articles.end() ;it++)
	{
		delete *it;
	}
	m_articles.clear();
}

void FileInfo::SetId(int id)
{
	m_id = id;
	if (m_idMax < m_id)
	{
		m_idMax = m_id;
	}
}

void FileInfo::ResetGenId(bool max)
{
	if (max)
	{
		m_idGen = m_idMax;
	}
	else
	{
		m_idGen = 0;
		m_idMax = 0;
	}
}

void FileInfo::SetPaused(bool paused)
{
	if (m_paused != paused && m_nzbInfo)
	{
		m_nzbInfo->SetPausedFileCount(m_nzbInfo->GetPausedFileCount() + (paused ? 1 : -1));
		m_nzbInfo->SetPausedSize(m_nzbInfo->GetPausedSize() + (paused ? m_remainingSize : - m_remainingSize));
	}
	m_paused = paused;
}

void FileInfo::MakeValidFilename()
{
	FileSystem::MakeValidFilename(m_filename, '_', false);
}

void FileInfo::LockOutputFile()
{
	m_mutexOutputFile->Lock();
}

void FileInfo::UnlockOutputFile()
{
	m_mutexOutputFile->Unlock();
}

void FileInfo::SetActiveDownloads(int activeDownloads)
{
	m_activeDownloads = activeDownloads;

	if (m_activeDownloads > 0 && !m_mutexOutputFile)
	{
		m_mutexOutputFile = new Mutex();
	}
	else if (m_activeDownloads == 0 && m_mutexOutputFile)
	{
		delete m_mutexOutputFile;
		m_mutexOutputFile = nullptr;
	}
}


FileList::~FileList()
{
	if (m_ownObjects)
	{
		Clear();
	}
}

void FileList::Clear()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
	clear();
}

void FileList::Remove(FileInfo* fileInfo)
{
	erase(std::find(begin(), end(), fileInfo));
}


CompletedFile::CompletedFile(int id, const char* fileName, EStatus status, uint32 crc)
{
	m_id = id;

	if (FileInfo::m_idMax < m_id)
	{
		FileInfo::m_idMax = m_id;
	}

	m_fileName = fileName;
	m_status = status;
	m_crc = crc;
}


PostInfo::PostInfo()
{
	debug("Creating PostInfo");

	m_nzbInfo = nullptr;
	m_working = false;
	m_deleted = false;
	m_requestParCheck = false;
	m_forceParFull = false;
	m_forceRepair = false;
	m_parRepaired = false;
	m_unpackTried = false;
	m_passListTried = false;
	m_lastUnpackStatus = 0;
	m_progressLabel = "";
	m_fileProgress = 0;
	m_stageProgress = 0;
	m_startTime = 0;
	m_stageTime = 0;
	m_stage = ptQueued;
	m_postThread = nullptr;
}

PostInfo::~ PostInfo()
{
	debug("Destroying PostInfo");
}


DupInfo::DupInfo()
{
	m_id = 0;
	m_dupeScore = 0;
	m_dupeMode = dmScore;
	m_size = 0;
	m_fullContentHash = 0;
	m_filteredContentHash = 0;
	m_status = dsUndefined;
}

void DupInfo::SetId(int id)
{
	m_id = id;
	if (NzbInfo::m_idMax < m_id)
	{
		NzbInfo::m_idMax = m_id;
	}
}


HistoryInfo::HistoryInfo(NzbInfo* nzbInfo)
{
	m_kind = nzbInfo->GetKind() == NzbInfo::nkNzb ? hkNzb : hkUrl;
	m_info = nzbInfo;
	m_time = 0;
}

HistoryInfo::HistoryInfo(DupInfo* dupInfo)
{
	m_kind = hkDup;
	m_info = dupInfo;
	m_time = 0;
}

HistoryInfo::~HistoryInfo()
{
	if ((m_kind == hkNzb || m_kind == hkUrl) && m_info)
	{
		delete (NzbInfo*)m_info;
	}
	else if (m_kind == hkDup && m_info)
	{
		delete (DupInfo*)m_info;
	}
}

int HistoryInfo::GetId()
{
	if ((m_kind == hkNzb || m_kind == hkUrl))
	{
		return ((NzbInfo*)m_info)->GetId();
	}
	else // if (m_eKind == hkDup)
	{
		return ((DupInfo*)m_info)->GetId();
	}
}

const char* HistoryInfo::GetName()
{
	if (m_kind == hkNzb || m_kind == hkUrl)
	{
		return GetNzbInfo()->GetName();
	}
	else if (m_kind == hkDup)
	{
		return GetDupInfo()->GetName();
	}
	else
	{
		return "<unknown>";
	}
}


HistoryList::~HistoryList()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
}

HistoryInfo* HistoryList::Find(int id)
{
	for (iterator it = begin(); it != end(); it++)
	{
		HistoryInfo* historyInfo = *it;
		if (historyInfo->GetId() == id)
		{
			return historyInfo;
		}
	}

	return nullptr;
}


DownloadQueue* DownloadQueue::Lock()
{
	g_DownloadQueue->m_lockMutex.Lock();
	return g_DownloadQueue;
}

void DownloadQueue::Unlock()
{
	g_DownloadQueue->m_lockMutex.Unlock();
}

void DownloadQueue::CalcRemainingSize(int64* remaining, int64* remainingForced)
{
	int64 remainingSize = 0;
	int64 remainingForcedSize = 0;

	for (NzbList::iterator it = m_queue.begin(); it != m_queue.end(); it++)
	{
		NzbInfo* nzbInfo = *it;
		for (FileList::iterator it2 = nzbInfo->GetFileList()->begin(); it2 != nzbInfo->GetFileList()->end(); it2++)
		{
			FileInfo* fileInfo = *it2;
			if (!fileInfo->GetPaused() && !fileInfo->GetDeleted())
			{
				remainingSize += fileInfo->GetRemainingSize();
				if (nzbInfo->GetForcePriority())
				{
					remainingForcedSize += fileInfo->GetRemainingSize();
				}
			}
		}
	}

	*remaining = remainingSize;

	if (remainingForced)
	{
		*remainingForced = remainingForcedSize;
	}
}
