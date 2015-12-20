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
 * $Revision$
 * $Date$
 *
 */


#include "nzbget.h"
#include "Unpack.h"
#include "Log.h"
#include "Util.h"
#include "ParParser.h"
#include "Options.h"

bool UnpackController::FileList::Exists(const char* filename)
{
	for (iterator it = begin(); it != end(); it++)
	{
		const char* filename1 = *it;
		if (!strcmp(filename1, filename))
		{
			return true;
		}
	}

	return false;
}

UnpackController::ParamList::~ParamList()
{
	for (iterator it = begin(); it != end(); it++)
	{
		free(*it);
	}
}

bool UnpackController::ParamList::Exists(const char* param)
{
	for (iterator it = begin(); it != end(); it++)
	{
		char* param1 = *it;
		if (!strcmp(param1, param))
		{
			return true;
		}
	}

	return false;
}

void UnpackController::StartJob(PostInfo* postInfo)
{
	UnpackController* unpackController = new UnpackController();
	unpackController->m_postInfo = postInfo;
	unpackController->SetAutoDestroy(false);

	postInfo->SetPostThread(unpackController);

	unpackController->Start();
}

void UnpackController::Run()
{
	time_t start = time(NULL);

	// the locking is needed for accessing the members of NZBInfo
	DownloadQueue::Lock();

	m_destDir = m_postInfo->GetNzbInfo()->GetDestDir();
	m_name = m_postInfo->GetNzbInfo()->GetName();
	m_cleanedUpDisk = false;
	m_finalDirCreated = false;
	m_unpackOk = true;
	m_unpackStartError = false;
	m_unpackSpaceError = false;
	m_unpackDecryptError = false;
	m_unpackPasswordError = false;
	m_autoTerminated = false;
	m_passListTried = false;

	NzbParameter* parameter = m_postInfo->GetNzbInfo()->GetParameters()->Find("*Unpack:", false);
	bool unpack = !(parameter && !strcasecmp(parameter->GetValue(), "no"));

	parameter = m_postInfo->GetNzbInfo()->GetParameters()->Find("*Unpack:Password", false);
	if (parameter)
	{
		m_password = parameter->GetValue();
	}

	DownloadQueue::Unlock();

	m_infoName.Format("unpack for %s", *m_name);
	m_infoNameUp.Format("Unpack for %s", *m_name); // first letter in upper case

	m_hasParFiles = ParParser::FindMainPars(m_destDir, NULL);

	if (unpack)
	{
		bool scanNonStdFiles = m_postInfo->GetNzbInfo()->GetRenameStatus() > NzbInfo::rsSkipped ||
			m_postInfo->GetNzbInfo()->GetParStatus() == NzbInfo::psSuccess ||
			!m_hasParFiles;
		CheckArchiveFiles(scanNonStdFiles);
	}

	SetInfoName(m_infoName);
	SetWorkingDir(m_destDir);

	bool hasFiles = m_hasRarFiles || m_hasNonStdRarFiles || m_hasSevenZipFiles || m_hasSevenZipMultiFiles || m_hasSplittedFiles;

	if (m_postInfo->GetUnpackTried() && !m_postInfo->GetParRepaired() &&
		(!m_password.Empty() || Util::EmptyStr(g_Options->GetUnpackPassFile()) || m_postInfo->GetPassListTried()))
	{
		PrintMessage(Message::mkInfo, "Second unpack attempt skipped for %s due to par-check not repaired anything", *m_name);
		PrintMessage(Message::mkError,
			m_postInfo->GetLastUnpackStatus() == (int)NzbInfo::usPassword ?
				 "%s failed: checksum error in the encrypted file. Corrupt file or wrong password." : "%s failed.",
			*m_infoNameUp);
		m_postInfo->GetNzbInfo()->SetUnpackStatus((NzbInfo::EUnpackStatus)m_postInfo->GetLastUnpackStatus());
		m_postInfo->SetStage(PostInfo::ptQueued);
	}
	else if (unpack && hasFiles)
	{
		PrintMessage(Message::mkInfo, "Unpacking %s", *m_name);

		CreateUnpackDir();

		if (m_hasRarFiles || m_hasNonStdRarFiles)
		{
			UnpackArchives(upUnrar, false);
		}

		if (m_hasSevenZipFiles && m_unpackOk)
		{
			UnpackArchives(upSevenZip, false);
		}

		if (m_hasSevenZipMultiFiles && m_unpackOk)
		{
			UnpackArchives(upSevenZip, true);
		}

		if (m_hasSplittedFiles && m_unpackOk)
		{
			JoinSplittedFiles();
		}

		Completed();

		m_joinedFiles.clear();
	}
	else
	{
		PrintMessage(Message::mkInfo, (unpack ? "Nothing to unpack for %s" : "Unpack for %s skipped"), *m_name);

#ifndef DISABLE_PARCHECK
		if (unpack && m_postInfo->GetNzbInfo()->GetParStatus() <= NzbInfo::psSkipped &&
			m_postInfo->GetNzbInfo()->GetRenameStatus() <= NzbInfo::rsSkipped && m_hasParFiles)
		{
			RequestParCheck(false);
		}
		else
#endif
		{
			m_postInfo->GetNzbInfo()->SetUnpackStatus(NzbInfo::usSkipped);
			m_postInfo->SetStage(PostInfo::ptQueued);
		}
	}

	int unpackSec = (int)(time(NULL) - start);
	m_postInfo->GetNzbInfo()->SetUnpackSec(m_postInfo->GetNzbInfo()->GetUnpackSec() + unpackSec);

	m_postInfo->SetWorking(false);
}

