/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2005 Bo Cordes Petersen <placebodk@sourceforge.net>
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
#include "RemoteClient.h"
#include "DownloadInfo.h"
#include "Options.h"
#include "Log.h"
#include "Util.h"

RemoteClient::RemoteClient()
{
	m_connection	= NULL;
	m_verbose		= true;

	/*
	printf("sizeof(SNZBRequestBase)=%i\n", sizeof(SNZBRequestBase));
	printf("sizeof(SNZBDownloadRequest)=%i\n", sizeof(SNZBDownloadRequest));
	printf("sizeof(SNZBListRequest)=%i\n", sizeof(SNZBListRequest));
	printf("sizeof(SNZBListResponse)=%i\n", sizeof(SNZBListResponse));
	printf("sizeof(SNZBListResponseFileEntry)=%i\n", sizeof(SNZBListResponseFileEntry));
	printf("sizeof(SNZBLogRequest)=%i\n", sizeof(SNZBLogRequest));
	printf("sizeof(SNZBLogResponse)=%i\n", sizeof(SNZBLogResponse));
	printf("sizeof(SNZBLogResponseEntry)=%i\n", sizeof(SNZBLogResponseEntry));
	printf("sizeof(SNZBPauseUnpauseRequest)=%i\n", sizeof(SNZBPauseUnpauseRequest));
	printf("sizeof(SNZBSetDownloadRateRequest)=%i\n", sizeof(SNZBSetDownloadRateRequest));
	printf("sizeof(SNZBEditQueueRequest)=%i\n", sizeof(SNZBEditQueueRequest));
	printf("sizeof(SNZBDumpDebugRequest)=%i\n", sizeof(SNZBDumpDebugRequest));
	*/
}

RemoteClient::~RemoteClient()
{
	delete m_connection;
}

void RemoteClient::printf(const char * msg,...)
{
	if (m_verbose)
	{
		va_list ap;
		va_start(ap, msg);
		::vprintf(msg, ap);
		va_end(ap);
	}
}

void RemoteClient::perror(const char * msg)
{
	if (m_verbose)
	{
		::perror(msg);
	}
}

bool RemoteClient::InitConnection()
{
	const char* controlIp = !strcmp(g_Options->GetControlIp(), "0.0.0.0") ? "127.0.0.1" : g_Options->GetControlIp();

	// Create a connection to the server
	m_connection = new Connection(controlIp, g_Options->GetControlPort(), false);

	bool OK = m_connection->Connect();
	if (!OK)
	{
		printf("Unable to send request to nzbget-server at %s (port %i)\n", controlIp, g_Options->GetControlPort());
	}
	return OK;
}

void RemoteClient::InitMessageBase(SNzbRequestBase* messageBase, int request, int size)
{
	messageBase->m_signature	= htonl(NZBMESSAGE_SIGNATURE);
	messageBase->m_type = htonl(request);
	messageBase->m_structSize = htonl(size);

	strncpy(messageBase->m_username, g_Options->GetControlUsername(), NZBREQUESTPASSWORDSIZE - 1);
	messageBase->m_username[NZBREQUESTPASSWORDSIZE - 1] = '\0';

	strncpy(messageBase->m_password, g_Options->GetControlPassword(), NZBREQUESTPASSWORDSIZE - 1);
	messageBase->m_password[NZBREQUESTPASSWORDSIZE - 1] = '\0';
}

bool RemoteClient::ReceiveBoolResponse()
{
	printf("Request sent\n");

	// all bool-responses have the same format of structure, we use SNZBDownloadResponse here
	SNzbDownloadResponse BoolResponse;
	memset(&BoolResponse, 0, sizeof(BoolResponse));

	bool read = m_connection->Recv((char*)&BoolResponse, sizeof(BoolResponse));
	if (!read ||
		(int)ntohl(BoolResponse.m_messageBase.m_signature) != (int)NZBMESSAGE_SIGNATURE ||
		ntohl(BoolResponse.m_messageBase.m_structSize) != sizeof(BoolResponse))
	{
		printf("No response or invalid response (timeout, not nzbget-server or wrong nzbget-server version)\n");
		return false;
	}

	int textLen = ntohl(BoolResponse.m_trailingDataLength);
	char* buf = (char*)malloc(textLen);
	read = m_connection->Recv(buf, textLen);
	if (!read)
	{
		printf("No response or invalid response (timeout, not nzbget-server or wrong nzbget-server version)\n");
		free(buf);
		return false;
	}

	printf("server returned: %s\n", buf);
	free(buf);
	return ntohl(BoolResponse.m_success);
}

/*
 * Sends a message to the running nzbget process.
 */
