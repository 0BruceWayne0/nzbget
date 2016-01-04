/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2013-2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
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
 * $Revision: 0 $
 * $Date$
 *
 */


#ifndef FEEDINFO_H
#define FEEDINFO_H

#include "NString.h"
#include "Util.h"
#include "DownloadInfo.h"

class FeedInfo
{
public:
	enum EStatus
	{
		fsUndefined,
		fsRunning,
		fsFinished,
		fsFailed
	};

private:
	int					m_id;
	CString				m_name;
	CString				m_url;
	int					m_interval;
	CString				m_filter;
	uint32				m_filterHash;
	bool				m_pauseNzb;
	CString				m_category;
	CString				m_feedScript;
	int					m_priority;
	time_t				m_lastUpdate;
	bool				m_preview;
	EStatus				m_status;
	CString				m_outputFilename;
	bool				m_fetch;
	bool				m_force;
	bool				m_backlog;

public:
						FeedInfo(int id, const char* name, const char* url, bool backlog, int interval,
							const char* filter, bool pauseNzb, const char* category, int priority,
							const char* feedScript);
	int					GetId() { return m_id; }
	const char*			GetName() { return m_name; }
	const char*			GetUrl() { return m_url; }
	int					GetInterval() { return m_interval; }
	const char*			GetFilter() { return m_filter; }
	uint32				GetFilterHash() { return m_filterHash; }
	bool				GetPauseNzb() { return m_pauseNzb; }
	const char*			GetCategory() { return m_category; }
	int					GetPriority() { return m_priority; }
	const char*			GetFeedScript() { return m_feedScript; }
	time_t				GetLastUpdate() { return m_lastUpdate; }
	void				SetLastUpdate(time_t lastUpdate) { m_lastUpdate = lastUpdate; }
	bool				GetPreview() { return m_preview; }
	void				SetPreview(bool preview) { m_preview = preview; }
	EStatus				GetStatus() { return m_status; }
	void				SetStatus(EStatus Status) { m_status = Status; }
	const char*			GetOutputFilename() { return m_outputFilename; }
	void 				SetOutputFilename(const char* outputFilename) { m_outputFilename = outputFilename; }
	bool				GetFetch() { return m_fetch; }
	void				SetFetch(bool fetch) { m_fetch = fetch; }
	bool				GetForce() { return m_force; }
	void				SetForce(bool force) { m_force = force; }
	bool				GetBacklog() { return m_backlog; }
	void				SetBacklog(bool backlog) { m_backlog = backlog; }
};

typedef std::deque<FeedInfo*> Feeds;

class FeedFilterHelper
{
public:
	virtual RegEx**		GetSeasonEpisodeRegEx() = 0;
	virtual void		CalcDupeStatus(const char* title, const char* dupeKey, char* statusBuf, int bufLen) = 0;
};

class FeedItemInfo
{
public:
	enum EStatus
	{
		isUnknown,
		isBacklog,
		isFetched,
		isNew
	};

	enum EMatchStatus
	{
		msIgnored,
		msAccepted,
		msRejected
	};

	class Attr
	{
	private:
		CString			m_name;
		CString			m_value;
	public:
						Attr(const char* name, const char* value) :
							m_name(name ? name : ""), m_value(value ? value : "") {}
		const char*		GetName() { return m_name; }
		const char*		GetValue() { return m_value; }
	};

	typedef std::deque<Attr>  AttributesBase;

	class Attributes: public AttributesBase
	{
	public:
		Attr*			Find(const char* name);
	};

private:
	CString				m_title;
	CString				m_filename;
	CString				m_url;
	time_t				m_time;
	int64				m_size;
	CString				m_category;
	int					m_imdbId;
	int					m_rageId;
	int					m_tvdbId;
	int					m_tvmazeId;
	CString				m_description;
	CString				m_season;
	CString				m_episode;
	int					m_seasonNum;
	int					m_episodeNum;
	bool				m_seasonEpisodeParsed;
	CString				m_addCategory;
	bool				m_pauseNzb;
	int					m_priority;
	EStatus				m_status;
	EMatchStatus		m_matchStatus;
	int					m_matchRule;
	CString				m_dupeKey;
	int					m_dupeScore;
	EDupeMode			m_dupeMode;
	CString				m_dupeStatus;
	FeedFilterHelper*	m_feedFilterHelper;
	Attributes			m_attributes;