void UnpackController::UnpackArchives(EUnpacker unpacker, bool multiVolumes)
{
	if (!m_postInfo->GetUnpackTried() || m_postInfo->GetParRepaired())
	{
		ExecuteUnpack(unpacker, m_password, multiVolumes);
		if (!m_unpackOk && m_hasParFiles && !m_unpackPasswordError &&
			m_postInfo->GetNzbInfo()->GetParStatus() <= NzbInfo::psSkipped)
		{
			// for rar4- or 7z-archives try par-check first, before trying password file
			return;
		}
	}
	else
	{
		m_unpackOk = false;
		m_unpackDecryptError = m_postInfo->GetLastUnpackStatus() == (int)NzbInfo::usPassword;
	}

	if (!m_unpackOk && !m_unpackStartError && !m_unpackSpaceError &&
		(m_unpackDecryptError || m_unpackPasswordError) &&
		(!GetTerminated() || m_autoTerminated) &&
		m_password.Empty() && !Util::EmptyStr(g_Options->GetUnpackPassFile()))
	{
		FILE* infile = fopen(g_Options->GetUnpackPassFile(), FOPEN_RB);
		if (!infile)
		{
			PrintMessage(Message::mkError, "Could not open file %s", g_Options->GetUnpackPassFile());
			return;
		}

		char password[512];
		while (!m_unpackOk && !m_unpackStartError && !m_unpackSpaceError &&
			(m_unpackDecryptError || m_unpackPasswordError) &&
			fgets(password, sizeof(password) - 1, infile))
		{
			// trim trailing <CR> and <LF>
			char* end = password + strlen(password) - 1;
			while (end >= password && (*end == '\n' || *end == '\r')) *end-- = '\0';

			if (!Util::EmptyStr(password))
			{
				if (IsStopped() && m_autoTerminated)
				{
					ScriptController::Resume();
					Thread::Resume();
				}
				m_unpackDecryptError = false;
				m_unpackPasswordError = false;
				m_autoTerminated = false;
				PrintMessage(Message::mkInfo, "Trying password %s for %s", password, *m_name);
				ExecuteUnpack(unpacker, password, multiVolumes);
			}
		}

		fclose(infile);
		m_passListTried = !IsStopped() || m_autoTerminated;
	}
}

