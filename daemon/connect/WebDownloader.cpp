/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2012-2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
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
#include "WebDownloader.h"
#include "Log.h"
#include "Options.h"
#include "Util.h"
#include "FileSystem.h"

WebDownloader::WebDownloader()
{
	debug("Creating WebDownloader");

	m_connection = NULL;
	m_confirmedLength = false;
	m_status = adUndefined;
	m_force = false;
	m_retry = true;
	SetLastUpdateTimeNow();
}

void WebDownloader::SetUrl(const char* url)
{
	m_url = WebUtil::UrlEncode(url);
}

void WebDownloader::SetStatus(EStatus status)
{
	m_status = status;
	Notify(NULL);
}

void WebDownloader::Run()
{
	debug("Entering WebDownloader-loop");

	SetStatus(adRunning);

	int remainedDownloadRetries = g_Options->GetRetries() > 0 ? g_Options->GetRetries() : 1;
	int remainedConnectRetries = remainedDownloadRetries > 10 ? remainedDownloadRetries : 10;
	if (!m_retry)
	{
		remainedDownloadRetries = 1;
		remainedConnectRetries = 1;
	}

	EStatus Status = adFailed;

	while (!IsStopped() && remainedDownloadRetries > 0 && remainedConnectRetries > 0)
	{
		SetLastUpdateTimeNow();

		Status = DownloadWithRedirects(5);

		if ((((Status == adFailed) && (remainedDownloadRetries > 1)) ||
			((Status == adConnectError) && (remainedConnectRetries > 1)))
			&& !IsStopped() && !(!m_force && g_Options->GetPauseDownload()))
		{
			detail("Waiting %i sec to retry", g_Options->GetRetryInterval());
			int msec = 0;
			while (!IsStopped() && (msec < g_Options->GetRetryInterval() * 1000) &&
				!(!m_force && g_Options->GetPauseDownload()))
			{
				usleep(100 * 1000);
				msec += 100;
			}
		}

		if (IsStopped() || (!m_force && g_Options->GetPauseDownload()))
		{
			Status = adRetry;
			break;
		}

		if (Status == adFinished || Status == adFatalError || Status == adNotFound)
		{
			break;
		}

		if (Status != adConnectError)
		{
			remainedDownloadRetries--;
		}
		else
		{
			remainedConnectRetries--;
		}
	}

	if (Status != adFinished && Status != adRetry)
	{
		Status = adFailed;
	}

	if (Status == adFailed)
	{
		if (IsStopped())
		{
			detail("Download %s cancelled", *m_infoName);
		}
		else
		{
			error("Download %s failed", *m_infoName);
		}
	}

	if (Status == adFinished)
	{
		detail("Download %s completed", *m_infoName);
	}

	SetStatus(Status);

	debug("Exiting WebDownloader-loop");
}

WebDownloader::EStatus WebDownloader::Download()
{
	EStatus Status = adRunning;

	URL url(m_url);

	Status = CreateConnection(&url);
	if (Status != adRunning)
	{
		return Status;
	}

	m_connection->SetTimeout(g_Options->GetUrlTimeout());
	m_connection->SetSuppressErrors(false);

	// connection
	bool connected = m_connection->Connect();
	if (!connected || IsStopped())
	{
		FreeConnection();
		return adConnectError;
	}

	// Okay, we got a Connection. Now start downloading.
	detail("Downloading %s", *m_infoName);

	SendHeaders(&url);

	Status = DownloadHeaders();

	if (Status == adRunning)
	{
		Status = DownloadBody();
	}

	if (IsStopped())
	{
		Status = adFailed;
	}

	FreeConnection();

	if (Status != adFinished)
	{
		// Download failed, delete broken output file
		remove(m_outputFilename);
	}

	return Status;
}

WebDownloader::EStatus WebDownloader::DownloadWithRedirects(int maxRedirects)
{
	// do sync download, following redirects
	EStatus status = adRedirect;
	while (status == adRedirect && maxRedirects >= 0)
	{
		maxRedirects--;
		status = Download();
	}

	if (status == adRedirect && maxRedirects < 0)
	{
		warn("Too many redirects for %s", *m_infoName);
		status = adFailed;
	}

	return status;
}