bool RemoteClient::RequestServerDownload(const char* nzbFilename, const char* nzbContent,
	const char* category, bool addFirst, bool addPaused, int priority,
	const char* dupeKey, int dupeMode, int dupeScore)
{
	// Read the file into the buffer
	char* buffer = NULL;
	int length = 0;
	bool isUrl = !strncasecmp(nzbContent, "http://", 6) || !strncasecmp(nzbContent, "https://", 7);
	if (isUrl)
	{
		length = strlen(nzbContent) + 1;
	}
	else
	{
		if (!Util::LoadFileIntoBuffer(nzbContent, &buffer, &length))
		{
			printf("Could not load file %s\n", nzbContent);
			return false;
		}
		length--;
	}

	bool OK = InitConnection();
	if (OK)
	{
		SNzbDownloadRequest DownloadRequest;
		InitMessageBase(&DownloadRequest.m_messageBase, rrDownload, sizeof(DownloadRequest));
		DownloadRequest.m_addFirst = htonl(addFirst);
		DownloadRequest.m_addPaused = htonl(addPaused);
		DownloadRequest.m_priority = htonl(priority);
		DownloadRequest.m_dupeMode = htonl(dupeMode);
		DownloadRequest.m_dupeScore = htonl(dupeScore);
		DownloadRequest.m_trailingDataLength = htonl(length);

		DownloadRequest.m_nzbFilename[0] = '\0';
		if (!Util::EmptyStr(nzbFilename))
		{
			strncpy(DownloadRequest.m_nzbFilename, nzbFilename, NZBREQUESTFILENAMESIZE - 1);
		}
		else if (!isUrl)
		{
			strncpy(DownloadRequest.m_nzbFilename, nzbContent, NZBREQUESTFILENAMESIZE - 1);
		}
		DownloadRequest.m_nzbFilename[NZBREQUESTFILENAMESIZE-1] = '\0';

		DownloadRequest.m_category[0] = '\0';
		if (category)
		{
			strncpy(DownloadRequest.m_category, category, NZBREQUESTFILENAMESIZE - 1);
		}
		DownloadRequest.m_category[NZBREQUESTFILENAMESIZE-1] = '\0';

		DownloadRequest.m_dupeKey[0] = '\0';
		if (!Util::EmptyStr(dupeKey))
		{
			strncpy(DownloadRequest.m_dupeKey, dupeKey, NZBREQUESTFILENAMESIZE - 1);
		}
		DownloadRequest.m_dupeKey[NZBREQUESTFILENAMESIZE-1] = '\0';

		if (!m_connection->Send((char*)(&DownloadRequest), sizeof(DownloadRequest)))
		{
			perror("m_pConnection->Send");
			OK = false;
		}
		else
		{
			m_connection->Send(isUrl ? nzbContent : buffer, length);
			OK = ReceiveBoolResponse();
			m_connection->Disconnect();
		}
	}

	// Cleanup
	free(buffer);

	return OK;
}

void RemoteClient::BuildFileList(SNzbListResponse* listResponse, const char* trailingData, DownloadQueue* downloadQueue)
{
	if (ntohl(listResponse->m_trailingDataLength) > 0)
	{
		const char* bufPtr = trailingData;

		// read nzb entries
		for (uint32 i = 0; i < ntohl(listResponse->m_nrTrailingNzbEntries); i++)
		{
			SNzbListResponseNzbEntry* listAnswer = (SNzbListResponseNzbEntry*) bufPtr;

			const char* fileName = bufPtr + sizeof(SNzbListResponseNzbEntry);
			const char* name = bufPtr + sizeof(SNzbListResponseNzbEntry) + ntohl(listAnswer->m_filenameLen);
			const char* destDir = bufPtr + sizeof(SNzbListResponseNzbEntry) + ntohl(listAnswer->m_filenameLen) +
				ntohl(listAnswer->m_nameLen);
			const char* category = bufPtr + sizeof(SNzbListResponseNzbEntry) + ntohl(listAnswer->m_filenameLen) +
				ntohl(listAnswer->m_nameLen) + ntohl(listAnswer->m_destDirLen);
			const char* m_queuedFilename = bufPtr + sizeof(SNzbListResponseNzbEntry) + ntohl(listAnswer->m_filenameLen) +
				ntohl(listAnswer->m_nameLen) + ntohl(listAnswer->m_destDirLen) + ntohl(listAnswer->m_categoryLen);

			MatchedNzbInfo* nzbInfo = new MatchedNzbInfo();
			nzbInfo->SetId(ntohl(listAnswer->m_id));
			nzbInfo->SetKind((NzbInfo::EKind)ntohl(listAnswer->m_kind));
			nzbInfo->SetSize(Util::JoinInt64(ntohl(listAnswer->m_sizeHi), ntohl(listAnswer->m_sizeLo)));
			nzbInfo->SetRemainingSize(Util::JoinInt64(ntohl(listAnswer->m_remainingSizeHi), ntohl(listAnswer->m_remainingSizeLo)));
			nzbInfo->SetPausedSize(Util::JoinInt64(ntohl(listAnswer->m_pausedSizeHi), ntohl(listAnswer->m_pausedSizeLo)));
			nzbInfo->SetPausedFileCount(ntohl(listAnswer->m_pausedCount));
			nzbInfo->SetRemainingParCount(ntohl(listAnswer->m_remainingParCount));
			nzbInfo->SetFilename(fileName);
			nzbInfo->SetName(name);
			nzbInfo->SetDestDir(destDir);
			nzbInfo->SetCategory(category);
			nzbInfo->SetQueuedFilename(m_queuedFilename);
			nzbInfo->SetPriority(ntohl(listAnswer->m_priority));
			nzbInfo->m_match = ntohl(listAnswer->m_match);

			downloadQueue->GetQueue()->push_back(nzbInfo);

			bufPtr += sizeof(SNzbListResponseNzbEntry) + ntohl(listAnswer->m_filenameLen) +
				ntohl(listAnswer->m_nameLen) + ntohl(listAnswer->m_destDirLen) +
				ntohl(listAnswer->m_categoryLen) + ntohl(listAnswer->m_queuedFilenameLen);
		}

		//read ppp entries
		for (uint32 i = 0; i < ntohl(listResponse->m_nrTrailingPPPEntries); i++)
		{
			SNzbListResponsePPPEntry* listAnswer = (SNzbListResponsePPPEntry*) bufPtr;

			const char* name = bufPtr + sizeof(SNzbListResponsePPPEntry);
			const char* value = bufPtr + sizeof(SNzbListResponsePPPEntry) + ntohl(listAnswer->m_nameLen);

			NzbInfo* nzbInfo = downloadQueue->GetQueue()->at(ntohl(listAnswer->m_nzbIndex) - 1);
			nzbInfo->GetParameters()->SetParameter(name, value);

			bufPtr += sizeof(SNzbListResponsePPPEntry) + ntohl(listAnswer->m_nameLen) +
				ntohl(listAnswer->m_valueLen);
		}

		//read file entries
		for (uint32 i = 0; i < ntohl(listResponse->m_nrTrailingFileEntries); i++)
		{
			SNzbListResponseFileEntry* listAnswer = (SNzbListResponseFileEntry*) bufPtr;

			const char* subject = bufPtr + sizeof(SNzbListResponseFileEntry);
			const char* fileName = bufPtr + sizeof(SNzbListResponseFileEntry) + ntohl(listAnswer->m_subjectLen);

			MatchedFileInfo* fileInfo = new MatchedFileInfo();
			fileInfo->SetId(ntohl(listAnswer->m_id));
			fileInfo->SetSize(Util::JoinInt64(ntohl(listAnswer->m_fileSizeHi), ntohl(listAnswer->m_fileSizeLo)));
			fileInfo->SetRemainingSize(Util::JoinInt64(ntohl(listAnswer->m_remainingSizeHi), ntohl(listAnswer->m_remainingSizeLo)));
			fileInfo->SetPaused(ntohl(listAnswer->m_paused));
			fileInfo->SetSubject(subject);
			fileInfo->SetFilename(fileName);
			fileInfo->SetFilenameConfirmed(ntohl(listAnswer->m_filenameConfirmed));
			fileInfo->SetActiveDownloads(ntohl(listAnswer->m_activeDownloads));
			fileInfo->m_match = ntohl(listAnswer->m_match);

			NzbInfo* nzbInfo = downloadQueue->GetQueue()->at(ntohl(listAnswer->m_nzbIndex) - 1);
			fileInfo->SetNzbInfo(nzbInfo);
			nzbInfo->GetFileList()->push_back(fileInfo);

			bufPtr += sizeof(SNzbListResponseFileEntry) + ntohl(listAnswer->m_subjectLen) +
				ntohl(listAnswer->m_filenameLen);
		}
	}
}