void UnpackController::ExecuteUnpack(EUnpacker unpacker, const char* password, bool multiVolumes)
{
	switch (unpacker)
	{
		case upUnrar:
			ExecuteUnrar(password);
			break;

		case upSevenZip:
			ExecuteSevenZip(password, multiVolumes);
			break;
	}
}

void UnpackController::ExecuteUnrar(const char* password)
{
	// Format:
	//   unrar x -y -p- -o+ *.rar ./_unpack/

	ParamList params;
	if (!PrepareCmdParams(g_Options->GetUnrarCmd(), &params, "unrar"))
	{
		return;
	}

	if (!params.Exists("x") && !params.Exists("e"))
	{
		params.push_back(strdup("x"));
	}

	params.push_back(strdup("-y"));

	if (!Util::EmptyStr(password))
	{
		params.push_back(strdup(BString<1024>("-p%s", password)));
	}
	else
	{
		params.push_back(strdup("-p-"));
	}

	if (!params.Exists("-o+") && !params.Exists("-o-"))
	{
		params.push_back(strdup("-o+"));
	}

	params.push_back(strdup(m_hasNonStdRarFiles ? "*.*" : "*.rar"));
	params.push_back(strdup(BString<1024>("%s%c", *m_unpackDir, PATH_SEPARATOR)));
	params.push_back(NULL);
	SetArgs((const char**)&params.front(), false);
	SetScript(params.at(0));
	SetLogPrefix("Unrar");
	ResetEnv();

	m_allOkMessageReceived = false;
	m_unpacker = upUnrar;

	SetProgressLabel("");
	int exitCode = Execute();
	SetLogPrefix(NULL);
	SetProgressLabel("");

	m_unpackOk = exitCode == 0 && m_allOkMessageReceived && !GetTerminated();
	m_unpackStartError = exitCode == -1;
	m_unpackSpaceError = exitCode == 5;
	m_unpackPasswordError |= exitCode == 11; // only for rar5-archives

	if (!m_unpackOk && exitCode > 0)
	{
		PrintMessage(Message::mkError, "Unrar error code: %i", exitCode);
	}
}

void UnpackController::ExecuteSevenZip(const char* password, bool multiVolumes)
{
	// Format:
	//   7z x -y -p- -o./_unpack *.7z
	// OR
	//   7z x -y -p- -o./_unpack *.7z.001

	ParamList params;
	if (!PrepareCmdParams(g_Options->GetSevenZipCmd(), &params, "7-Zip"))
	{
		return;
	}

	if (!params.Exists("x") && !params.Exists("e"))
	{
		params.push_back(strdup("x"));
	}

	params.push_back(strdup("-y"));

	if (!Util::EmptyStr(password))
	{
		params.push_back(strdup(BString<1024>("-p%s", password)));
	}
	else
	{
		params.push_back(strdup("-p-"));
	}

	params.push_back(strdup(BString<1024>("-o%s", *m_unpackDir)));
	params.push_back(strdup(multiVolumes ? "*.7z.001" : "*.7z"));
	params.push_back(NULL);
	SetArgs((const char**)&params.front(), false);
	SetScript(params.at(0));
	ResetEnv();

	m_allOkMessageReceived = false;
	m_unpacker = upSevenZip;

	PrintMessage(Message::mkInfo, "Executing 7-Zip");
	SetLogPrefix("7-Zip");
	SetProgressLabel("");
	int exitCode = Execute();
	SetLogPrefix(NULL);
	SetProgressLabel("");

	m_unpackOk = exitCode == 0 && m_allOkMessageReceived && !GetTerminated();
	m_unpackStartError = exitCode == -1;

	if (!m_unpackOk && exitCode > 0)
	{
		PrintMessage(Message::mkError, "7-Zip error code: %i", exitCode);
	}
}

bool UnpackController::PrepareCmdParams(const char* command, ParamList* params, const char* infoName)
{
	if (Util::FileExists(command))
	{
		params->push_back(strdup(command));
		return true;
	}

	char** cmdArgs = NULL;
	if (!Util::SplitCommandLine(command, &cmdArgs))
	{
		PrintMessage(Message::mkError, "Could not start %s, failed to parse command line: %s", infoName, command);
		m_unpackOk = false;
		m_unpackStartError = true;
		return false;
	}

	for (char** argPtr = cmdArgs; *argPtr; argPtr++)
	{
		params->push_back(*argPtr);
	}
	free(cmdArgs);

	return true;
}