	int					ParsePrefixedInt(const char *value);
	void				ParseSeasonEpisode();

public:
						FeedItemInfo();
						FeedItemInfo(FeedItemInfo&&) = delete; // catch performance issues
	void				SetFeedFilterHelper(FeedFilterHelper* feedFilterHelper) { m_feedFilterHelper = feedFilterHelper; }
	const char*			GetTitle() { return m_title; }
	void				SetTitle(const char* title) { m_title = title; }
	const char*			GetFilename() { return m_filename; }
	void				SetFilename(const char* filename) { m_filename = filename; }
	const char*			GetUrl() { return m_url; }
	void				SetUrl(const char* url) { m_url = url; }
	int64				GetSize() { return m_size; }
	void				SetSize(int64 size) { m_size = size; }
	const char*			GetCategory() { return m_category; }
	void				SetCategory(const char* category) { m_category = category; }
	int					GetImdbId() { return m_imdbId; }
	void				SetImdbId(int imdbId) { m_imdbId = imdbId; }
	int					GetRageId() { return m_rageId; }
	void				SetRageId(int rageId) { m_rageId = rageId; }
	int					GetTvdbId() { return m_tvdbId; }
	void				SetTvdbId(int tvdbId) { m_tvdbId = tvdbId; }
	int					GetTvmazeId() { return m_tvmazeId; }
	void				SetTvmazeId(int tvmazeId) { m_tvmazeId = tvmazeId; }
	const char*			GetDescription() { return m_description; }
	void				SetDescription(const char* description) { m_description = description ? description: ""; }
	const char*			GetSeason() { return m_season; }
	void				SetSeason(const char* season);
	const char*			GetEpisode() { return m_episode; }
	void				SetEpisode(const char* episode);
	int					GetSeasonNum();
	int					GetEpisodeNum();
	const char*			GetAddCategory() { return m_addCategory; }
	void				SetAddCategory(const char* addCategory) { m_addCategory = addCategory ? addCategory : ""; }
	bool				GetPauseNzb() { return m_pauseNzb; }
	void				SetPauseNzb(bool pauseNzb) { m_pauseNzb = pauseNzb; }
	int					GetPriority() { return m_priority; }
	void				SetPriority(int priority) { m_priority = priority; }
	time_t				GetTime() { return m_time; }
	void				SetTime(time_t time) { m_time = time; }
	EStatus				GetStatus() { return m_status; }
	void				SetStatus(EStatus status) { m_status = status; }
	EMatchStatus		GetMatchStatus() { return m_matchStatus; }
	void				SetMatchStatus(EMatchStatus matchStatus) { m_matchStatus = matchStatus; }
	int					GetMatchRule() { return m_matchRule; }
	void				SetMatchRule(int matchRule) { m_matchRule = matchRule; }
	const char*			GetDupeKey() { return m_dupeKey; }
	void				SetDupeKey(const char* dupeKey) { m_dupeKey = dupeKey ? dupeKey : ""; }
	void				AppendDupeKey(const char* extraDupeKey);
	void				BuildDupeKey(const char* rageId, const char* tvdbId, const char* tvmazeId, const char* series);
	int					GetDupeScore() { return m_dupeScore; }
	void				SetDupeScore(int dupeScore) { m_dupeScore = dupeScore; }
	EDupeMode			GetDupeMode() { return m_dupeMode; }
	void				SetDupeMode(EDupeMode dupeMode) { m_dupeMode = dupeMode; }
	const char*			GetDupeStatus();
	Attributes*			GetAttributes() { return &m_attributes; }
};

typedef std::deque<FeedItemInfo>	FeedItemInfosBase;

class FeedItemInfos : public FeedItemInfosBase
{
private:
	int					m_refCount;

public:
						FeedItemInfos();
	void				Retain();
	void				Release();
};

class FeedHistoryInfo
{
public:
	enum EStatus
	{
		hsUnknown,
		hsBacklog,
		hsFetched
	};

private:
	CString				m_url;
	EStatus				m_status;
	time_t				m_lastSeen;

public:
						FeedHistoryInfo(const char* url, EStatus status, time_t lastSeen) :
							m_url(url), m_status(status), m_lastSeen(lastSeen) {}
	const char*			GetUrl() { return m_url; }
	EStatus				GetStatus() { return m_status; }
	void				SetStatus(EStatus Status) { m_status = Status; }
	time_t				GetLastSeen() { return m_lastSeen; }
	void				SetLastSeen(time_t lastSeen) { m_lastSeen = lastSeen; }
};

typedef std::deque<FeedHistoryInfo> FeedHistoryBase;

class FeedHistory : public FeedHistoryBase
{
public:
	void				Remove(const char* url);
	FeedHistoryInfo*	Find(const char* url);
};

#endif