WebDownloader::EStatus WebDownloader::CreateConnection(URL *url)
{
	if (!url->IsValid())
	{
		error("URL is not valid: %s", url->GetAddress());
		return adFatalError;
	}

	int port = url->GetPort();
	if (port == 0 && !strcasecmp(url->GetProtocol(), "http"))
	{
		port = 80;
	}
	if (port == 0 && !strcasecmp(url->GetProtocol(), "https"))
	{
		port = 443;
	}

	if (strcasecmp(url->GetProtocol(), "http") && strcasecmp(url->GetProtocol(), "https"))
	{
		error("Unsupported protocol in URL: %s", url->GetAddress());
		return adFatalError;
	}

#ifdef DISABLE_TLS
	if (!strcasecmp(url->GetProtocol(), "https"))
	{
		error("Program was compiled without TLS/SSL-support. Cannot download using https protocol. URL: %s", url->GetAddress());
		return adFatalError;
	}
#endif

	bool tls = !strcasecmp(url->GetProtocol(), "https");

	m_connection = new Connection(url->GetHost(), port, tls);

	return adRunning;
}

void WebDownloader::SendHeaders(URL *url)
{
	// retrieve file
	m_connection->WriteLine(BString<1024>("GET %s HTTP/1.0\r\n", url->GetResource()));
	m_connection->WriteLine(BString<1024>("User-Agent: nzbget/%s\r\n", Util::VersionRevision()));

	if ((!strcasecmp(url->GetProtocol(), "http") && (url->GetPort() == 80 || url->GetPort() == 0)) ||
		(!strcasecmp(url->GetProtocol(), "https") && (url->GetPort() == 443 || url->GetPort() == 0)))
	{
		m_connection->WriteLine(BString<1024>("Host: %s\r\n", url->GetHost()));
	}
	else
	{
		m_connection->WriteLine(BString<1024>("Host: %s:%i\r\n", url->GetHost(), url->GetPort()));
	}

	m_connection->WriteLine("Accept: */*\r\n");
#ifndef DISABLE_GZIP
	m_connection->WriteLine("Accept-Encoding: gzip\r\n");
#endif
	m_connection->WriteLine("Connection: close\r\n");
	m_connection->WriteLine("\r\n");
}

WebDownloader::EStatus WebDownloader::DownloadHeaders()
{
	EStatus Status = adRunning;

	m_confirmedLength = false;
	const int LineBufSize = 1024*10;
	char* lineBuf = (char*)malloc(LineBufSize);
	m_contentLen = -1;
	bool firstLine = true;
	m_gzip = false;
	m_redirecting = false;
	m_redirected = false;

	// Headers
	while (!IsStopped())
	{
		SetLastUpdateTimeNow();

		int len = 0;
		char* line = m_connection->ReadLine(lineBuf, LineBufSize, &len);

		if (firstLine)
		{
			Status = CheckResponse(lineBuf);
			if (Status != adRunning)
			{
				break;
			}
			firstLine = false;
		}

		// Have we encountered a timeout?
		if (!line)
		{
			if (!IsStopped())
			{
				warn("URL %s failed: Unexpected end of file", *m_infoName);
			}
			Status = adFailed;
			break;
		}

		debug("Header: %s", line);

		// detect body of response
		if (*line == '\r' || *line == '\n')
		{
			break;
		}

		Util::TrimRight(line);
		ProcessHeader(line);

		if (m_redirected)
		{
			Status = adRedirect;
			break;
		}
	}

	free(lineBuf);

	return Status;
}