void UnpackController::JoinSplittedFiles()
{
	SetLogPrefix("Join");
	SetProgressLabel("");
	m_postInfo->SetStageProgress(0);

	// determine groups

	FileList groups;
	RegEx regExSplitExt(".*\\.[a-z,0-9]{3}\\.001$");

	DirBrowser dir(m_destDir);
	while (const char* filename = dir.Next())
	{
		BString<1024> fullFilename("%s%c%s", *m_destDir, PATH_SEPARATOR, filename);

		if (strcmp(filename, ".") && strcmp(filename, "..") && !Util::DirectoryExists(fullFilename))
		{
			if (regExSplitExt.Match(filename) && !FileHasRarSignature(fullFilename))
			{
				if (!JoinFile(filename))
				{
					m_unpackOk = false;
					break;
				}
			}
		}
	}

	SetLogPrefix(NULL);
	SetProgressLabel("");
}

bool UnpackController::JoinFile(const char* fragBaseName)
{
	BString<1024> destBaseName;
	destBaseName.Set(fragBaseName);

	// trim extension
	char* extension = strrchr(destBaseName, '.');
	*extension = '\0';

	BString<1024> fullFilename("%s%c%s", *m_destDir, PATH_SEPARATOR, fragBaseName);
	int64 firstSegmentSize = Util::FileSize(fullFilename);
	int64 difSegmentSize = 0;

	// Validate joinable file:
	//  - fragments have continuous numbers (no holes);
	//  - fragments have the same size (except of the last fragment);
	//  - the last fragment must be smaller than other fragments,
	//  if it has the same size it is probably not the last and there are missing fragments.

	RegEx regExSplitExt(".*\\.[a-z,0-9]{3}\\.[0-9]{3}$");
	int count = 0;
	int min = -1;
	int max = -1;
	int difSizeCount = 0;
	int difSizeMin = 999999;
	DirBrowser dir(m_destDir);
	while (const char* filename = dir.Next())
	{
		fullFilename.Format("%s%c%s", *m_destDir, PATH_SEPARATOR, filename);

		if (strcmp(filename, ".") && strcmp(filename, "..") && !Util::DirectoryExists(fullFilename) &&
			regExSplitExt.Match(filename))
		{
			const char* segExt = strrchr(filename, '.');
			int segNum = atoi(segExt + 1);
			count++;
			min = segNum < min || min == -1 ? segNum : min;
			max = segNum > max ? segNum : max;

			int64 segmentSize = Util::FileSize(fullFilename);
			if (segmentSize != firstSegmentSize)
			{
				difSizeCount++;
				difSizeMin = segNum < difSizeMin ? segNum : difSizeMin;
				difSegmentSize = segmentSize;
			}
		}
	}

	int correctedCount = count - (min == 0 ? 1 : 0);
	if ((min > 1) || correctedCount != max ||
		((difSizeMin != correctedCount || difSizeMin > max) &&
		 m_postInfo->GetNzbInfo()->GetParStatus() != NzbInfo::psSuccess))
	{
		PrintMessage(Message::mkWarning, "Could not join splitted file %s: missing fragments detected", *destBaseName);
		return false;
	}

	// Now can join
	PrintMessage(Message::mkInfo, "Joining splitted file %s", *destBaseName);
	m_postInfo->SetStageProgress(0);

	BString<1024> destFilename("%s%c%s", *m_unpackDir, PATH_SEPARATOR, *destBaseName);

	FILE* outFile = fopen(destFilename, FOPEN_WBP);
	if (!outFile)
	{
		PrintMessage(Message::mkError, "Could not create file %s: %s", *destFilename,
			*Util::GetLastErrorMessage());
		return false;
	}
	if (g_Options->GetWriteBuffer() > 0)
	{
		setvbuf(outFile, NULL, _IOFBF, g_Options->GetWriteBuffer() * 1024);
	}

	int64 totalSize = firstSegmentSize * (count - 1) + difSegmentSize;
	int64 written = 0;

	static const int BUFFER_SIZE = 1024 * 50;
	char* buffer = (char*)malloc(BUFFER_SIZE);

	bool ok = true;
	for (int i = min; i <= max; i++)
	{
		PrintMessage(Message::mkInfo, "Joining from %s.%.3i", *destBaseName, i);
		SetProgressLabel(BString<1024>("Joining from %s.%.3i", *destBaseName, i));

		BString<1024> fragFilename("%s%c%s.%.3i", *m_destDir, PATH_SEPARATOR, *destBaseName, i);
		if (!Util::FileExists(fragFilename))
		{
			break;
		}

		FILE* inFile = fopen(fragFilename, FOPEN_RB);
		if (inFile)
		{
			int cnt = BUFFER_SIZE;
			while (cnt == BUFFER_SIZE)
			{
				cnt = (int)fread(buffer, 1, BUFFER_SIZE, inFile);
				fwrite(buffer, 1, cnt, outFile);
				written += cnt;
				m_postInfo->SetStageProgress(int(written * 1000 / totalSize));
			}
			fclose(inFile);

			CString fragFilename;
			fragFilename.Format("%s.%.3i", *destBaseName, i);
			m_joinedFiles.push_back(std::move(fragFilename));
		}
		else
		{
			PrintMessage(Message::mkError, "Could not open file %s", *fragFilename);
			ok = false;
			break;
		}
	}

	fclose(outFile);
	free(buffer);

	return ok;
}

