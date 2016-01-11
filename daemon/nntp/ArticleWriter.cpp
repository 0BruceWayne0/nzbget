/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2014-2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
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
#include "ArticleWriter.h"
#include "DiskState.h"
#include "Options.h"
#include "Log.h"
#include "Util.h"
#include "FileSystem.h"

ArticleWriter::ArticleWriter()
{
	debug("Creating ArticleWriter");

	m_resultFilename = nullptr;
	m_format = Decoder::efUnknown;
	m_articleData = nullptr;
	m_duplicate = false;
	m_flushing = false;
}

ArticleWriter::~ArticleWriter()
{
	debug("Destroying ArticleWriter");

	if (m_articleData)
	{
		free(m_articleData);
		g_ArticleCache->Free(m_articleSize);
	}

	if (m_flushing)
	{
		g_ArticleCache->UnlockFlush();
	}
}

void ArticleWriter::SetWriteBuffer(DiskFile& outFile, int recSize)
{
	if (g_Options->GetWriteBuffer() > 0)
	{
		outFile.SetWriteBuffer(recSize > 0 && recSize < g_Options->GetWriteBuffer() * 1024 ?
			recSize : g_Options->GetWriteBuffer() * 1024);
	}
}

void ArticleWriter::Prepare()
{
	BuildOutputFilename();
	m_resultFilename = m_articleInfo->GetResultFilename();
}

bool ArticleWriter::Start(Decoder::EFormat format, const char* filename, int64 fileSize,
	int64 articleOffset, int articleSize)
{
	m_outFile.Close();
	m_format = format;
	m_articleOffset = articleOffset;
	m_articleSize = articleSize ? articleSize : m_articleInfo->GetSize();
	m_articlePtr = 0;

	// prepare file for writing
	if (m_format == Decoder::efYenc)
	{
		if (g_Options->GetDupeCheck() &&
			m_fileInfo->GetNzbInfo()->GetDupeMode() != dmForce &&
			!m_fileInfo->GetNzbInfo()->GetManyDupeFiles())
		{
			m_fileInfo->LockOutputFile();
			bool outputInitialized = m_fileInfo->GetOutputInitialized();
			if (!g_Options->GetDirectWrite())
			{
				m_fileInfo->SetOutputInitialized(true);
			}
			m_fileInfo->UnlockOutputFile();
			if (!outputInitialized && filename &&
				FileSystem::FileExists(m_fileInfo->GetNzbInfo()->GetDestDir(), filename))
			{
				m_duplicate = true;
				return false;
			}
		}

		if (g_Options->GetDirectWrite())
		{
			m_fileInfo->LockOutputFile();
			if (!m_fileInfo->GetOutputInitialized())
			{
				if (!CreateOutputFile(fileSize))
				{
					m_fileInfo->UnlockOutputFile();
					return false;
				}
				m_fileInfo->SetOutputInitialized(true);
			}
			m_fileInfo->UnlockOutputFile();
		}
	}

	// allocate cache buffer
	if (g_Options->GetArticleCache() > 0 && g_Options->GetDecode() &&
		(!g_Options->GetDirectWrite() || m_format == Decoder::efYenc))
	{
		if (m_articleData)
		{
			free(m_articleData);
			g_ArticleCache->Free(m_articleSize);
		}

		m_articleData = (char*)g_ArticleCache->Alloc(m_articleSize);

		while (!m_articleData && g_ArticleCache->GetFlushing())
		{
			usleep(5 * 1000);
			m_articleData = (char*)g_ArticleCache->Alloc(m_articleSize);
		}

		if (!m_articleData)
		{
			detail("Article cache is full, using disk for %s", *m_infoName);
		}
	}

	if (!m_articleData)
	{
		bool directWrite = g_Options->GetDirectWrite() && m_format == Decoder::efYenc;
		const char* filename = directWrite ? m_outputFilename : m_tempFilename;
		if (!m_outFile.Open(filename, directWrite ? DiskFile::omReadWrite : DiskFile::omWrite))
		{
			m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
				"Could not %s file %s: %s", directWrite ? "open" : "create", filename,
				*FileSystem::GetLastErrorMessage());
			return false;
		}
		SetWriteBuffer(m_outFile, m_articleInfo->GetSize());

		if (g_Options->GetDirectWrite() && m_format == Decoder::efYenc)
		{
			m_outFile.Seek(m_articleOffset);
		}
	}

	return true;
}