bool RemoteClient::RequestServerList(bool files, bool groups, const char* pattern)
{
	if (!InitConnection()) return false;

	SNzbListRequest ListRequest;
	InitMessageBase(&ListRequest.m_messageBase, rrList, sizeof(ListRequest));
	ListRequest.m_fileList = htonl(true);
	ListRequest.m_serverState = htonl(true);
	ListRequest.m_matchMode = htonl(pattern ? rmRegEx : rmId);
	ListRequest.m_matchGroup = htonl(groups);
	if (pattern)
	{
		strncpy(ListRequest.m_pattern, pattern, NZBREQUESTFILENAMESIZE - 1);
		ListRequest.m_pattern[NZBREQUESTFILENAMESIZE-1] = '\0';
	}

	if (!m_connection->Send((char*)(&ListRequest), sizeof(ListRequest)))
	{
		perror("m_pConnection->Send");
		return false;
	}

	printf("Request sent\n");

	// Now listen for the returned list
	SNzbListResponse ListResponse;
	bool read = m_connection->Recv((char*) &ListResponse, sizeof(ListResponse));
	if (!read ||
		(int)ntohl(ListResponse.m_messageBase.m_signature) != (int)NZBMESSAGE_SIGNATURE ||
		ntohl(ListResponse.m_messageBase.m_structSize) != sizeof(ListResponse))
	{
		printf("No response or invalid response (timeout, not nzbget-server or wrong nzbget-server version)\n");
		return false;
	}

	char* buf = NULL;
	if (ntohl(ListResponse.m_trailingDataLength) > 0)
	{
		buf = (char*)malloc(ntohl(ListResponse.m_trailingDataLength));
		if (!m_connection->Recv(buf, ntohl(ListResponse.m_trailingDataLength)))
		{
			free(buf);
			return false;
		}
	}

	m_connection->Disconnect();

	if (pattern && !ListResponse.m_regExValid)
	{
		printf("Error in regular expression\n");
		free(buf);
		return false;
	}

	if (files)
	{
		if (ntohl(ListResponse.m_trailingDataLength) == 0)
		{
			printf("Server has no files queued for download\n");
		}
		else
		{
			printf("Queue List\n");
			printf("-----------------------------------\n");

			DownloadQueue* downloadQueue = DownloadQueue::Lock();
			BuildFileList(&ListResponse, buf, downloadQueue);

			int64 remaining = 0;
			int64 paused = 0;
			int matches = 0;
			int nrFileEntries = 0;

			for (NzbList::iterator it = downloadQueue->GetQueue()->begin(); it != downloadQueue->GetQueue()->end(); it++)
			{
				NzbInfo* nzbInfo = *it;
				for (FileList::iterator it2 = nzbInfo->GetFileList()->begin(); it2 != nzbInfo->GetFileList()->end(); it2++)
				{
					FileInfo* fileInfo = *it2;

					nrFileEntries++;

					char completed[100];
					completed[0] = '\0';
					if (fileInfo->GetRemainingSize() < fileInfo->GetSize())
					{
						sprintf(completed, ", %i%s", (int)(100 - fileInfo->GetRemainingSize() * 100 / fileInfo->GetSize()), "%");
					}

					char threads[100];
					threads[0] = '\0';
					if (fileInfo->GetActiveDownloads() > 0)
					{
						sprintf(threads, ", %i thread%s", fileInfo->GetActiveDownloads(), (fileInfo->GetActiveDownloads() > 1 ? "s" : ""));
					}

					char status[100];
					if (fileInfo->GetPaused())
					{
						sprintf(status, " (paused)");
						paused += fileInfo->GetRemainingSize();
					}
					else
					{
						status[0] = '\0';
						remaining += fileInfo->GetRemainingSize();
					}

					if (!pattern || ((MatchedFileInfo*)fileInfo)->m_match)
					{
						char size[20];
						printf("[%i] %s/%s (%s%s%s)%s\n", fileInfo->GetId(), fileInfo->GetNzbInfo()->GetName(),
							fileInfo->GetFilename(),
							Util::FormatSize(size, sizeof(size), fileInfo->GetSize()),
							completed, threads, status);
						matches++;
					}
				}
			}

			DownloadQueue::Unlock();

			if (matches == 0)
			{
				printf("No matches founds\n");
			}

			printf("-----------------------------------\n");
			printf("Files: %i\n", nrFileEntries);
			if (pattern)
			{
				printf("Matches: %i\n", matches);
			}

			if (paused > 0)
			{
				char remainingBuf[20];
				char pausedSizeBuf[20];
				printf("Remaining size: %s (+%s paused)\n",
					Util::FormatSize(remainingBuf, sizeof(remainingBuf), remaining),
					Util::FormatSize(pausedSizeBuf, sizeof(pausedSizeBuf), paused));
			}
			else
			{
				char remainingBuf[20];
				printf("Remaining size: %s\n", Util::FormatSize(remainingBuf, sizeof(remainingBuf), remaining));
			}
		}
	}

	if (groups)
	{
		if (ntohl(ListResponse.m_trailingDataLength) == 0)
		{
			printf("Server has no files queued for download\n");
		}
		else
		{
			printf("Queue List\n");
			printf("-----------------------------------\n");

			DownloadQueue* downloadQueue = DownloadQueue::Lock();
			BuildFileList(&ListResponse, buf, downloadQueue);

			int64 remaining = 0;
			int64 paused = 0;
			int matches = 0;
			int nrFileEntries = 0;

			for (NzbList::iterator it = downloadQueue->GetQueue()->begin(); it != downloadQueue->GetQueue()->end(); it++)
			{
				NzbInfo* nzbInfo = *it;

				nrFileEntries += nzbInfo->GetFileList()->size();

				int64 unpausedRemainingSize = nzbInfo->GetRemainingSize() - nzbInfo->GetPausedSize();
				remaining += unpausedRemainingSize;

				char remainingStr[20];
				Util::FormatSize(remainingStr, sizeof(remainingStr), unpausedRemainingSize);

				char priority[100];
				priority[0] = '\0';
				if (nzbInfo->GetPriority() != 0)
				{
					sprintf(priority, "[%+i] ", nzbInfo->GetPriority());
				}

				char pausedStr[20];
				pausedStr[0] = '\0';
				if (nzbInfo->GetPausedSize() > 0)
				{
					char pausedSize[20];
					Util::FormatSize(pausedSize, sizeof(pausedSize), nzbInfo->GetPausedSize());
					sprintf(pausedStr, " + %s paused", pausedSize);
					paused += nzbInfo->GetPausedSize();
				}

				char category[1024];
				category[0] = '\0';
				if (nzbInfo->GetCategory() && strlen(nzbInfo->GetCategory()) > 0)
				{
					sprintf(category, " (%s)", nzbInfo->GetCategory());
				}

				char threads[100];
				threads[0] = '\0';
				if (nzbInfo->GetActiveDownloads() > 0)
				{
					sprintf(threads, ", %i thread%s", nzbInfo->GetActiveDownloads(), (nzbInfo->GetActiveDownloads() > 1 ? "s" : ""));
				}

				char parameters[1024];
				parameters[0] = '\0';
				for (NzbParameterList::iterator it = nzbInfo->GetParameters()->begin(); it != nzbInfo->GetParameters()->end(); it++)
				{
					if (parameters[0] == '\0')
					{
						strncat(parameters, " (", sizeof(parameters) - strlen(parameters) - 1);
					}
					else
					{
						strncat(parameters, ", ", sizeof(parameters) - strlen(parameters) - 1);
					}
					NzbParameter* nzbParameter = *it;
					strncat(parameters, nzbParameter->GetName(), sizeof(parameters) - strlen(parameters) - 1);
					strncat(parameters, "=", sizeof(parameters) - strlen(parameters) - 1);
					strncat(parameters, nzbParameter->GetValue(), sizeof(parameters) - strlen(parameters) - 1);
				}
				if (parameters[0] != '\0')
				{
					strncat(parameters, ")", sizeof(parameters) - strlen(parameters) - 1);
				}

				char urlOrFile[100];
				if (nzbInfo->GetKind() == NzbInfo::nkUrl)
				{
					strncpy(urlOrFile, "URL", sizeof(urlOrFile));
				}
				else
				{
					snprintf(urlOrFile, sizeof(urlOrFile), "%i file%s", (int)nzbInfo->GetFileList()->size(),
						nzbInfo->GetFileList()->size() > 1 ? "s" : "");
					urlOrFile[100-1] = '\0';
				}

				if (!pattern || ((MatchedNzbInfo*)nzbInfo)->m_match)
				{
					printf("[%i] %s%s (%s, %s%s%s)%s%s\n", nzbInfo->GetId(), priority,
						nzbInfo->GetName(), urlOrFile, remainingStr,
						pausedStr, threads, category, parameters);
					matches++;
				}
			}

			if (matches == 0)
			{
				printf("No matches founds\n");
			}

			printf("-----------------------------------\n");
			printf("Groups: %i\n", downloadQueue->GetQueue()->size());
			if (pattern)
			{
				printf("Matches: %i\n", matches);
			}
			printf("Files: %i\n", nrFileEntries);

			if (paused > 0)
			{
				char remainingBuf[20];
				char pausedSizeBuf[20];
				printf("Remaining size: %s (+%s paused)\n",
					Util::FormatSize(remainingBuf, sizeof(remainingBuf), remaining),
					Util::FormatSize(pausedSizeBuf, sizeof(pausedSizeBuf), paused));
			}
			else
			{
				char remainingBuf[20];
				printf("Remaining size: %s\n",
					Util::FormatSize(remainingBuf, sizeof(remainingBuf), remaining));
			}

			DownloadQueue::Unlock();
		}
	}

	free(buf);

	int64 remaining = Util::JoinInt64(ntohl(ListResponse.m_remainingSizeHi), ntohl(ListResponse.m_remainingSizeLo));

	if (!files && !groups)
	{
		char remainingBuf[20];
		printf("Remaining size: %s\n", Util::FormatSize(remainingBuf, sizeof(remainingBuf), remaining));
	}

	if (ntohl(ListResponse.m_downloadRate) > 0 &&
		!ntohl(ListResponse.m_downloadPaused) &&
		!ntohl(ListResponse.m_download2Paused) &&
		!ntohl(ListResponse.m_downloadStandBy))
	{
		int64 remain_sec = (int64)(remaining / ntohl(ListResponse.m_downloadRate));
		int h = (int)(remain_sec / 3600);
		int m = (int)((remain_sec % 3600) / 60);
		int s = (int)(remain_sec % 60);
		printf("Remaining time: %.2d:%.2d:%.2d\n", h, m, s);
	}

	char speed[20];
	printf("Current download rate: %s\n",
		Util::FormatSpeed(speed, sizeof(speed), ntohl(ListResponse.m_downloadRate)));

	int64 allBytes = Util::JoinInt64(ntohl(ListResponse.m_downloadedBytesHi), ntohl(ListResponse.m_downloadedBytesLo));
	int averageSpeed = (int)(ntohl(ListResponse.m_downloadTimeSec) > 0 ? allBytes / ntohl(ListResponse.m_downloadTimeSec) : 0);
	printf("Session download rate: %s\n", Util::FormatSpeed(speed, sizeof(speed), averageSpeed));

	if (ntohl(ListResponse.m_downloadLimit) > 0)
	{
		printf("Speed limit: %s\n",
			Util::FormatSpeed(speed, sizeof(speed), ntohl(ListResponse.m_downloadLimit)));
	}

	int sec = ntohl(ListResponse.m_upTimeSec);
	int h = sec / 3600;
	int m = (sec % 3600) / 60;
	int s = sec % 60;
	printf("Up time: %.2d:%.2d:%.2d\n", h, m, s);

	sec = ntohl(ListResponse.m_downloadTimeSec);
	h = sec / 3600;
	m = (sec % 3600) / 60;
	s = sec % 60;
	printf("Download time: %.2d:%.2d:%.2d\n", h, m, s);

	char size[20];
	printf("Downloaded: %s\n", Util::FormatSize(size, sizeof(size), allBytes));

	printf("Threads running: %i\n", ntohl(ListResponse.m_threadCount));

	if (ntohl(ListResponse.m_postJobCount) > 0)
	{
		printf("Post-jobs: %i\n", (int)ntohl(ListResponse.m_postJobCount));
	}

	if (ntohl(ListResponse.m_scanPaused))
	{
		printf("Scan state: Paused\n");
	}

	char serverState[50];

	if (ntohl(ListResponse.m_downloadPaused) || ntohl(ListResponse.m_download2Paused))
	{
		snprintf(serverState, sizeof(serverState), "%s%s",
			ntohl(ListResponse.m_downloadStandBy) ? "Paused" : "Pausing",
			ntohl(ListResponse.m_downloadPaused) && ntohl(ListResponse.m_download2Paused) ?
			" (+2)" : ntohl(ListResponse.m_download2Paused) ? " (2)" : "");
	}
	else
	{
		snprintf(serverState, sizeof(serverState), "%s", ntohl(ListResponse.m_downloadStandBy) ? "" : "Downloading");
	}

	if (ntohl(ListResponse.m_postJobCount) > 0 || ntohl(ListResponse.m_postPaused))
	{
		strncat(serverState, strlen(serverState) > 0 ? ", Post-Processing" : "Post-Processing", sizeof(serverState) - strlen(serverState) - 1);
		if (ntohl(ListResponse.m_postPaused))
		{
			strncat(serverState, " paused", sizeof(serverState) - strlen(serverState) - 1);
		}
	}

	if (strlen(serverState) == 0)
	{
		strncpy(serverState, "Stand-By", sizeof(serverState));
	}

	printf("Server state: %s\n", serverState);

	return true;
}