void UnpackController::Completed()
{
	bool cleanupSuccess = Cleanup();

	if (m_unpackOk && cleanupSuccess)
	{
		PrintMessage(Message::mkInfo, "%s %s", *m_infoNameUp, "successful");
		m_postInfo->GetNzbInfo()->SetUnpackStatus(NzbInfo::usSuccess);
		m_postInfo->GetNzbInfo()->SetUnpackCleanedUpDisk(m_cleanedUpDisk);
		if (g_Options->GetParRename())
		{
			//request par-rename check for extracted files
			m_postInfo->GetNzbInfo()->SetRenameStatus(NzbInfo::rsNone);
		}
		m_postInfo->SetStage(PostInfo::ptQueued);
	}
	else
	{
#ifndef DISABLE_PARCHECK
		if (!m_unpackOk &&
			(m_postInfo->GetNzbInfo()->GetParStatus() <= NzbInfo::psSkipped ||
			 !m_postInfo->GetNzbInfo()->GetParFull()) &&
			!m_unpackStartError && !m_unpackSpaceError && !m_unpackPasswordError &&
			(!GetTerminated() || m_autoTerminated) && m_hasParFiles)
		{
			RequestParCheck(!m_password.Empty() ||
				Util::EmptyStr(g_Options->GetUnpackPassFile()) || m_passListTried ||
				!(m_unpackDecryptError || m_unpackPasswordError) ||
				m_postInfo->GetNzbInfo()->GetParStatus() > NzbInfo::psSkipped);
		}
		else
#endif
		{
			PrintMessage(Message::mkError, "%s failed", *m_infoNameUp);
			m_postInfo->GetNzbInfo()->SetUnpackStatus(
				m_unpackSpaceError ? NzbInfo::usSpace :
				m_unpackPasswordError || m_unpackDecryptError ? NzbInfo::usPassword :
				NzbInfo::usFailure);
			m_postInfo->SetStage(PostInfo::ptQueued);
		}
	}
}