bool ArticleWriter::Write(char* buffer, int len)
{
	if (g_Options->GetDecode())
	{
		m_articlePtr += len;
	}

	if (g_Options->GetDecode() && m_articleData)
	{
		if (m_articlePtr > m_articleSize)
		{
			detail("Decoding %s failed: article size mismatch", *m_infoName);
			return false;
		}
		memcpy(m_articleData + m_articlePtr - len, buffer, len);
		return true;
	}

	return m_outFile.Write(buffer, len) > 0;
}

void ArticleWriter::Finish(bool success)
{
	m_outFile.Close();

	if (!success)
	{
		FileSystem::DeleteFile(m_tempFilename);
		FileSystem::DeleteFile(m_resultFilename);
		return;
	}

	bool directWrite = g_Options->GetDirectWrite() && m_format == Decoder::efYenc;

	if (g_Options->GetDecode())
	{
		if (!directWrite && !m_articleData)
		{
			if (!FileSystem::MoveFile(m_tempFilename, m_resultFilename))
			{
				m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
					"Could not rename file %s to %s: %s", *m_tempFilename, m_resultFilename,
					*FileSystem::GetLastErrorMessage());
			}
		}

		FileSystem::DeleteFile(m_tempFilename);

		if (m_articleData)
		{
			if (m_articleSize != m_articlePtr)
			{
				m_articleData = (char*)g_ArticleCache->Realloc(m_articleData, m_articleSize, m_articlePtr);
			}
			g_ArticleCache->LockContent();
			m_articleInfo->AttachSegment(m_articleData, m_articleOffset, m_articlePtr);
			m_fileInfo->SetCachedArticles(m_fileInfo->GetCachedArticles() + 1);
			g_ArticleCache->UnlockContent();
			m_articleData = nullptr;
		}
		else
		{
			m_articleInfo->SetSegmentOffset(m_articleOffset);
			m_articleInfo->SetSegmentSize(m_articlePtr);
		}
	}
	else
	{
		// rawmode
		if (!FileSystem::MoveFile(m_tempFilename, m_resultFilename))
		{
			m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
				"Could not move file %s to %s: %s", *m_tempFilename, m_resultFilename,
				*FileSystem::GetLastErrorMessage());
		}
	}
}

/* creates output file and subdirectores */
bool ArticleWriter::CreateOutputFile(int64 size)
{
	if (g_Options->GetDirectWrite() && FileSystem::FileExists(m_outputFilename) &&
		FileSystem::FileSize(m_outputFilename) == size)
	{
		// keep existing old file from previous program session
		return true;
	}

	// delete eventually existing old file from previous program session
	FileSystem::DeleteFile(m_outputFilename);

	// ensure the directory exist
	BString<1024> destDir;
	destDir.Set(m_outputFilename, FileSystem::BaseFileName(m_outputFilename) - m_outputFilename);
	CString errmsg;

	if (!FileSystem::ForceDirectories(destDir, errmsg))
	{
		m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
			"Could not create directory %s: %s", *destDir, *errmsg);
		return false;
	}

	if (!FileSystem::CreateSparseFile(m_outputFilename, size, errmsg))
	{
		m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
			"Could not create file %s: %s", *m_outputFilename, *errmsg);
		return false;
	}

	return true;
}

void ArticleWriter::BuildOutputFilename()
{
	BString<1024> filename("%s%i.%03i", g_Options->GetTempDir(),
		m_fileInfo->GetId(), m_articleInfo->GetPartNumber());

	m_articleInfo->SetResultFilename(filename);
	m_tempFilename.Format("%s.tmp", *filename);

	if (g_Options->GetDirectWrite())
	{
		m_fileInfo->LockOutputFile();

		if (m_fileInfo->GetOutputFilename())
		{
			filename = m_fileInfo->GetOutputFilename();
		}
		else
		{
			filename.Format("%s%c%i.out.tmp", m_fileInfo->GetNzbInfo()->GetDestDir(),
				(int)PATH_SEPARATOR, m_fileInfo->GetId());
			m_fileInfo->SetOutputFilename(filename);
		}

		m_fileInfo->UnlockOutputFile();

		m_outputFilename = *filename;
	}
}