bool RemoteClient::RequestServerLog(int lines)
{
	if (!InitConnection()) return false;

	SNzbLogRequest LogRequest;
	InitMessageBase(&LogRequest.m_messageBase, rrLog, sizeof(LogRequest));
	LogRequest.m_lines = htonl(lines);
	LogRequest.m_idFrom = 0;

	if (!m_connection->Send((char*)(&LogRequest), sizeof(LogRequest)))
	{
		perror("m_pConnection->Send");
		return false;
	}

	printf("Request sent\n");

	// Now listen for the returned log
	SNzbLogResponse LogResponse;
	bool read = m_connection->Recv((char*) &LogResponse, sizeof(LogResponse));
	if (!read ||
		(int)ntohl(LogResponse.m_messageBase.m_signature) != (int)NZBMESSAGE_SIGNATURE ||
		ntohl(LogResponse.m_messageBase.m_structSize) != sizeof(LogResponse))
	{
		printf("No response or invalid response (timeout, not nzbget-server or wrong nzbget-server version)\n");
		return false;
	}

	char* buf = NULL;
	if (ntohl(LogResponse.m_trailingDataLength) > 0)
	{
		buf = (char*)malloc(ntohl(LogResponse.m_trailingDataLength));
		if (!m_connection->Recv(buf, ntohl(LogResponse.m_trailingDataLength)))
		{
			free(buf);
			return false;
		}
	}

	m_connection->Disconnect();

	if (LogResponse.m_trailingDataLength == 0)
	{
		printf("Log is empty\n");
	}
	else
	{
		printf("Log (last %i entries)\n", ntohl(LogResponse.m_nrTrailingEntries));
		printf("-----------------------------------\n");

		char* bufPtr = (char*)buf;
		for (uint32 i = 0; i < ntohl(LogResponse.m_nrTrailingEntries); i++)
		{
			SNzbLogResponseEntry* logAnswer = (SNzbLogResponseEntry*) bufPtr;

			char* text = bufPtr + sizeof(SNzbLogResponseEntry);
			switch (ntohl(logAnswer->m_kind))
			{
				case Message::mkDebug:
					printf("[DEBUG] %s\n", text);
					break;
				case Message::mkError:
					printf("[ERROR] %s\n", text);
					break;
				case Message::mkWarning:
					printf("[WARNING] %s\n", text);
					break;
				case Message::mkInfo:
					printf("[INFO] %s\n", text);
					break;
				case Message::mkDetail:
					printf("[DETAIL] %s\n", text);
					break;
			}

			bufPtr += sizeof(SNzbLogResponseEntry) + ntohl(logAnswer->m_textLen);
		}

		printf("-----------------------------------\n");

		free(buf);
	}

	return true;
}

