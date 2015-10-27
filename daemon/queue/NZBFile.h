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


#ifndef NZBFILE_H
#define NZBFILE_H

#include <list>

#include "DownloadInfo.h"

class NZBFile
{
public:
	typedef std::list<FileInfo*>	TempFileList;

private:
	NZBInfo*			m_nzbInfo;
	char*				m_fileName;
	char*				m_password;

	void				AddArticle(FileInfo* fileInfo, ArticleInfo* articleInfo);
	void				AddFileInfo(FileInfo* fileInfo);
	void				ParseSubject(FileInfo* fileInfo, bool TryQuotes);
	void				BuildFilenames();
	void				ProcessFiles();
	void				CalcHashes();
	bool				HasDuplicateFilenames();
	void				ReadPassword();
#ifdef WIN32
    bool 				ParseNZB(IUnknown* nzb);
	static void			EncodeURL(const char* filename, char* url, int bufLen);
#else
	FileInfo*			m_fileInfo;
	ArticleInfo*		m_article;
	char*				m_tagContent;
	int					m_tagContentLen;
	bool				m_ignoreNextError;
	bool				m_hasPassword;

	static void			SAX_StartElement(NZBFile* file, const char *name, const char **atts);
	static void			SAX_EndElement(NZBFile* file, const char *name);
	static void			SAX_characters(NZBFile* file, const char * xmlstr, int len);
	static void*		SAX_getEntity(NZBFile* file, const char * name);
	static void			SAX_error(NZBFile* file, const char *msg, ...);
	void				Parse_StartElement(const char *name, const char **atts);
	void				Parse_EndElement(const char *name);
	void				Parse_Content(const char *buf, int len);
#endif

public:
						NZBFile(const char* fileName, const char* category);
						~NZBFile();
	bool				Parse();
	const char* 		GetFileName() const { return m_fileName; }
	NZBInfo*			GetNZBInfo() { return m_nzbInfo; }
	const char*			GetPassword() { return m_password; }
	void				DetachNZBInfo() { m_nzbInfo = NULL; }

	void				LogDebugInfo();
};

#endif