void ArticleWriter::CompleteFileParts()
{
	debug("Completing file parts");
	debug("ArticleFilename: %s", m_fileInfo->GetFilename());

	bool directWrite = g_Options->GetDirectWrite() && m_fileInfo->GetOutputInitialized();

	CString errmsg;
	BString<1024> nzbName;
	BString<1024> nzbDestDir;

	// the locking is needed for accessing the members of NZBInfo
	DownloadQueue::Lock();
	nzbName = m_fileInfo->GetNzbInfo()->GetName();
	nzbDestDir = m_fileInfo->GetNzbInfo()->GetDestDir();
	DownloadQueue::Unlock();

	BString<1024> infoFilename("%s%c%s", *nzbName, (int)PATH_SEPARATOR, m_fileInfo->GetFilename());

	bool cached = m_fileInfo->GetCachedArticles() > 0;

	if (!g_Options->GetDecode())
	{
		detail("Moving articles for %s", *infoFilename);
	}
	else if (directWrite && cached)
	{
		detail("Writing articles for %s", *infoFilename);
	}
	else if (directWrite)
	{
		detail("Checking articles for %s", *infoFilename);
	}
	else
	{
		detail("Joining articles for %s", *infoFilename);
	}

	// Ensure the DstDir is created
	if (!FileSystem::ForceDirectories(nzbDestDir, errmsg))
	{
		m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
			"Could not create directory %s: %s", *nzbDestDir, *errmsg);
		return;
	}

	CString ofn = FileSystem::MakeUniqueFilename(nzbDestDir, m_fileInfo->GetFilename());

	DiskFile outfile;
	BString<1024> tmpdestfile("%s.tmp", *ofn);

	if (g_Options->GetDecode() && !directWrite)
	{
		FileSystem::DeleteFile(tmpdestfile);
		if (!outfile.Open(tmpdestfile, DiskFile::omWrite))
		{
			m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
				"Could not create file %s: %s", *tmpdestfile, *FileSystem::GetLastErrorMessage());
			return;
		}
	}
	else if (directWrite && cached)
	{
		if (!outfile.Open(m_outputFilename, DiskFile::omReadWrite))
		{
			m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
				"Could not open file %s: %s", *m_outputFilename, *FileSystem::GetLastErrorMessage());
			return;
		}
		tmpdestfile = *m_outputFilename;
	}
	else if (!g_Options->GetDecode())
	{
		FileSystem::DeleteFile(tmpdestfile);
		if (!FileSystem::CreateDirectory(ofn))
		{
			m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
				"Could not create directory %s: %s", *ofn, *FileSystem::GetLastErrorMessage());
			return;
		}
	}

	if (outfile.Active())
	{
		SetWriteBuffer(outfile, 0);
	}

	if (cached)
	{
		g_ArticleCache->LockFlush();
		m_flushing = true;
	}

	static const int BUFFER_SIZE = 1024 * 64;
	char* buffer = nullptr;
	bool firstArticle = true;
	uint32 crc = 0;

	if (g_Options->GetDecode() && !directWrite)
	{
		buffer = (char*)malloc(BUFFER_SIZE);
	}

	for (ArticleInfo* pa : *m_fileInfo->GetArticles())
	{
		if (pa->GetStatus() != ArticleInfo::aiFinished)
		{
			continue;
		}

		if (g_Options->GetDecode() && !directWrite && pa->GetSegmentOffset() > -1 &&
			pa->GetSegmentOffset() > outfile.Position() && outfile.Position() > -1)
		{
			memset(buffer, 0, BUFFER_SIZE);
			while (pa->GetSegmentOffset() > outfile.Position() && outfile.Position() > -1 &&
				outfile.Write(buffer, std::min((int)(pa->GetSegmentOffset() - outfile.Position()), BUFFER_SIZE))) ;
		}

		if (pa->GetSegmentContent())
		{
			outfile.Seek(pa->GetSegmentOffset());
			outfile.Write(pa->GetSegmentContent(), pa->GetSegmentSize());
			pa->DiscardSegment();
			SetLastUpdateTimeNow();
		}
		else if (g_Options->GetDecode() && !directWrite)
		{
			DiskFile infile;
			if (pa->GetResultFilename() && infile.Open(pa->GetResultFilename(), DiskFile::omRead))
			{
				int cnt = BUFFER_SIZE;
				while (cnt == BUFFER_SIZE)
				{
					cnt = (int)infile.Read(buffer, BUFFER_SIZE);
					outfile.Write(buffer, cnt);
					SetLastUpdateTimeNow();
				}
				infile.Close();
			}
			else
			{
				m_fileInfo->SetFailedArticles(m_fileInfo->GetFailedArticles() + 1);
				m_fileInfo->SetSuccessArticles(m_fileInfo->GetSuccessArticles() - 1);
				m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
					"Could not find file %s for %s%c%s [%i/%i]",
					pa->GetResultFilename(), *nzbName, (int)PATH_SEPARATOR, m_fileInfo->GetFilename(),
					pa->GetPartNumber(), (int)m_fileInfo->GetArticles()->size());
			}
		}
		else if (!g_Options->GetDecode())
		{
			BString<1024> dstFileName("%s%c%03i", *ofn, (int)PATH_SEPARATOR, pa->GetPartNumber());
			if (!FileSystem::MoveFile(pa->GetResultFilename(), dstFileName))
			{
				m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
					"Could not move file %s to %s: %s", pa->GetResultFilename(),
					*dstFileName, *FileSystem::GetLastErrorMessage());
			}
		}

		if (m_format == Decoder::efYenc)
		{
			crc = firstArticle ? pa->GetCrc() : Util::Crc32Combine(crc, pa->GetCrc(), pa->GetSegmentSize());
			firstArticle = false;
		}
	}

	free(buffer);

	if (cached)
	{
		g_ArticleCache->UnlockFlush();
		m_flushing = false;
	}

	if (outfile.Active())
	{
		outfile.Close();
		if (!directWrite && !FileSystem::MoveFile(tmpdestfile, ofn))
		{
			m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
				"Could not move file %s to %s: %s", *tmpdestfile, *ofn,
				*FileSystem::GetLastErrorMessage());
		}
	}

	if (directWrite)
	{
		if (!FileSystem::MoveFile(m_outputFilename, ofn))
		{
			m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
				"Could not move file %s to %s: %s", *m_outputFilename, *ofn,
				*FileSystem::GetLastErrorMessage());
		}

		// if destination directory was changed delete the old directory (if empty)
		int len = strlen(nzbDestDir);
		if (!(!strncmp(nzbDestDir, m_outputFilename, len) &&
			(m_outputFilename[len] == PATH_SEPARATOR || m_outputFilename[len] == ALT_PATH_SEPARATOR)))
		{
			debug("Checking old dir for: %s", *m_outputFilename);
			BString<1024> oldDestDir;
			oldDestDir.Set(m_outputFilename, FileSystem::BaseFileName(m_outputFilename) - m_outputFilename);
			if (FileSystem::DirEmpty(oldDestDir))
			{
				debug("Deleting old dir: %s", *oldDestDir);
				FileSystem::RemoveDirectory(oldDestDir);
			}
		}
	}

	if (!directWrite)
	{
		for (ArticleInfo* pa : *m_fileInfo->GetArticles())
		{
			FileSystem::DeleteFile(pa->GetResultFilename());
		}
	}

	if (m_fileInfo->GetMissedArticles() == 0 && m_fileInfo->GetFailedArticles() == 0)
	{
		m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkInfo, "Successfully downloaded %s", *infoFilename);
	}
	else
	{
		m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkWarning,
			"%i of %i article downloads failed for \"%s\"",
			m_fileInfo->GetMissedArticles() + m_fileInfo->GetFailedArticles(),
			m_fileInfo->GetTotalArticles(), *infoFilename);

		if (g_Options->GetBrokenLog())
		{
			BString<1024> brokenLogName("%s%c_brokenlog.txt", *nzbDestDir, (int)PATH_SEPARATOR);
			DiskFile file;
			if (file.Open(brokenLogName, DiskFile::omAppend))
			{
				file.Print("%s (%i/%i)%s", m_fileInfo->GetFilename(), m_fileInfo->GetSuccessArticles(),
					m_fileInfo->GetTotalArticles(), LINE_ENDING);
				file.Close();
			}
		}

		crc = 0;

		if (g_Options->GetSaveQueue() && g_Options->GetServerMode())
		{
			g_DiskState->DiscardFile(m_fileInfo, false, true, false);
			g_DiskState->SaveFileState(m_fileInfo, true);
		}
	}

	CompletedFile::EStatus fileStatus = m_fileInfo->GetMissedArticles() == 0 &&
		m_fileInfo->GetFailedArticles() == 0 ? CompletedFile::cfSuccess :
		m_fileInfo->GetSuccessArticles() > 0 ? CompletedFile::cfPartial :
		CompletedFile::cfFailure;

	// the locking is needed for accessing the members of NZBInfo
	DownloadQueue::Lock();
	m_fileInfo->GetNzbInfo()->GetCompletedFiles()->emplace_back(
		m_fileInfo->GetId(), FileSystem::BaseFileName(ofn), fileStatus, crc);
	if (strcmp(m_fileInfo->GetNzbInfo()->GetDestDir(), nzbDestDir))
	{
		// destination directory was changed during completion, need to move the file
		MoveCompletedFiles(m_fileInfo->GetNzbInfo(), nzbDestDir);
	}
	DownloadQueue::Unlock();
}