#ifndef DISABLE_PARCHECK
void UnpackController::RequestParCheck(bool forceRepair)
{
	PrintMessage(Message::mkInfo, "%s requested %s", *m_infoNameUp, forceRepair ? "par-check with forced repair" : "par-check/repair");
	m_postInfo->SetRequestParCheck(true);
	m_postInfo->SetForceRepair(forceRepair);
	m_postInfo->SetStage(PostInfo::ptFinished);
	m_postInfo->SetUnpackTried(true);
	m_postInfo->SetPassListTried(m_passListTried);
	m_postInfo->SetLastUnpackStatus((int)(m_unpackSpaceError ? NzbInfo::usSpace :
		m_unpackPasswordError || m_unpackDecryptError ? NzbInfo::usPassword :
		NzbInfo::usFailure));
}
#endif

void UnpackController::CreateUnpackDir()
{
	m_interDir = strlen(g_Options->GetInterDir()) > 0 &&
		!strncmp(m_destDir, g_Options->GetInterDir(), strlen(g_Options->GetInterDir()));
	if (m_interDir)
	{
		m_finalDir = m_postInfo->GetNzbInfo()->BuildFinalDirName();
		m_unpackDir.Format("%s%c%s", *m_finalDir, PATH_SEPARATOR, "_unpack");
		m_finalDirCreated = !Util::DirectoryExists(m_finalDir);
	}
	else
	{
		m_unpackDir.Format("%s%c%s", *m_destDir, PATH_SEPARATOR, "_unpack");
	}

	CString errmsg;
	if (!Util::ForceDirectories(m_unpackDir, errmsg))
	{
		PrintMessage(Message::mkError, "Could not create directory %s: %s", *m_unpackDir, *errmsg);
	}
}


void UnpackController::CheckArchiveFiles(bool scanNonStdFiles)
{
	m_hasRarFiles = false;
	m_hasNonStdRarFiles = false;
	m_hasSevenZipFiles = false;
	m_hasSevenZipMultiFiles = false;
	m_hasSplittedFiles = false;

	RegEx regExRar(".*\\.rar$");
	RegEx regExRarMultiSeq(".*\\.(r|s)[0-9][0-9]$");
	RegEx regExSevenZip(".*\\.7z$");
	RegEx regExSevenZipMulti(".*\\.7z\\.[0-9]+$");
	RegEx regExNumExt(".*\\.[0-9]+$");
	RegEx regExSplitExt(".*\\.[a-z,0-9]{3}\\.[0-9]{3}$");

	DirBrowser dir(m_destDir);
	while (const char* filename = dir.Next())
	{
		BString<1024> fullFilename("%s%c%s", *m_destDir, PATH_SEPARATOR, filename);

		if (strcmp(filename, ".") && strcmp(filename, "..") && !Util::DirectoryExists(fullFilename))
		{
			const char* ext = strchr(filename, '.');
			int extNum = ext ? atoi(ext + 1) : -1;

			if (regExRar.Match(filename))
			{
				m_hasRarFiles = true;
			}
			else if (regExSevenZip.Match(filename))
			{
				m_hasSevenZipFiles = true;
			}
			else if (regExSevenZipMulti.Match(filename))
			{
				m_hasSevenZipMultiFiles = true;
			}
			else if (scanNonStdFiles && !m_hasNonStdRarFiles && extNum > 1 &&
				!regExRarMultiSeq.Match(filename) && regExNumExt.Match(filename) &&
				FileHasRarSignature(fullFilename))
			{
				m_hasNonStdRarFiles = true;
			}
			else if (regExSplitExt.Match(filename) && (extNum == 0 || extNum == 1))
			{
				m_hasSplittedFiles = true;
			}
		}
	}
}

bool UnpackController::FileHasRarSignature(const char* filename)
{
	char rar4Signature[] = { 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00 };
	char rar5Signature[] = { 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00 };

	char fileSignature[8];

	int cnt = 0;
	FILE* infile;
	infile = fopen(filename, FOPEN_RB);
	if (infile)
	{
		cnt = (int)fread(fileSignature, 1, sizeof(fileSignature), infile);
		fclose(infile);
	}

	bool rar = cnt == sizeof(fileSignature) &&
		(!strcmp(rar4Signature, fileSignature) || !strcmp(rar5Signature, fileSignature));
	return rar;
}

