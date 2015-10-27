/*
 *  This file if part of nzbget
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


#ifndef NEWSSERVER_H
#define NEWSSERVER_H

#include <vector>
#include <time.h>

class NewsServer
{
private:
	int				m_id;
	int				m_stateId;
	bool			m_active;
	char*			m_name;
	int				m_group;
	char*			m_host;
	int				m_port;
	char*			m_user;
	char*			m_password;
	int				m_maxConnections;
	int				m_level;
	int				m_normLevel;
	bool			m_joinGroup;
	bool			m_tLS;
	char*			m_cipher;
	int				m_retention;
	time_t			m_blockTime;

public:
					NewsServer(int id, bool active, const char* name, const char* host, int port,
						const char* user, const char* pass, bool joinGroup,
						bool tLS, const char* cipher, int maxConnections, int retention,
						int level, int group);
					~NewsServer();
	int				GetID() { return m_id; }
	int				GetStateID() { return m_stateId; }
	void			SetStateID(int stateId) { m_stateId = stateId; }
	bool			GetActive() { return m_active; }
	void			SetActive(bool active) { m_active = active; }
	const char*		GetName() { return m_name; }
	int				GetGroup() { return m_group; }
	const char*		GetHost() { return m_host; }
	int				GetPort() { return m_port; }
	const char*		GetUser() { return m_user; }
	const char*		GetPassword() { return m_password; }
	int				GetMaxConnections() { return m_maxConnections; }
	int				GetLevel() { return m_level; }
	int				GetNormLevel() { return m_normLevel; }
	void			SetNormLevel(int level) { m_normLevel = level; }
	int				GetJoinGroup() { return m_joinGroup; }
	bool			GetTLS() { return m_tLS; }
	const char*		GetCipher() { return m_cipher; }
	int				GetRetention() { return m_retention; }
	time_t			GetBlockTime() { return m_blockTime; }
	void			SetBlockTime(time_t blockTime) { m_blockTime = blockTime; }
};

typedef std::vector<NewsServer*>		Servers;

#endif