void ArticleWriter::FlushCache()
{
	detail("Flushing cache for %s", *m_infoName);

	bool directWrite = g_Options->GetDirectWrite() && m_fileInfo->GetOutputInitialized();
	DiskFile outfile;
	bool needBufFile = false;
	int flushedArticles = 0;
	int64 flushedSize = 0;

	g_ArticleCache->LockFlush();

	FileInfo::Articles cachedArticles;
	cachedArticles.reserve(m_fileInfo->GetArticles()->size());

	g_ArticleCache->LockContent();
	for (ArticleInfo* pa : *m_fileInfo->GetArticles())
	{
		if (pa->GetSegmentContent())
		{
			cachedArticles.push_back(pa);
		}
	}
	g_ArticleCache->UnlockContent();

	for (ArticleInfo* pa : cachedArticles)
	{
		if (m_fileInfo->GetDeleted())
		{
			// the file was deleted during flushing: stop flushing immediately
			break;
		}

		if (directWrite && !outfile.Active())
		{
			if (!outfile.Open(m_fileInfo->GetOutputFilename(), DiskFile::omReadWrite))
			{
				m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
					"Could not open file %s: %s", m_fileInfo->GetOutputFilename(),
					*FileSystem::GetLastErrorMessage());
				break;
			}
			needBufFile = true;
		}

		BString<1024> destFile;

		if (!directWrite)
		{
			destFile.Format("%s.tmp", pa->GetResultFilename());
			if (!outfile.Open(destFile, DiskFile::omWrite))
			{
				m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
					"Could not create file %s: %s", *destFile,
					*FileSystem::GetLastErrorMessage());
				break;
			}
			needBufFile = true;
		}

		if (outfile.Active() && needBufFile)
		{
			SetWriteBuffer(outfile, 0);
			needBufFile = false;
		}

		if (directWrite)
		{
			outfile.Seek(pa->GetSegmentOffset());
		}

		outfile.Write(pa->GetSegmentContent(), pa->GetSegmentSize());

		flushedSize += pa->GetSegmentSize();
		flushedArticles++;

		pa->DiscardSegment();

		if (!directWrite)
		{
			outfile.Close();

			if (!FileSystem::MoveFile(destFile, pa->GetResultFilename()))
			{
				m_fileInfo->GetNzbInfo()->PrintMessage(Message::mkError,
					"Could not rename file %s to %s: %s", *destFile, pa->GetResultFilename(),
					*FileSystem::GetLastErrorMessage());
			}
		}
	}

	outfile.Close();

	g_ArticleCache->LockContent();
	m_fileInfo->SetCachedArticles(m_fileInfo->GetCachedArticles() - flushedArticles);
	g_ArticleCache->UnlockContent();

	g_ArticleCache->UnlockFlush();

	detail("Saved %i articles (%.2f MB) from cache into disk for %s", flushedArticles,
		(float)(flushedSize / 1024.0 / 1024.0), *m_infoName);
}