WebDownloader::EStatus WebDownloader::DownloadBody()
{
	EStatus Status = adRunning;

	m_outFile = NULL;
	bool end = false;
	const int LineBufSize = 1024*10;
	char* lineBuf = (char*)malloc(LineBufSize);
	int writtenLen = 0;

#ifndef DISABLE_GZIP
	m_gUnzipStream = NULL;
	if (m_gzip)
	{
		m_gUnzipStream = new GUnzipStream(1024*10);
	}
#endif

	// Body
	while (!IsStopped())
	{
		SetLastUpdateTimeNow();

		char* buffer;
		int len;
		m_connection->ReadBuffer(&buffer, &len);
		if (len == 0)
		{
			len = m_connection->TryRecv(lineBuf, LineBufSize);
			buffer = lineBuf;
		}

		// Connection closed or timeout?
		if (len <= 0)
		{
			if (len == 0 && m_contentLen == -1 && writtenLen > 0)
			{
				end = true;
				break;
			}

			if (!IsStopped())
			{
				warn("URL %s failed: Unexpected end of file", *m_infoName);
			}
			Status = adFailed;
			break;
		}

		// write to output file
		if (!Write(buffer, len))
		{
			Status = adFatalError;
			break;
		}
		writtenLen += len;

		//detect end of file
		if (writtenLen == m_contentLen || (m_contentLen == -1 && m_gzip && m_confirmedLength))
		{
			end = true;
			break;
		}
	}

	free(lineBuf);

#ifndef DISABLE_GZIP
	delete m_gUnzipStream;
#endif

	if (m_outFile)
	{
		fclose(m_outFile);
	}

	if (!end && Status == adRunning && !IsStopped())
	{
		warn("URL %s failed: file incomplete", *m_infoName);
		Status = adFailed;
	}

	if (end)
	{
		Status = adFinished;
	}

	return Status;
}

WebDownloader::EStatus WebDownloader::CheckResponse(const char* response)
{
	if (!response)
	{
		if (!IsStopped())
		{
			warn("URL %s: Connection closed by remote host", *m_infoName);
		}
		return adConnectError;
	}

	const char* hTTPResponse = strchr(response, ' ');
	if (strncmp(response, "HTTP", 4) || !hTTPResponse)
	{
		warn("URL %s failed: %s", *m_infoName, response);
		return adFailed;
	}

	hTTPResponse++;

	if (!strncmp(hTTPResponse, "400", 3) || !strncmp(hTTPResponse, "499", 3))
	{
		warn("URL %s failed: %s", *m_infoName, hTTPResponse);
		return adConnectError;
	}
	else if (!strncmp(hTTPResponse, "404", 3))
	{
		warn("URL %s failed: %s", *m_infoName, hTTPResponse);
		return adNotFound;
	}
	else if (!strncmp(hTTPResponse, "301", 3) || !strncmp(hTTPResponse, "302", 3))
	{
		m_redirecting = true;
		return adRunning;
	}
	else if (!strncmp(hTTPResponse, "200", 3))
	{
		// OK
		return adRunning;
	}
	else
	{
		// unknown error, no special handling
		warn("URL %s failed: %s", *m_infoName, response);
		return adFailed;
	}
}

void WebDownloader::ProcessHeader(const char* line)
{
	if (!strncasecmp(line, "Content-Length: ", 16))
	{
		m_contentLen = atoi(line + 16);
		m_confirmedLength = true;
	}
	else if (!strncasecmp(line, "Content-Encoding: gzip", 22))
	{
		m_gzip = true;
	}
	else if (!strncasecmp(line, "Content-Disposition: ", 21))
	{
		ParseFilename(line);
	}
	else if (m_redirecting && !strncasecmp(line, "Location: ", 10))
	{
		ParseRedirect(line + 10);
		m_redirected = true;
	}
}

void WebDownloader::ParseFilename(const char* contentDisposition)
{
	// Examples:
	// Content-Disposition: attachment; filename="fname.ext"
	// Content-Disposition: attachement;filename=fname.ext
	// Content-Disposition: attachement;filename=fname.ext;
	const char *p = strstr(contentDisposition, "filename");
	if (!p)
	{
		return;
	}

	p = strchr(p, '=');
	if (!p)
	{
		return;
	}

	p++;

	while (*p == ' ') p++;

	BString<1024> fname = p;

	char *pe = fname + strlen(fname) - 1;
	while ((*pe == ' ' || *pe == '\n' || *pe == '\r' || *pe == ';') && pe > fname) {
		*pe = '\0';
		pe--;
	}

	WebUtil::HttpUnquote(fname);

	m_originalFilename = FileSystem::BaseFileName(fname);

	debug("OriginalFilename: %s", *m_originalFilename);
}