bool RemoteClient::RequestServerPauseUnpause(bool pause, ERemotePauseUnpauseAction action)
{
	if (!InitConnection()) return false;

	SNzbPauseUnpauseRequest PauseUnpauseRequest;
	InitMessageBase(&PauseUnpauseRequest.m_messageBase, rrPauseUnpause, sizeof(PauseUnpauseRequest));
	PauseUnpauseRequest.m_pause = htonl(pause);
	PauseUnpauseRequest.m_action = htonl(action);

	if (!m_connection->Send((char*)(&PauseUnpauseRequest), sizeof(PauseUnpauseRequest)))
	{
		perror("m_pConnection->Send");
		m_connection->Disconnect();
		return false;
	}

	bool OK = ReceiveBoolResponse();
	m_connection->Disconnect();

	return OK;
}

bool RemoteClient::RequestServerSetDownloadRate(int rate)
{
	if (!InitConnection()) return false;

	SNzbSetDownloadRateRequest SetDownloadRateRequest;
	InitMessageBase(&SetDownloadRateRequest.m_messageBase, rrSetDownloadRate, sizeof(SetDownloadRateRequest));
	SetDownloadRateRequest.m_downloadRate = htonl(rate);

	if (!m_connection->Send((char*)(&SetDownloadRateRequest), sizeof(SetDownloadRateRequest)))
	{
		perror("m_pConnection->Send");
		m_connection->Disconnect();
		return false;
	}

	bool OK = ReceiveBoolResponse();
	m_connection->Disconnect();

	return OK;
}