bool ArticleWriter::MoveCompletedFiles(NzbInfo* nzbInfo, const char* oldDestDir)
{
	if (nzbInfo->GetCompletedFiles()->empty())
	{
		return true;
	}

	// Ensure the DstDir is created
	CString errmsg;
	if (!FileSystem::ForceDirectories(nzbInfo->GetDestDir(), errmsg))
	{
		nzbInfo->PrintMessage(Message::mkError, "Could not create directory %s: %s", nzbInfo->GetDestDir(), *errmsg);
		return false;
	}

	// move already downloaded files to new destination
	for (CompletedFile& completedFile : *nzbInfo->GetCompletedFiles())
	{
		BString<1024> oldFileName("%s%c%s", oldDestDir, (int)PATH_SEPARATOR, completedFile.GetFileName());
		BString<1024> newFileName("%s%c%s", nzbInfo->GetDestDir(), (int)PATH_SEPARATOR, completedFile.GetFileName());

		// check if file was not moved already
		if (strcmp(oldFileName, newFileName))
		{
			// prevent overwriting of existing files
			newFileName = FileSystem::MakeUniqueFilename(nzbInfo->GetDestDir(), completedFile.GetFileName());

			detail("Moving file %s to %s", *oldFileName, *newFileName);
			if (!FileSystem::MoveFile(oldFileName, newFileName))
			{
				nzbInfo->PrintMessage(Message::mkError, "Could not move file %s to %s: %s",
					*oldFileName, *newFileName, *FileSystem::GetLastErrorMessage());
			}
		}
	}

	// move brokenlog.txt
	if (g_Options->GetBrokenLog())
	{
		BString<1024> oldBrokenLogName("%s%c_brokenlog.txt", oldDestDir, (int)PATH_SEPARATOR);
		if (FileSystem::FileExists(oldBrokenLogName))
		{
			BString<1024> brokenLogName("%s%c_brokenlog.txt", nzbInfo->GetDestDir(), (int)PATH_SEPARATOR);

			detail("Moving file %s to %s", *oldBrokenLogName, *brokenLogName);
			if (FileSystem::FileExists(brokenLogName))
			{
				// copy content to existing new file, then delete old file
				DiskFile outfile;
				if (outfile.Open(brokenLogName, DiskFile::omAppend))
				{
					DiskFile infile;
					if (infile.Open(oldBrokenLogName, DiskFile::omRead))
					{
						static const int BUFFER_SIZE = 1024 * 50;
						int cnt = BUFFER_SIZE;
						char* buffer = (char*)malloc(BUFFER_SIZE);
						while (cnt == BUFFER_SIZE)
						{
							cnt = (int)infile.Read(buffer, BUFFER_SIZE);
							outfile.Write(buffer, cnt);
						}
						infile.Close();
						free(buffer);
						FileSystem::DeleteFile(oldBrokenLogName);
					}
					else
					{
						nzbInfo->PrintMessage(Message::mkError, "Could not open file %s", *oldBrokenLogName);
					}
					outfile.Close();
				}
				else
				{
					nzbInfo->PrintMessage(Message::mkError, "Could not open file %s", *brokenLogName);
				}
			}
			else
			{
				// move to new destination
				if (!FileSystem::MoveFile(oldBrokenLogName, brokenLogName))
				{
					nzbInfo->PrintMessage(Message::mkError, "Could not move file %s to %s: %s",
						*oldBrokenLogName, *brokenLogName, *FileSystem::GetLastErrorMessage());
				}
			}
		}
	}

	// delete old directory (if empty)
	if (FileSystem::DirEmpty(oldDestDir))
	{
		// check if there are pending writes into directory
		bool pendingWrites = false;
		for (FileInfo* fileInfo : *nzbInfo->GetFileList())
		{
			if (!pendingWrites)
			{
				break;
			}

			if (fileInfo->GetActiveDownloads() > 0)
			{
				fileInfo->LockOutputFile();
				pendingWrites = fileInfo->GetOutputInitialized() && !Util::EmptyStr(fileInfo->GetOutputFilename());
				fileInfo->UnlockOutputFile();
			}
			else
			{
				pendingWrites = fileInfo->GetOutputInitialized() && !Util::EmptyStr(fileInfo->GetOutputFilename());
			}
		}

		if (!pendingWrites)
		{
			FileSystem::RemoveDirectory(oldDestDir);
		}
	}

	return true;
}