bool UnpackController::Cleanup()
{
	// By success:
	//   - move unpacked files to destination dir;
	//   - remove _unpack-dir;
	//   - delete archive-files.
	// By failure:
	//   - remove _unpack-dir.

	bool ok = true;

	FileList extractedFiles;

	if (m_unpackOk)
	{
		// moving files back
		DirBrowser dir(m_unpackDir);
		while (const char* filename = dir.Next())
		{
			if (strcmp(filename, ".") && strcmp(filename, ".."))
			{
				BString<1024> srcFile("%s%c%s", *m_unpackDir, PATH_SEPARATOR, filename);
				BString<1024> dstFile("%s%c%s", !m_finalDir.Empty() ? *m_finalDir : *m_destDir, PATH_SEPARATOR, filename);

				// silently overwrite existing files
				remove(dstFile);

				bool hiddenFile = filename[0] == '.';

				if (!Util::MoveFile(srcFile, dstFile) && !hiddenFile)
				{
					PrintMessage(Message::mkError, "Could not move file %s to %s: %s", *srcFile, *dstFile,
						*Util::GetLastErrorMessage());
					ok = false;
				}

				extractedFiles.push_back(filename);
			}
		}
	}

	CString errmsg;
	if (ok && !Util::DeleteDirectoryWithContent(m_unpackDir, errmsg))
	{
		PrintMessage(Message::mkError, "Could not delete temporary directory %s: %s", *m_unpackDir, *errmsg);
	}

	if (!m_unpackOk && m_finalDirCreated)
	{
		Util::RemoveDirectory(m_finalDir);
	}

	if (m_unpackOk && ok && g_Options->GetUnpackCleanupDisk())
	{
		PrintMessage(Message::mkInfo, "Deleting archive files");

		RegEx regExRar(".*\\.rar$");
		RegEx regExRarMultiSeq(".*\\.[r-z][0-9][0-9]$");
		RegEx regExSevenZip(".*\\.7z$|.*\\.7z\\.[0-9]+$");
		RegEx regExNumExt(".*\\.[0-9]+$");
		RegEx regExSplitExt(".*\\.[a-z,0-9]{3}\\.[0-9]{3}$");

		DirBrowser dir(m_destDir);
		while (const char* filename = dir.Next())
		{
			BString<1024> fullFilename("%s%c%s", *m_destDir, PATH_SEPARATOR, filename);

			if (strcmp(filename, ".") && strcmp(filename, "..") &&
				!Util::DirectoryExists(fullFilename) &&
				(m_interDir || !extractedFiles.Exists(filename)) &&
				(regExRar.Match(filename) || regExSevenZip.Match(filename) ||
				 (regExRarMultiSeq.Match(filename) && FileHasRarSignature(fullFilename)) ||
				 (m_hasNonStdRarFiles && regExNumExt.Match(filename) && FileHasRarSignature(fullFilename)) ||
				 (m_hasSplittedFiles && regExSplitExt.Match(filename) && m_joinedFiles.Exists(filename))))
			{
				PrintMessage(Message::mkInfo, "Deleting file %s", filename);

				if (remove(fullFilename) != 0)
				{
					PrintMessage(Message::mkError, "Could not delete file %s: %s", *fullFilename,
						*Util::GetLastErrorMessage());
				}
			}
		}

		m_cleanedUpDisk = true;
	}

	return ok;
}

/**
 * Unrar prints progress information into the same line using backspace control character.
 * In order to print progress continuously we analyze the output after every char
 * and update post-job progress information.
 */