bool RemoteClient::RequestServerDumpDebug()
{
	if (!InitConnection()) return false;

	SNzbDumpDebugRequest DumpDebugInfo;
	InitMessageBase(&DumpDebugInfo.m_messageBase, rrDumpDebug, sizeof(DumpDebugInfo));

	if (!m_connection->Send((char*)(&DumpDebugInfo), sizeof(DumpDebugInfo)))
	{
		perror("m_pConnection->Send");
		m_connection->Disconnect();
		return false;
	}

	bool OK = ReceiveBoolResponse();
	m_connection->Disconnect();

	return OK;
}

bool RemoteClient::RequestServerEditQueue(DownloadQueue::EEditAction action, int offset, const char* text,
	int* idList, int idCount, NameList* nameList, ERemoteMatchMode matchMode)
{
	if ((idCount <= 0 || idList == NULL) && (nameList == NULL || nameList->size() == 0))
	{
		printf("File(s) not specified\n");
		return false;
	}

	if (!InitConnection()) return false;

	int idLength = sizeof(int32) * idCount;

	int nameCount = 0;
	int nameLength = 0;
	if (nameList && nameList->size() > 0)
	{
		for (NameList::iterator it = nameList->begin(); it != nameList->end(); it++)
		{
			const char *name = *it;
			nameLength += strlen(name) + 1;
			nameCount++;
		}
		// align size to 4-bytes, needed by ARM-processor (and may be others)
		nameLength += nameLength % 4 > 0 ? 4 - nameLength % 4 : 0;
	}

	int textLen = text ? strlen(text) + 1 : 0;
	// align size to 4-bytes, needed by ARM-processor (and may be others)
	textLen += textLen % 4 > 0 ? 4 - textLen % 4 : 0;

	int length = textLen + idLength + nameLength;

	SNzbEditQueueRequest EditQueueRequest;
	InitMessageBase(&EditQueueRequest.m_messageBase, rrEditQueue, sizeof(EditQueueRequest));
	EditQueueRequest.m_action = htonl(action);
	EditQueueRequest.m_matchMode = htonl(matchMode);
	EditQueueRequest.m_offset = htonl((int)offset);
	EditQueueRequest.m_textLen = htonl(textLen);
	EditQueueRequest.m_nrTrailingIdEntries = htonl(idCount);
	EditQueueRequest.m_nrTrailingNameEntries = htonl(nameCount);
	EditQueueRequest.m_trailingNameEntriesLen = htonl(nameLength);
	EditQueueRequest.m_trailingDataLength = htonl(length);

	char* trailingData = (char*)malloc(length);

	if (textLen > 0)
	{
		strcpy(trailingData, text);
	}

	int32* ids = (int32*)(trailingData + textLen);

	for (int i = 0; i < idCount; i++)
	{
		ids[i] = htonl(idList[i]);
	}

	if (nameCount > 0)
	{
		char *names = trailingData + textLen + idLength;
		for (NameList::iterator it = nameList->begin(); it != nameList->end(); it++)
		{
			const char *name = *it;
			int len = strlen(name);
			strncpy(names, name, len + 1);
			names += len + 1;
		}
	}

	bool OK = false;
	if (!m_connection->Send((char*)(&EditQueueRequest), sizeof(EditQueueRequest)))
	{
		perror("m_pConnection->Send");
	}
	else
	{
		m_connection->Send(trailingData, length);
		OK = ReceiveBoolResponse();
		m_connection->Disconnect();
	}

	free(trailingData);

	m_connection->Disconnect();
	return OK;
}