ArticleCache::ArticleCache()
{
	m_allocated = 0;
	m_flushing = false;
	m_fileInfo = nullptr;
}

void* ArticleCache::Alloc(int size)
{
	m_allocMutex.Lock();

	void* p = nullptr;
	if (m_allocated + size <= (size_t)g_Options->GetArticleCache() * 1024 * 1024)
	{
		p = malloc(size);
		if (p)
		{
			if (!m_allocated && g_Options->GetSaveQueue() && g_Options->GetServerMode() && g_Options->GetContinuePartial())
			{
				g_DiskState->WriteCacheFlag();
			}
			m_allocated += size;
		}
	}
	m_allocMutex.Unlock();

	return p;
}

void* ArticleCache::Realloc(void* buf, int oldSize, int newSize)
{
	m_allocMutex.Lock();

	void* p = realloc(buf, newSize);
	if (p)
	{
		m_allocated += newSize - oldSize;
	}
	else
	{
		p = buf;
	}
	m_allocMutex.Unlock();

	return p;
}

void ArticleCache::Free(int size)
{
	m_allocMutex.Lock();
	m_allocated -= size;
	if (!m_allocated && g_Options->GetSaveQueue() && g_Options->GetServerMode() && g_Options->GetContinuePartial())
	{
		g_DiskState->DeleteCacheFlag();
	}
	m_allocMutex.Unlock();
}