bool UnpackController::ReadLine(char* buf, int bufSize, FILE* stream)
{
	bool printed = false;

	int i = 0;

	for (; i < bufSize - 1; i++)
	{
		int ch = fgetc(stream);
		buf[i] = ch;
		buf[i+1] = '\0';
		if (ch == EOF)
		{
			break;
		}
		if (ch == '\n')
		{
			i++;
			break;
		}

		char* backspace = strrchr(buf, '\b');
		if (backspace)
		{
			if (!printed)
			{
				BString<1024> tmp;
				tmp.Set(buf);
				char* tmpPercent = strrchr(tmp, '\b');
				if (tmpPercent)
				{
					*tmpPercent = '\0';
				}
				if (strncmp(buf, "...", 3))
				{
					ProcessOutput(tmp);
				}
				printed = true;
			}
			if (strchr(backspace, '%'))
			{
				int percent = atoi(backspace + 1);
				m_postInfo->SetStageProgress(percent * 10);
			}
		}
	}

	buf[i] = '\0';

	if (printed)
	{
		buf[0] = '\0';
	}

	return i > 0;
}

void UnpackController::AddMessage(Message::EKind kind, const char* text)
{
	BString<1024> msgText;
	msgText.Set(text);
	int len = strlen(text);

	// Modify unrar messages for better readability:
	// remove the destination path part from message "Extracting file.xxx"
	if (m_unpacker == upUnrar && !strncmp(text, "Unrar: Extracting  ", 19) &&
		!strncmp(text + 19, m_unpackDir, strlen(m_unpackDir)))
	{
		msgText.Format("Unrar: Extracting %s", text + 19 + strlen(m_unpackDir) + 1);
	}

	m_postInfo->GetNzbInfo()->AddMessage(kind, msgText);

	if (m_unpacker == upUnrar && !strncmp(msgText, "Unrar: UNRAR ", 6) &&
		strstr(msgText, " Copyright ") && strstr(msgText, " Alexander Roshal"))
	{
		// reset start time for a case if user uses unpack-script to do some things
		// (like sending Wake-On-Lan message) before executing unrar
		m_postInfo->SetStageTime(time(NULL));
	}

	if (m_unpacker == upUnrar && !strncmp(msgText, "Unrar: Extracting ", 18))
	{
		SetProgressLabel(msgText + 7);
	}

	if (m_unpacker == upUnrar && !strncmp(text, "Unrar: Extracting from ", 23))
	{
		const char *filename = text + 23;
		debug("Filename: %s", filename);
		SetProgressLabel(text + 7);
	}

	if (m_unpacker == upUnrar &&
		(!strncmp(text, "Unrar: Checksum error in the encrypted file", 42) ||
		 !strncmp(text, "Unrar: CRC failed in the encrypted file", 39)))
	{
		m_unpackDecryptError = true;
	}

	if (m_unpacker == upUnrar && !strncmp(text, "Unrar: The specified password is incorrect.'", 43))
	{
		m_unpackPasswordError = true;
	}

	if (m_unpacker == upSevenZip &&
		(len > 18 && !strncmp(text + len - 45, "Data Error in encrypted file. Wrong password?", 45)))
	{
		m_unpackDecryptError = true;
	}

	if (!IsStopped() && (m_unpackDecryptError || m_unpackPasswordError ||
		strstr(text, " : packed data CRC failed in volume") ||
		strstr(text, " : packed data checksum error in volume") ||
		(len > 13 && !strncmp(text + len - 13, " - CRC failed", 13)) ||
		(len > 18 && !strncmp(text + len - 18, " - checksum failed", 18)) ||
		!strncmp(text, "Unrar: WARNING: You need to start extraction from a previous volume", 67)))
	{
		m_postInfo->GetNzbInfo()->AddMessage(Message::mkWarning,
			BString<1024>("Cancelling %s due to errors", *m_infoName));
		m_autoTerminated = true;
		Stop();
	}

	if ((m_unpacker == upUnrar && !strncmp(text, "Unrar: All OK", 13)) ||
		(m_unpacker == upSevenZip && !strncmp(text, "7-Zip: Everything is Ok", 23)))
	{
		m_allOkMessageReceived = true;
	}
}

void UnpackController::Stop()
{
	debug("Stopping unpack");
	Thread::Stop();
	Terminate();
}

void UnpackController::SetProgressLabel(const char* progressLabel)
{
	DownloadQueue::Lock();
	m_postInfo->SetProgressLabel(progressLabel);
	DownloadQueue::Unlock();
}