bool RemoteClient::RequestServerShutdown()
{
	if (!InitConnection()) return false;

	SNzbShutdownRequest ShutdownRequest;
	InitMessageBase(&ShutdownRequest.m_messageBase, rrShutdown, sizeof(ShutdownRequest));

	bool OK = m_connection->Send((char*)(&ShutdownRequest), sizeof(ShutdownRequest));
	if (OK)
	{
		OK = ReceiveBoolResponse();
	}
	else
	{
		perror("m_pConnection->Send");
	}

	m_connection->Disconnect();
	return OK;
}

bool RemoteClient::RequestServerReload()
{
	if (!InitConnection()) return false;

	SNzbReloadRequest ReloadRequest;
	InitMessageBase(&ReloadRequest.m_messageBase, rrReload, sizeof(ReloadRequest));

	bool OK = m_connection->Send((char*)(&ReloadRequest), sizeof(ReloadRequest));
	if (OK)
	{
		OK = ReceiveBoolResponse();
	}
	else
	{
		perror("m_pConnection->Send");
	}

	m_connection->Disconnect();
	return OK;
}

bool RemoteClient::RequestServerVersion()
{
	if (!InitConnection()) return false;

	SNzbVersionRequest VersionRequest;
	InitMessageBase(&VersionRequest.m_messageBase, rrVersion, sizeof(VersionRequest));

	bool OK = m_connection->Send((char*)(&VersionRequest), sizeof(VersionRequest));
	if (OK)
	{
		OK = ReceiveBoolResponse();
	}
	else
	{
		perror("m_pConnection->Send");
	}

	m_connection->Disconnect();
	return OK;
}

bool RemoteClient::RequestPostQueue()
{
	if (!InitConnection()) return false;

	SNzbPostQueueRequest PostQueueRequest;
	InitMessageBase(&PostQueueRequest.m_messageBase, rrPostQueue, sizeof(PostQueueRequest));

	if (!m_connection->Send((char*)(&PostQueueRequest), sizeof(PostQueueRequest)))
	{
		perror("m_pConnection->Send");
		return false;
	}

	printf("Request sent\n");

	// Now listen for the returned list
	SNzbPostQueueResponse PostQueueResponse;
	bool read = m_connection->Recv((char*) &PostQueueResponse, sizeof(PostQueueResponse));
	if (!read ||
		(int)ntohl(PostQueueResponse.m_messageBase.m_signature) != (int)NZBMESSAGE_SIGNATURE ||
		ntohl(PostQueueResponse.m_messageBase.m_structSize) != sizeof(PostQueueResponse))
	{
		printf("No response or invalid response (timeout, not nzbget-server or wrong nzbget-server version)\n");
		return false;
	}

	char* buf = NULL;
	if (ntohl(PostQueueResponse.m_trailingDataLength) > 0)
	{
		buf = (char*)malloc(ntohl(PostQueueResponse.m_trailingDataLength));
		if (!m_connection->Recv(buf, ntohl(PostQueueResponse.m_trailingDataLength)))
		{
			free(buf);
			return false;
		}
	}

	m_connection->Disconnect();

	if (ntohl(PostQueueResponse.m_trailingDataLength) == 0)
	{
		printf("Server has no jobs queued for post-processing\n");
	}
	else
	{
		printf("Post-Processing List\n");
		printf("-----------------------------------\n");

		char* bufPtr = (char*)buf;
		for (uint32 i = 0; i < ntohl(PostQueueResponse.m_nrTrailingEntries); i++)
		{
			SNzbPostQueueResponseEntry* postQueueAnswer = (SNzbPostQueueResponseEntry*) bufPtr;

			int stageProgress = ntohl(postQueueAnswer->m_stageProgress);

			char completed[100];
			completed[0] = '\0';
			if (stageProgress > 0 && (int)ntohl(postQueueAnswer->m_stage) != (int)PostInfo::ptExecutingScript)
			{
				sprintf(completed, ", %i%s", (int)(stageProgress / 10), "%");
			}

			const char* postStageName[] = { "", ", Loading Pars", ", Verifying source files", ", Repairing", ", Verifying repaired files", ", Unpacking", ", Executing postprocess-script", "" };
			char* infoName = bufPtr + sizeof(SNzbPostQueueResponseEntry) + ntohl(postQueueAnswer->m_nzbFilenameLen);

			printf("[%i] %s%s%s\n", ntohl(postQueueAnswer->m_id), infoName, postStageName[ntohl(postQueueAnswer->m_stage)], completed);

			bufPtr += sizeof(SNzbPostQueueResponseEntry) + ntohl(postQueueAnswer->m_nzbFilenameLen) +
				ntohl(postQueueAnswer->m_infoNameLen) + ntohl(postQueueAnswer->m_destDirLen) +
				ntohl(postQueueAnswer->m_progressLabelLen);
		}

		free(buf);

		printf("-----------------------------------\n");
	}

	return true;
}