void ArticleCache::LockFlush()
{
	m_flushMutex.Lock();
	m_flushing = true;
}

void ArticleCache::UnlockFlush()
{
	m_flushMutex.Unlock();
	m_flushing = false;
}

void ArticleCache::Run()
{
	// automatically flush the cache if it is filled to 90% (only in DirectWrite mode)
	size_t fillThreshold = (size_t)g_Options->GetArticleCache() * 1024 * 1024 / 100 * 90;

	int resetCounter = 0;
	bool justFlushed = false;
	while (!IsStopped() || m_allocated > 0)
	{
		if ((justFlushed || resetCounter >= 1000  || IsStopped() ||
			 (g_Options->GetDirectWrite() && m_allocated >= fillThreshold)) &&
			m_allocated > 0)
		{
			justFlushed = CheckFlush(m_allocated >= fillThreshold);
			resetCounter = 0;
		}
		else
		{
			usleep(5 * 1000);
			resetCounter += 5;
		}
	}
}

bool ArticleCache::CheckFlush(bool flushEverything)
{
	debug("Checking cache, Allocated: %i, FlushEverything: %i", (int)m_allocated, (int)flushEverything);

	BString<1024> infoName;

	DownloadQueue* downloadQueue = DownloadQueue::Lock();
	for (NzbInfo* nzbInfo : *downloadQueue->GetQueue())
	{
		if (m_fileInfo)
		{
			break;
		}

		for (FileInfo* fileInfo : *nzbInfo->GetFileList())
		{
			if (fileInfo->GetCachedArticles() > 0 && (fileInfo->GetActiveDownloads() == 0 || flushEverything))
			{
				m_fileInfo = fileInfo;
				infoName.Format("%s%c%s", m_fileInfo->GetNzbInfo()->GetName(), (int)PATH_SEPARATOR, m_fileInfo->GetFilename());
				break;
			}
		}
	}
	DownloadQueue::Unlock();

	if (m_fileInfo)
	{
		ArticleWriter* articleWriter = new ArticleWriter();
		articleWriter->SetFileInfo(m_fileInfo);
		articleWriter->SetInfoName(infoName);
		articleWriter->FlushCache();
		delete articleWriter;
		m_fileInfo = nullptr;
		return true;
	}

	debug("Checking cache... nothing to flush");

	return false;
}