void WebDownloader::ParseRedirect(const char* location)
{
	const char* newLocation = location;
	BString<1024> urlBuf;
	URL newUrl(newLocation);
	if (!newUrl.IsValid())
	{
		// redirect within host

		BString<1024> resource;
		URL oldUrl(m_url);

		if (*location == '/')
		{
			// absolute path within host
			resource = location;
		}
		else
		{
			// relative path within host
			resource = oldUrl.GetResource();

			char* p = strchr(resource, '?');
			if (p)
			{
				*p = '\0';
			}

			p = strrchr(resource, '/');
			if (p)
			{
				p[1] = '\0';
			}

			resource.Append(location);
		}

		if (oldUrl.GetPort() > 0)
		{
			urlBuf.Format("%s://%s:%i%s", oldUrl.GetProtocol(), oldUrl.GetHost(), oldUrl.GetPort(), *resource);
		}
		else
		{
			urlBuf.Format("%s://%s%s", oldUrl.GetProtocol(), oldUrl.GetHost(), *resource);
		}
		newLocation = urlBuf;
	}
	detail("URL %s redirected to %s", *m_url, newLocation);
	SetUrl(newLocation);
}

bool WebDownloader::Write(void* buffer, int len)
{
	if (!m_outFile && !PrepareFile())
	{
		return false;
	}

#ifndef DISABLE_GZIP
	if (m_gzip)
	{
		m_gUnzipStream->Write(buffer, len);
		const void *outBuf;
		int outLen = 1;
		while (outLen > 0)
		{
			GUnzipStream::EStatus gZStatus = m_gUnzipStream->Read(&outBuf, &outLen);

			if (gZStatus == GUnzipStream::zlError)
			{
				error("URL %s: GUnzip failed", *m_infoName);
				return false;
			}

			if (outLen > 0 && fwrite(outBuf, 1, outLen, m_outFile) <= 0)
			{
				return false;
			}

			if (gZStatus == GUnzipStream::zlFinished)
			{
				m_confirmedLength = true;
				return true;
			}
		}
		return true;
	}
	else
#endif

	return fwrite(buffer, 1, len, m_outFile) > 0;
}

bool WebDownloader::PrepareFile()
{
	// prepare file for writing

	const char* filename = m_outputFilename;
	m_outFile = fopen(filename, FOPEN_WB);
	if (!m_outFile)
	{
		error("Could not %s file %s", "create", filename);
		return false;
	}
	if (g_Options->GetWriteBuffer() > 0)
	{
		setvbuf(m_outFile, NULL, _IOFBF, g_Options->GetWriteBuffer() * 1024);
	}

	return true;
}

void WebDownloader::LogDebugInfo()
{
	char time[50];
#ifdef HAVE_CTIME_R_3
		ctime_r(&m_lastUpdateTime, time, 50);
#else
		ctime_r(&m_lastUpdateTime, time);
#endif

	info("      Web-Download: status=%i, LastUpdateTime=%s, filename=%s", m_status, time, FileSystem::BaseFileName(m_outputFilename));
}

void WebDownloader::Stop()
{
	debug("Trying to stop WebDownloader");
	Thread::Stop();
	m_connectionMutex.Lock();
	if (m_connection)
	{
		m_connection->SetSuppressErrors(true);
		m_connection->Cancel();
	}
	m_connectionMutex.Unlock();
	debug("WebDownloader stopped successfully");
}

bool WebDownloader::Terminate()
{
	Connection* connection = m_connection;
	bool terminated = Kill();
	if (terminated && connection)
	{
		debug("Terminating connection");
		connection->SetSuppressErrors(true);
		connection->Cancel();
		connection->Disconnect();
		delete connection;
	}
	return terminated;
}

void WebDownloader::FreeConnection()
{
	if (m_connection)
	{
		debug("Releasing connection");
		m_connectionMutex.Lock();
		if (m_connection->GetStatus() == Connection::csCancelled)
		{
			m_connection->Disconnect();
		}
		delete m_connection;
		m_connection = NULL;
		m_connectionMutex.Unlock();
	}
}