bool RemoteClient::RequestWriteLog(int kind, const char* text)
{
	if (!InitConnection()) return false;

	SNzbWriteLogRequest WriteLogRequest;
	InitMessageBase(&WriteLogRequest.m_messageBase, rrWriteLog, sizeof(WriteLogRequest));
	WriteLogRequest.m_kind = htonl(kind);
	int length = strlen(text) + 1;
	WriteLogRequest.m_trailingDataLength = htonl(length);

	if (!m_connection->Send((char*)(&WriteLogRequest), sizeof(WriteLogRequest)))
	{
		perror("m_pConnection->Send");
		return false;
	}

	m_connection->Send(text, length);
	bool OK = ReceiveBoolResponse();
	m_connection->Disconnect();
	return OK;
}

bool RemoteClient::RequestScan(bool syncMode)
{
	if (!InitConnection()) return false;

	SNzbScanRequest ScanRequest;
	InitMessageBase(&ScanRequest.m_messageBase, rrScan, sizeof(ScanRequest));

	ScanRequest.m_syncMode = htonl(syncMode);

	bool OK = m_connection->Send((char*)(&ScanRequest), sizeof(ScanRequest));
	if (OK)
	{
		OK = ReceiveBoolResponse();
	}
	else
	{
		perror("m_pConnection->Send");
	}

	m_connection->Disconnect();
	return OK;
}

bool RemoteClient::RequestHistory(bool withHidden)
{
	if (!InitConnection()) return false;

	SNzbHistoryRequest HistoryRequest;
	InitMessageBase(&HistoryRequest.m_messageBase, rrHistory, sizeof(HistoryRequest));
	HistoryRequest.m_hidden = htonl(withHidden);

	if (!m_connection->Send((char*)(&HistoryRequest), sizeof(HistoryRequest)))
	{
		perror("m_pConnection->Send");
		return false;
	}

	printf("Request sent\n");

	// Now listen for the returned list
	SNzbHistoryResponse HistoryResponse;
	bool read = m_connection->Recv((char*) &HistoryResponse, sizeof(HistoryResponse));
	if (!read ||
		(int)ntohl(HistoryResponse.m_messageBase.m_signature) != (int)NZBMESSAGE_SIGNATURE ||
		ntohl(HistoryResponse.m_messageBase.m_structSize) != sizeof(HistoryResponse))
	{
		printf("No response or invalid response (timeout, not nzbget-server or wrong nzbget-server version)\n");
		return false;
	}

	char* buf = NULL;
	if (ntohl(HistoryResponse.m_trailingDataLength) > 0)
	{
		buf = (char*)malloc(ntohl(HistoryResponse.m_trailingDataLength));
		if (!m_connection->Recv(buf, ntohl(HistoryResponse.m_trailingDataLength)))
		{
			free(buf);
			return false;
		}
	}

	m_connection->Disconnect();

	if (ntohl(HistoryResponse.m_trailingDataLength) == 0)
	{
		printf("Server has no files in history\n");
	}
	else
	{
		printf("History (most recent first)\n");
		printf("-----------------------------------\n");

		char* bufPtr = (char*)buf;
		for (uint32 i = 0; i < ntohl(HistoryResponse.m_nrTrailingEntries); i++)
		{
			SNzbHistoryResponseEntry* listAnswer = (SNzbHistoryResponseEntry*) bufPtr;

			HistoryInfo::EKind kind = (HistoryInfo::EKind)ntohl(listAnswer->m_kind);
			const char* nicename = bufPtr + sizeof(SNzbHistoryResponseEntry);

			if (kind == HistoryInfo::hkNzb || kind == HistoryInfo::hkDup)
			{
				char files[20];
				snprintf(files, sizeof(files), "%i files, ", ntohl(listAnswer->m_fileCount));
				files[20 - 1] = '\0';

				int64 size = Util::JoinInt64(ntohl(listAnswer->m_sizeHi), ntohl(listAnswer->m_sizeLo));

				char sizeStr[20];
				Util::FormatSize(sizeStr, sizeof(sizeStr), size);

				const char* parStatusText[] = { "", "", ", Par failed", ", Par successful", ", Repair possible", ", Repair needed" };
				const char* scriptStatusText[] = { "", ", Script status unknown", ", Script failed", ", Script successful" };
				int parStatus = ntohl(listAnswer->m_parStatus);
				int scriptStatus = ntohl(listAnswer->m_scriptStatus);

				printf("[%i] %s (%s%s%s%s%s)\n", ntohl(listAnswer->m_id), nicename,
					(kind == HistoryInfo::hkDup ? "Hidden, " : ""),
					(kind == HistoryInfo::hkDup ? "" : files), sizeStr,
					(kind == HistoryInfo::hkDup ? "" : parStatusText[parStatus]),
					(kind == HistoryInfo::hkDup ? "" : scriptStatusText[scriptStatus]));
			}
			else if (kind == HistoryInfo::hkUrl)
			{
				const char* urlStatusText[] = { "", "", "Url download successful", "Url download failed", "", "Nzb scan skipped", "Nzb scan failed" };

				printf("[%i] %s (URL, %s)\n", ntohl(listAnswer->m_id), nicename,
					urlStatusText[ntohl(listAnswer->m_urlStatus)]);
			}

			bufPtr += sizeof(SNzbHistoryResponseEntry) + ntohl(listAnswer->m_nicenameLen);
		}

		printf("-----------------------------------\n");
		printf("Items: %i\n", ntohl(HistoryResponse.m_nrTrailingEntries));
	}

	free(buf);

	return true;
}
