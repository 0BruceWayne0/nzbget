/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2004 Sven Henkel <sidddy@users.sourceforge.net>
 *  Copyright (C) 2007-2014 Andrey Prygunkov <hugbug@users.sourceforge.net>
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


#ifndef ARTICLEDOWNLOADER_H
#define ARTICLEDOWNLOADER_H

#include "Observer.h"
#include "DownloadInfo.h"
#include "Thread.h"
#include "NntpConnection.h"
#include "Decoder.h"
#include "ArticleWriter.h"

class ArticleDownloader : public Thread, public Subject
{
public:
	enum EStatus
	{
		adUndefined,
		adRunning,
		adWaiting,
		adFinished,
		adFailed,
		adRetry,
		adCrcError,
		adNotFound,
		adConnectError,
		adFatalError
	};

	class ArticleWriterImpl : public ArticleWriter
	{
	private:
		 ArticleDownloader*		m_owner;
	protected:
		virtual void	SetLastUpdateTimeNow() { m_owner->SetLastUpdateTimeNow(); }
	public:
		void			SetOwner(ArticleDownloader* owner) { m_owner = owner; }
	};

private:
	FileInfo*			m_fileInfo;
	ArticleInfo*		m_articleInfo;
	NntpConnection* 	m_connection;
	EStatus				m_status;
	Mutex			 	m_connectionMutex;
	char*				m_infoName;
	char				m_connectionName[250];
	char*				m_articleFilename;
	time_t				m_lastUpdateTime;
	Decoder::EFormat	m_format;
	YDecoder			m_yDecoder;
	UDecoder			m_uDecoder;
	ArticleWriterImpl	m_articleWriter;
	ServerStatList		m_serverStats;
	bool				m_writingStarted;
	int					m_downloadedSize;

	EStatus				Download();
	EStatus				DecodeCheck();
	void				FreeConnection(bool keepConnected);
	EStatus				CheckResponse(const char* response, const char* comment);
	void				SetStatus(EStatus status) { m_status = status; }
	bool				Write(char* line, int len);
	void				AddServerData();

public:
						ArticleDownloader();
	virtual				~ArticleDownloader();
	void				SetFileInfo(FileInfo* fileInfo) { m_fileInfo = fileInfo; }
	FileInfo*			GetFileInfo() { return m_fileInfo; }
	void				SetArticleInfo(ArticleInfo* articleInfo) { m_articleInfo = articleInfo; }
	ArticleInfo*		GetArticleInfo() { return m_articleInfo; }
	EStatus				GetStatus() { return m_status; }
	ServerStatList*		GetServerStats() { return &m_serverStats; }
	virtual void		Run();
	virtual void		Stop();
	bool				Terminate();
	time_t				GetLastUpdateTime() { return m_lastUpdateTime; }
	void				SetLastUpdateTimeNow() { m_lastUpdateTime = ::time(NULL); }
	const char* 		GetArticleFilename() { return m_articleFilename; }
	void				SetInfoName(const char* infoName);
	const char*			GetInfoName() { return m_infoName; }
	const char*			GetConnectionName() { return m_connectionName; }
	void				SetConnection(NntpConnection* connection) { m_connection = connection; }
	void				CompleteFileParts() { m_articleWriter.CompleteFileParts(); }
	int					GetDownloadedSize() { return m_downloadedSize; }

	void				LogDebugInfo();
};

#endif
