/*
 *  This file is part of nzbget
 *
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
#include "XmlRpc.h"
#include "Log.h"
#include "Options.h"
#include "Scanner.h"
#include "FeedCoordinator.h"
#include "ServerPool.h"
#include "Util.h"
#include "Maintenance.h"
#include "StatMeter.h"
#include "ArticleWriter.h"
#include "DiskState.h"
#include "ScriptConfig.h"
#include "QueueScript.h"

extern void ExitProc();
extern void Reload();

class ErrorXmlCommand: public XmlCommand
{
private:
	int					m_errCode;
	const char*			m_errText;

public:
						ErrorXmlCommand(int errCode, const char* errText);
	virtual void		Execute();
};

class PauseUnpauseXmlCommand: public XmlCommand
{
public:
	enum EPauseAction
	{
		paDownload,
		paPostProcess,
		paScan
	};

private:
	bool				m_pause;
	EPauseAction		m_pauseAction;

public:
						PauseUnpauseXmlCommand(bool pause, EPauseAction pauseAction);
	virtual void		Execute();
};

class ScheduleResumeXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ShutdownXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ReloadXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class VersionXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class DumpDebugXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class SetDownloadRateXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class StatusXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class LogXmlCommand: public XmlCommand
{
protected:
	int						m_idFrom;
	int						m_nrEntries;
	virtual MessageList*	LockMessages();
	virtual void			UnlockMessages();
public:
	virtual void			Execute();
};

class NzbInfoXmlCommand: public XmlCommand
{
protected:
	void				AppendNzbInfoFields(NzbInfo* nzbInfo);
	void				AppendPostInfoFields(PostInfo* postInfo, int logEntries, bool postQueue);
};

class ListFilesXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ListGroupsXmlCommand: public NzbInfoXmlCommand
{
private:
	const char*			DetectStatus(NzbInfo* nzbInfo);
public:
	virtual void		Execute();
};

class EditQueueXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class DownloadXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class PostQueueXmlCommand: public NzbInfoXmlCommand
{
public:
	virtual void		Execute();
};

class WriteLogXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ClearLogXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ScanXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class HistoryXmlCommand: public NzbInfoXmlCommand
{
private:
	const char*			DetectStatus(HistoryInfo* historyInfo);
public:
	virtual void		Execute();
};

class UrlQueueXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ConfigXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class LoadConfigXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class SaveConfigXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ConfigTemplatesXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ViewFeedXmlCommand: public XmlCommand
{
private:
	bool				m_preview;

public:
						ViewFeedXmlCommand(bool preview);
	virtual void		Execute();
};

class FetchFeedXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class EditServerXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ReadUrlXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class CheckUpdatesXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class StartUpdateXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class LogUpdateXmlCommand: public LogXmlCommand
{
protected:
	virtual MessageList*	LockMessages();
	virtual void			UnlockMessages();
};

class ServerVolumesXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class ResetServerVolumeXmlCommand: public XmlCommand
{
public:
	virtual void		Execute();
};

class LoadLogXmlCommand: public LogXmlCommand
{
private:
	MessageList				m_messages;
	int						m_nzbId;
	NzbInfo*				m_nzbInfo;
protected:
	virtual void			Execute();
	virtual MessageList*	LockMessages();
	virtual void			UnlockMessages();
};

class TestServerXmlCommand: public XmlCommand
{
private:
	char*				m_errText;

	class TestConnection : public NntpConnection
	{
	protected:
		TestServerXmlCommand* m_owner;
		virtual void	PrintError(const char* errMsg) { m_owner->PrintError(errMsg); }
	public:
						TestConnection(NewsServer* newsServer, TestServerXmlCommand* owner):
							NntpConnection(newsServer), m_owner(owner) {}
	};

	void				PrintError(const char* errMsg);
public:
	virtual void		Execute();
};


//*****************************************************************
// XmlRpcProcessor

XmlRpcProcessor::XmlRpcProcessor()
{
	m_request = NULL;
	m_protocol = rpUndefined;
	m_httpMethod = hmPost;
	m_contentType = NULL;
}

void XmlRpcProcessor::SetUrl(const char* url)
{
	m_url = url;
	WebUtil::UrlDecode((char*)m_url);
}

bool XmlRpcProcessor::IsRpcRequest(const char* url)
{
	return !strcmp(url, "/xmlrpc") || !strncmp(url, "/xmlrpc/", 8) ||
		!strcmp(url, "/jsonrpc") || !strncmp(url, "/jsonrpc/", 9) ||
		!strcmp(url, "/jsonprpc") || !strncmp(url, "/jsonprpc/", 10);
}

void XmlRpcProcessor::Execute()
{
	m_protocol = rpUndefined;
	if (!strcmp(m_url, "/xmlrpc") || !strncmp(m_url, "/xmlrpc/", 8))
	{
		m_protocol = XmlRpcProcessor::rpXmlRpc;
	}
	else if (!strcmp(m_url, "/jsonrpc") || !strncmp(m_url, "/jsonrpc/", 9))
	{
		m_protocol = rpJsonRpc;
	}
	else if (!strcmp(m_url, "/jsonprpc") || !strncmp(m_url, "/jsonprpc/", 10))
	{
		m_protocol = rpJsonPRpc;
	}
	else
	{
		error("internal error: invalid rpc-request: %s", *m_url);
		return;
	}

	Dispatch();
}

void XmlRpcProcessor::Dispatch()
{
	char* request = m_request;

	char methodName[100];
	methodName[0] = '\0';

	char requestId[100];
	requestId[0] = '\0';

	if (m_httpMethod == hmGet)
	{
		request = (char*)m_url + 1;
		char* pstart = strchr(request, '/');
		if (pstart)
		{
			char* pend = strchr(pstart + 1, '?');
			if (pend)
			{
				int len = (int)(pend - pstart - 1 < (int)sizeof(methodName) - 1 ? pend - pstart - 1 : (int)sizeof(methodName) - 1);
				len = len >= sizeof(methodName) ? sizeof(methodName) - 1 : len;
				strncpy(methodName, pstart + 1, len);
				methodName[len] = '\0';
				request = pend + 1;
			}
			else
			{
				strncpy(methodName, pstart + 1, sizeof(methodName));
				methodName[sizeof(methodName) - 1] = '\0';
				request = request + strlen(request);
			}
		}
	}
	else if (m_protocol == rpXmlRpc)
	{
		WebUtil::XmlParseTagValue(m_request, "methodName", methodName, sizeof(methodName), NULL);
	}
	else if (m_protocol == rpJsonRpc)
	{
		int valueLen = 0;
		if (const char* methodPtr = WebUtil::JsonFindField(m_request, "method", &valueLen))
		{
			valueLen = valueLen >= sizeof(methodName) ? sizeof(methodName) - 1 : valueLen;
			strncpy(methodName, methodPtr + 1, valueLen - 2);
			methodName[valueLen - 2] = '\0';
		}
		if (const char* requestIdPtr = WebUtil::JsonFindField(m_request, "id", &valueLen))
		{
			valueLen = valueLen >= sizeof(requestId) ? sizeof(requestId) - 1 : valueLen;
			strncpy(requestId, requestIdPtr, valueLen);
			requestId[valueLen] = '\0';
		}
	}

	debug("MethodName=%s", methodName);

	if (!strcasecmp(methodName, "system.multicall") && m_protocol == rpXmlRpc && m_httpMethod == hmPost)
	{
		MutliCall();
	}
	else
	{
		XmlCommand* command = CreateCommand(methodName);
		command->SetRequest(request);
		command->SetProtocol(m_protocol);
		command->SetHttpMethod(m_httpMethod);
		command->SetUserAccess(m_userAccess);
		command->PrepareParams();
		command->Execute();
		BuildResponse(command->GetResponse(), command->GetCallbackFunc(), command->GetFault(), requestId);
		delete command;
	}
}

void XmlRpcProcessor::MutliCall()
{
	bool error = false;
	CString response;

	response.Append("<array><data>");

	char* requestPtr = m_request;
	char* callEnd = strstr(requestPtr, "</struct>");
	while (callEnd)
	{
		*callEnd = '\0';
		debug("MutliCall, request=%s", requestPtr);
		char* nameEnd = strstr(requestPtr, "</name>");
		if (!nameEnd)
		{
			error = true;
			break;
		}

		char methodName[100];
		methodName[0] = '\0';
		WebUtil::XmlParseTagValue(nameEnd, "string", methodName, sizeof(methodName), NULL);
		debug("MutliCall, MethodName=%s", methodName);

		XmlCommand* command = CreateCommand(methodName);
		command->SetRequest(requestPtr);
		command->Execute();

		debug("MutliCall, Response=%s", command->GetResponse());

		bool fault = !strncmp(command->GetResponse(), "<fault>", 7);
		bool array = !fault && !strncmp(command->GetResponse(), "<array>", 7);
		if (!fault && !array)
		{
			response.Append("<array><data>");
		}
		response.Append("<value>");
		response.Append(command->GetResponse());
		response.Append("</value>");
		if (!fault && !array)
		{
			response.Append("</data></array>");
		}

		delete command;

		requestPtr = callEnd + 9; //strlen("</struct>")
		callEnd = strstr(requestPtr, "</struct>");
	}

	if (error)
	{
		XmlCommand* command = new ErrorXmlCommand(4, "Parse error");
		command->SetRequest(m_request);
		command->SetProtocol(rpXmlRpc);
		command->PrepareParams();
		command->Execute();
		BuildResponse(command->GetResponse(), "", command->GetFault(), NULL);
		delete command;
	}
	else
	{
		response.Append("</data></array>");
		BuildResponse(response, "", false, NULL);
	}
}

void XmlRpcProcessor::BuildResponse(const char* response, const char* callbackFunc,
	bool fault, const char* requestId)
{
	const char XML_HEADER[] = "<?xml version=\"1.0\"?>\n<methodResponse>\n";
	const char XML_FOOTER[] = "</methodResponse>";
	const char XML_OK_OPEN[] = "<params><param><value>";
	const char XML_OK_CLOSE[] = "</value></param></params>\n";
	const char XML_FAULT_OPEN[] = "<fault><value>";
	const char XML_FAULT_CLOSE[] = "</value></fault>\n";

	const char JSON_HEADER[] = "{\n\"version\" : \"1.1\",\n";
	const char JSON_ID_OPEN[] = "\"id\" : ";
	const char JSON_ID_CLOSE[] = ",\n";
	const char JSON_FOOTER[] = "\n}";
	const char JSON_OK_OPEN[] = "\"result\" : ";
	const char JSON_OK_CLOSE[] = "";
	const char JSON_FAULT_OPEN[] = "\"error\" : ";
	const char JSON_FAULT_CLOSE[] = "";

	const char JSONP_CALLBACK_HEADER[] = "(";
	const char JSONP_CALLBACK_FOOTER[] = ")";

	bool xmlRpc = m_protocol == rpXmlRpc;

	const char* callbackHeader = m_protocol == rpJsonPRpc ? JSONP_CALLBACK_HEADER : "";
	const char* header = xmlRpc ? XML_HEADER : JSON_HEADER;
	const char* footer = xmlRpc ? XML_FOOTER : JSON_FOOTER;
	const char* openTag = fault ? (xmlRpc ? XML_FAULT_OPEN : JSON_FAULT_OPEN) : (xmlRpc ? XML_OK_OPEN : JSON_OK_OPEN);
	const char* closeTag = fault ? (xmlRpc ? XML_FAULT_CLOSE : JSON_FAULT_CLOSE ) : (xmlRpc ? XML_OK_CLOSE : JSON_OK_CLOSE);
	const char* callbackFooter = m_protocol == rpJsonPRpc ? JSONP_CALLBACK_FOOTER : "";

	debug("Response=%s", response);

	if (callbackFunc)
	{
		m_response.Append(callbackFunc);
	}
	m_response.Append(callbackHeader);
	m_response.Append(header);
	if (!xmlRpc && requestId && *requestId)
	{
		m_response.Append(JSON_ID_OPEN);
		m_response.Append(requestId);
		m_response.Append(JSON_ID_CLOSE);
	}
	m_response.Append(openTag);
	m_response.Append(response);
	m_response.Append(closeTag);
	m_response.Append(footer);
	m_response.Append(callbackFooter);

	m_contentType = xmlRpc ? "text/xml" : "application/json";
}

XmlCommand* XmlRpcProcessor::CreateCommand(const char* methodName)
{
	XmlCommand* command = NULL;

	if (m_userAccess == uaAdd &&
		!(!strcasecmp(methodName, "append") || !strcasecmp(methodName, "appendurl") ||
		 !strcasecmp(methodName, "version")))
	{
		command = new ErrorXmlCommand(401, "Access denied");
		warn("Received request \"%s\" from add-user, access denied", methodName);
	}
	else if (m_userAccess == uaRestricted && !strcasecmp(methodName, "saveconfig"))
	{
		command = new ErrorXmlCommand(401, "Access denied");
		warn("Received request \"%s\" from restricted user, access denied", methodName);
	}
	else if (!strcasecmp(methodName, "pause") || !strcasecmp(methodName, "pausedownload") ||
		!strcasecmp(methodName, "pausedownload2"))
	{
		command = new PauseUnpauseXmlCommand(true, PauseUnpauseXmlCommand::paDownload);
	}
	else if (!strcasecmp(methodName, "resume") || !strcasecmp(methodName, "resumedownload") ||
		!strcasecmp(methodName, "resumedownload2"))
	{
		command = new PauseUnpauseXmlCommand(false, PauseUnpauseXmlCommand::paDownload);
	}
	else if (!strcasecmp(methodName, "shutdown"))
	{
		command = new ShutdownXmlCommand();
	}
	else if (!strcasecmp(methodName, "reload"))
	{
		command = new ReloadXmlCommand();
	}
	else if (!strcasecmp(methodName, "version"))
	{
		command = new VersionXmlCommand();
	}
	else if (!strcasecmp(methodName, "dump"))
	{
		command = new DumpDebugXmlCommand();
	}
	else if (!strcasecmp(methodName, "rate"))
	{
		command = new SetDownloadRateXmlCommand();
	}
	else if (!strcasecmp(methodName, "status"))
	{
		command = new StatusXmlCommand();
	}
	else if (!strcasecmp(methodName, "log"))
	{
		command = new LogXmlCommand();
	}
	else if (!strcasecmp(methodName, "listfiles"))
	{
		command = new ListFilesXmlCommand();
	}
	else if (!strcasecmp(methodName, "listgroups"))
	{
		command = new ListGroupsXmlCommand();
	}
	else if (!strcasecmp(methodName, "editqueue"))
	{
		command = new EditQueueXmlCommand();
	}
	else if (!strcasecmp(methodName, "append") || !strcasecmp(methodName, "appendurl"))
	{
		command = new DownloadXmlCommand();
	}
	else if (!strcasecmp(methodName, "postqueue"))
	{
		command = new PostQueueXmlCommand();
	}
	else if (!strcasecmp(methodName, "writelog"))
	{
		command = new WriteLogXmlCommand();
	}
	else if (!strcasecmp(methodName, "clearlog"))
	{
		command = new ClearLogXmlCommand();
	}
	else if (!strcasecmp(methodName, "loadlog"))
	{
		command = new LoadLogXmlCommand();
	}
	else if (!strcasecmp(methodName, "scan"))
	{
		command = new ScanXmlCommand();
	}
	else if (!strcasecmp(methodName, "pausepost"))
	{
		command = new PauseUnpauseXmlCommand(true, PauseUnpauseXmlCommand::paPostProcess);
	}
	else if (!strcasecmp(methodName, "resumepost"))
	{
		command = new PauseUnpauseXmlCommand(false, PauseUnpauseXmlCommand::paPostProcess);
	}
	else if (!strcasecmp(methodName, "pausescan"))
	{
		command = new PauseUnpauseXmlCommand(true, PauseUnpauseXmlCommand::paScan);
	}
	else if (!strcasecmp(methodName, "resumescan"))
	{
		command = new PauseUnpauseXmlCommand(false, PauseUnpauseXmlCommand::paScan);
	}
	else if (!strcasecmp(methodName, "scheduleresume"))
	{
		command = new ScheduleResumeXmlCommand();
	}
	else if (!strcasecmp(methodName, "history"))
	{
		command = new HistoryXmlCommand();
	}
	else if (!strcasecmp(methodName, "urlqueue"))
	{
		command = new UrlQueueXmlCommand();
	}
	else if (!strcasecmp(methodName, "config"))
	{
		command = new ConfigXmlCommand();
	}
	else if (!strcasecmp(methodName, "loadconfig"))
	{
		command = new LoadConfigXmlCommand();
	}
	else if (!strcasecmp(methodName, "saveconfig"))
	{
		command = new SaveConfigXmlCommand();
	}
	else if (!strcasecmp(methodName, "configtemplates"))
	{
		command = new ConfigTemplatesXmlCommand();
	}
	else if (!strcasecmp(methodName, "viewfeed"))
	{
		command = new ViewFeedXmlCommand(false);
	}
	else if (!strcasecmp(methodName, "previewfeed"))
	{
		command = new ViewFeedXmlCommand(true);
	}
	else if (!strcasecmp(methodName, "fetchfeed"))
	{
		command = new FetchFeedXmlCommand();
	}
	else if (!strcasecmp(methodName, "editserver"))
	{
		command = new EditServerXmlCommand();
	}
	else if (!strcasecmp(methodName, "readurl"))
	{
		command = new ReadUrlXmlCommand();
	}
	else if (!strcasecmp(methodName, "checkupdates"))
	{
		command = new CheckUpdatesXmlCommand();
	}
	else if (!strcasecmp(methodName, "startupdate"))
	{
		command = new StartUpdateXmlCommand();
	}
	else if (!strcasecmp(methodName, "logupdate"))
	{
		command = new LogUpdateXmlCommand();
	}
	else if (!strcasecmp(methodName, "servervolumes"))
	{
		command = new ServerVolumesXmlCommand();
	}
	else if (!strcasecmp(methodName, "resetservervolume"))
	{
		command = new ResetServerVolumeXmlCommand();
	}
	else if (!strcasecmp(methodName, "testserver"))
	{
		command = new TestServerXmlCommand();
	}
	else
	{
		command = new ErrorXmlCommand(1, "Invalid procedure");
	}

	return command;
}


//*****************************************************************
// Base command

XmlCommand::XmlCommand()
{
	m_request = NULL;
	m_requestPtr = NULL;
	m_callbackFunc = NULL;
	m_fault = false;
	m_protocol = XmlRpcProcessor::rpUndefined;
	m_response.Reserve(1024 * 10);
}

bool XmlCommand::IsJson()
{
	return m_protocol == XmlRpcProcessor::rpJsonRpc || m_protocol == XmlRpcProcessor::rpJsonPRpc;
}

void XmlCommand::AppendResponse(const char* part)
{
	m_response.Append(part);
}

void XmlCommand::AppendFmtResponse(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	m_response.AppendFmtV(format, args);
	va_end(args);
}

void XmlCommand::AppendCondResponse(const char* part, bool cond)
{
	if (cond)
	{
		m_response.Append(part);
	}
}

void XmlCommand::BuildErrorResponse(int errCode, const char* errText, ...)
{
	const char* XML_RESPONSE_ERROR_BODY =
		"<struct>\n"
		"<member><name>faultCode</name><value><i4>%i</i4></value></member>\n"
		"<member><name>faultString</name><value><string>%s</string></value></member>\n"
		"</struct>\n";

	const char* JSON_RESPONSE_ERROR_BODY =
		"{\n"
		"\"name\" : \"JSONRPCError\",\n"
		"\"code\" : %i,\n"
		"\"message\" : \"%s\"\n"
		"}";

	char fullText[1024];

	va_list ap;
	va_start(ap, errText);
	vsnprintf(fullText, 1024, errText, ap);
	fullText[1024-1] = '\0';
	va_end(ap);

	char* xmlText = EncodeStr(fullText);

	char content[1024];
	snprintf(content, 1024, IsJson() ? JSON_RESPONSE_ERROR_BODY : XML_RESPONSE_ERROR_BODY, errCode, xmlText);
	content[1024-1] = '\0';

	free(xmlText);

	AppendResponse(content);

	m_fault = true;
}

void XmlCommand::BuildBoolResponse(bool ok)
{
	const char* XML_RESPONSE_BOOL_BODY = "<boolean>%s</boolean>";
	const char* JSON_RESPONSE_BOOL_BODY = "%s";

	char content[1024];
	snprintf(content, 1024, IsJson() ? JSON_RESPONSE_BOOL_BODY : XML_RESPONSE_BOOL_BODY,
		BoolToStr(ok));
	content[1024-1] = '\0';

	AppendResponse(content);
}

void XmlCommand::BuildIntResponse(int value)
{
	const char* XML_RESPONSE_INT_BODY = "<i4>%i</i4>";
	const char* JSON_RESPONSE_INT_BODY = "%i";

	char content[1024];
	snprintf(content, 1024, IsJson() ? JSON_RESPONSE_INT_BODY : XML_RESPONSE_INT_BODY, value);
	content[1024-1] = '\0';

	AppendResponse(content);
}

void XmlCommand::PrepareParams()
{
	if (IsJson() && m_httpMethod == XmlRpcProcessor::hmPost)
	{
		char* params = strstr(m_requestPtr, "\"params\"");
		if (!params)
		{
			m_requestPtr[0] = '\0';
			return;
		}
		m_requestPtr = params + 8; // strlen("\"params\"")
	}

	if (m_protocol == XmlRpcProcessor::rpJsonPRpc)
	{
		NextParamAsStr(&m_callbackFunc);
	}
}

char* XmlCommand::XmlNextValue(char* xml, const char* tag, int* valueLength)
{
	int valueLen;
	const char* value = WebUtil::XmlFindTag(xml, "value", &valueLen);
	if (value)
	{
		char* tagContent = (char*)WebUtil::XmlFindTag(value, tag, valueLength);
		if (tagContent <= value + valueLen)
		{
			return tagContent;
		}
	}
	return NULL;
}

bool XmlCommand::NextParamAsInt(int* value)
{
	if (m_httpMethod == XmlRpcProcessor::hmGet)
	{
		char* param = strchr(m_requestPtr, '=');
		if (!param)
		{
			return false;
		}
		*value = atoi(param + 1);
		m_requestPtr = param + 1;
		while (strchr("-+0123456789&", *m_requestPtr))
		{
			m_requestPtr++;
		}
		return true;
	}
	else if (IsJson())
	{
		int len = 0;
		char* param = (char*)WebUtil::JsonNextValue(m_requestPtr, &len);
		if (!param || !strchr("-+0123456789", *param))
		{
			return false;
		}
		*value = atoi(param);
		m_requestPtr = param + len + 1;
		return true;
	}
	else
	{
		int len = 0;
		int tagLen = 4; //strlen("<i4>");
		char* param = XmlNextValue(m_requestPtr, "i4", &len);
		if (!param)
		{
			param = XmlNextValue(m_requestPtr, "int", &len);
			tagLen = 5; //strlen("<int>");
		}
		if (!param || !strchr("-+0123456789", *param))
		{
			return false;
		}
		*value = atoi(param);
		m_requestPtr = param + len + tagLen;
		return true;
	}
}

bool XmlCommand::NextParamAsBool(bool* value)
{
	if (m_httpMethod == XmlRpcProcessor::hmGet)
	{
		char* param;
		if (!NextParamAsStr(&param))
		{
			return false;
		}

		if (IsJson())
		{
			if (!strncmp(param, "true", 4))
			{
				*value = true;
				return true;
			}
			else if (!strncmp(param, "false", 5))
			{
				*value = false;
				return true;
			}
		}
		else
		{
			*value = param[0] == '1';
			return true;
		}
		return false;
	}
	else if (IsJson())
	{
		int len = 0;
		char* param = (char*)WebUtil::JsonNextValue(m_requestPtr, &len);
		if (!param)
		{
			return false;
		}
		if (len == 4 && !strncmp(param, "true", 4))
		{
			*value = true;
			m_requestPtr = param + len + 1;
			return true;
		}
		else if (len == 5 && !strncmp(param, "false", 5))
		{
			*value = false;
			m_requestPtr = param + len + 1;
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		int len = 0;
		char* param = XmlNextValue(m_requestPtr, "boolean", &len);
		if (!param)
		{
			return false;
		}
		*value = param[0] == '1';
		m_requestPtr = param + len + 9; //strlen("<boolean>");
		return true;
	}
}

bool XmlCommand::NextParamAsStr(char** value)
{
	if (m_httpMethod == XmlRpcProcessor::hmGet)
	{
		char* param = strchr(m_requestPtr, '=');
		if (!param)
		{
			return false;
		}
		param++; // skip '='
		int len = 0;
		char* paramEnd = strchr(param, '&');
		if (paramEnd)
		{
			len = (int)(paramEnd - param);
			param[len] = '\0';
		}
		else
		{
			len = strlen(param) - 1;
		}
		m_requestPtr = param + len + 1;
		*value = param;
		return true;
	}
	else if (IsJson())
	{
		int len = 0;
		char* param = (char*)WebUtil::JsonNextValue(m_requestPtr, &len);
		if (!param || len < 2 || param[0] != '"' || param[len - 1] != '"')
		{
			return false;
		}
		param++; // skip first '"'
		param[len - 2] = '\0'; // skip last '"'
		m_requestPtr = param + len;
		*value = param;
		return true;
	}
	else
	{
		int len = 0;
		char* param = XmlNextValue(m_requestPtr, "string", &len);
		if (!param)
		{
			return false;
		}
		param[len] = '\0';
		m_requestPtr = param + len + 8; //strlen("<string>")
		*value = param;
		return true;
	}
}

const char* XmlCommand::BoolToStr(bool value)
{
	return IsJson() ? (value ? "true" : "false") : (value ? "1" : "0");
}

char* XmlCommand::EncodeStr(const char* str)
{
	if (!str)
	{
		return strdup("");
	}

	if (IsJson())
	{
		return WebUtil::JsonEncode(str);
	}
	else
	{
		return WebUtil::XmlEncode(str);
	}
}

void XmlCommand::DecodeStr(char* str)
{
	if (IsJson())
	{
		WebUtil::JsonDecode(str);
	}
	else
	{
		WebUtil::XmlDecode(str);
	}
}

bool XmlCommand::CheckSafeMethod()
{
	bool safe = m_httpMethod == XmlRpcProcessor::hmPost || m_protocol == XmlRpcProcessor::rpJsonPRpc;
	if (!safe)
	{
		BuildErrorResponse(4, "Not safe procedure for HTTP-Method GET. Use Method POST instead");
	}
	return safe;
}

//*****************************************************************
// Commands

ErrorXmlCommand::ErrorXmlCommand(int errCode, const char* errText)
{
	m_errCode = errCode;
	m_errText = errText;
}

void ErrorXmlCommand::Execute()
{
	BuildErrorResponse(m_errCode, m_errText);
}

PauseUnpauseXmlCommand::PauseUnpauseXmlCommand(bool pause, EPauseAction pauseAction)
{
	m_pause = pause;
	m_pauseAction = pauseAction;
}

void PauseUnpauseXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	bool ok = true;

	g_Options->SetResumeTime(0);

	switch (m_pauseAction)
	{
		case paDownload:
			g_Options->SetPauseDownload(m_pause);
			break;

		case paPostProcess:
			g_Options->SetPausePostProcess(m_pause);
			break;

		case paScan:
			g_Options->SetPauseScan(m_pause);
			break;

		default:
			ok = false;
	}

	BuildBoolResponse(ok);
}

// bool scheduleresume(int Seconds)
void ScheduleResumeXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	int seconds = 0;
	if (!NextParamAsInt(&seconds) || seconds < 0)
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	time_t curTime = time(NULL);

	g_Options->SetResumeTime(curTime + seconds);

	BuildBoolResponse(true);
}

void ShutdownXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	BuildBoolResponse(true);
	ExitProc();
}

void ReloadXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	BuildBoolResponse(true);
	Reload();
}

void VersionXmlCommand::Execute()
{
	const char* XML_RESPONSE_STRING_BODY = "<string>%s</string>";
	const char* JSON_RESPONSE_STRING_BODY = "\"%s\"";

	char content[1024];
	snprintf(content, 1024, IsJson() ? JSON_RESPONSE_STRING_BODY : XML_RESPONSE_STRING_BODY, Util::VersionRevision());
	content[1024-1] = '\0';

	AppendResponse(content);
}

void DumpDebugXmlCommand::Execute()
{
	g_Log->LogDebugInfo();
	BuildBoolResponse(true);
}

void SetDownloadRateXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	int rate = 0;
	if (!NextParamAsInt(&rate) || rate < 0)
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	g_Options->SetDownloadRate(rate * 1024);
	BuildBoolResponse(true);
}

void StatusXmlCommand::Execute()
{
	const char* XML_STATUS_START =
		"<struct>\n"
		"<member><name>RemainingSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>RemainingSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>RemainingSizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ForcedSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>ForcedSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>ForcedSizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DownloadedSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>DownloadedSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>DownloadedSizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ArticleCacheLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>ArticleCacheHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>ArticleCacheMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DownloadRate</name><value><i4>%i</i4></value></member>\n"
		"<member><name>AverageDownloadRate</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DownloadLimit</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ThreadCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ParJobCount</name><value><i4>%i</i4></value></member>\n"					// deprecated (renamed to PostJobCount)
		"<member><name>PostJobCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>UrlCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>UpTimeSec</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DownloadTimeSec</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ServerPaused</name><value><boolean>%s</boolean></value></member>\n"		// deprecated (renamed to DownloadPaused)
		"<member><name>DownloadPaused</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>Download2Paused</name><value><boolean>%s</boolean></value></member>\n"	// deprecated (same as DownloadPaused)
		"<member><name>ServerStandBy</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>PostPaused</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>ScanPaused</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>FreeDiskSpaceLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>FreeDiskSpaceHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>FreeDiskSpaceMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ServerTime</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ResumeTime</name><value><i4>%i</i4></value></member>\n"
		"<member><name>FeedActive</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>QueueScriptCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>NewsServers</name><value><array><data>\n";

	const char* XML_STATUS_END =
		"</data></array></value></member>\n"
		"</struct>\n";

	const char* JSON_STATUS_START =
		"{\n"
		"\"RemainingSizeLo\" : %u,\n"
		"\"RemainingSizeHi\" : %u,\n"
		"\"RemainingSizeMB\" : %i,\n"
		"\"ForcedSizeLo\" : %u,\n"
		"\"ForcedSizeHi\" : %u,\n"
		"\"ForcedSizeMB\" : %i,\n"
		"\"DownloadedSizeLo\" : %u,\n"
		"\"DownloadedSizeHi\" : %u,\n"
		"\"DownloadedSizeMB\" : %i,\n"
		"\"ArticleCacheLo\" : %u,\n"
		"\"ArticleCacheHi\" : %u,\n"
		"\"ArticleCacheMB\" : %i,\n"
		"\"DownloadRate\" : %i,\n"
		"\"AverageDownloadRate\" : %i,\n"
		"\"DownloadLimit\" : %i,\n"
		"\"ThreadCount\" : %i,\n"
		"\"ParJobCount\" : %i,\n"			// deprecated (renamed to PostJobCount)
		"\"PostJobCount\" : %i,\n"
		"\"UrlCount\" : %i,\n"
		"\"UpTimeSec\" : %i,\n"
		"\"DownloadTimeSec\" : %i,\n"
		"\"ServerPaused\" : %s,\n"			// deprecated (renamed to DownloadPaused)
		"\"DownloadPaused\" : %s,\n"
		"\"Download2Paused\" : %s,\n"		// deprecated (same as DownloadPaused)
		"\"ServerStandBy\" : %s,\n"
		"\"PostPaused\" : %s,\n"
		"\"ScanPaused\" : %s,\n"
		"\"FreeDiskSpaceLo\" : %u,\n"
		"\"FreeDiskSpaceHi\" : %u,\n"
		"\"FreeDiskSpaceMB\" : %i,\n"
		"\"ServerTime\" : %i,\n"
		"\"ResumeTime\" : %i,\n"
		"\"FeedActive\" : %s,\n"
		"\"QueueScriptCount\" : %i,\n"
		"\"NewsServers\" : [\n";

	const char* JSON_STATUS_END =
		"]\n"
		"}";

	const char* XML_NEWSSERVER_ITEM =
		"<value><struct>\n"
		"<member><name>ID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Active</name><value><boolean>%s</boolean></value></member>\n"
		"</struct></value>\n";

	const char* JSON_NEWSSERVER_ITEM =
		"{\n"
		"\"ID\" : %i,\n"
		"\"Active\" : %s\n"
		"}";

	DownloadQueue* downloadQueue = DownloadQueue::Lock();
	int postJobCount = 0;
	int urlCount = 0;
	for (NzbList::iterator it = downloadQueue->GetQueue()->begin(); it != downloadQueue->GetQueue()->end(); it++)
	{
		NzbInfo* nzbInfo = *it;
		postJobCount += nzbInfo->GetPostInfo() ? 1 : 0;
		urlCount += nzbInfo->GetKind() == NzbInfo::nkUrl ? 1 : 0;
	}
	int64 remainingSize, forcedSize;
	downloadQueue->CalcRemainingSize(&remainingSize, &forcedSize);
	DownloadQueue::Unlock();

	uint32 remainingSizeHi, remainingSizeLo;
	Util::SplitInt64(remainingSize, &remainingSizeHi, &remainingSizeLo);
	int remainingMBytes = (int)(remainingSize / 1024 / 1024);

	uint32 forcedSizeHi, forcedSizeLo;
	Util::SplitInt64(forcedSize, &forcedSizeHi, &forcedSizeLo);
	int forcedMBytes = (int)(forcedSize / 1024 / 1024);

	int64 articleCache = g_ArticleCache->GetAllocated();
	uint32 articleCacheHi, articleCacheLo;
	Util::SplitInt64(articleCache, &articleCacheHi, &articleCacheLo);
	int articleCacheMBytes = (int)(articleCache / 1024 / 1024);

	int downloadRate = (int)(g_StatMeter->CalcCurrentDownloadSpeed());
	int downloadLimit = (int)(g_Options->GetDownloadRate());
	bool downloadPaused = g_Options->GetPauseDownload();
	bool postPaused = g_Options->GetPausePostProcess();
	bool scanPaused = g_Options->GetPauseScan();
	int threadCount = Thread::GetThreadCount() - 1; // not counting itself

	uint32 downloadedSizeHi, downloadedSizeLo;
	int upTimeSec, downloadTimeSec;
	int64 allBytes;
	bool serverStandBy;
	g_StatMeter->CalcTotalStat(&upTimeSec, &downloadTimeSec, &allBytes, &serverStandBy);
	int downloadedMBytes = (int)(allBytes / 1024 / 1024);
	Util::SplitInt64(allBytes, &downloadedSizeHi, &downloadedSizeLo);
	int averageDownloadRate = (int)(downloadTimeSec > 0 ? allBytes / downloadTimeSec : 0);
	uint32 freeDiskSpaceHi, freeDiskSpaceLo;
	int64 freeDiskSpace = Util::FreeDiskSize(g_Options->GetDestDir());
	Util::SplitInt64(freeDiskSpace, &freeDiskSpaceHi, &freeDiskSpaceLo);
	int freeDiskSpaceMB = (int)(freeDiskSpace / 1024 / 1024);
	int serverTime = time(NULL);
	int resumeTime = g_Options->GetResumeTime();
	bool feedActive = g_FeedCoordinator->HasActiveDownloads();
	int queuedScripts = g_QueueScriptCoordinator->GetQueueSize();

	AppendFmtResponse(IsJson() ? JSON_STATUS_START : XML_STATUS_START,
		remainingSizeLo, remainingSizeHi, remainingMBytes, forcedSizeLo,
		forcedSizeHi, forcedMBytes, downloadedSizeLo, downloadedSizeHi,
		downloadedMBytes, articleCacheLo, articleCacheHi, articleCacheMBytes,
		downloadRate, averageDownloadRate, downloadLimit, threadCount,
		postJobCount, postJobCount, urlCount, upTimeSec, downloadTimeSec,
		BoolToStr(downloadPaused), BoolToStr(downloadPaused), BoolToStr(downloadPaused),
		BoolToStr(serverStandBy), BoolToStr(postPaused), BoolToStr(scanPaused),
		freeDiskSpaceLo, freeDiskSpaceHi,	freeDiskSpaceMB, serverTime, resumeTime,
		BoolToStr(feedActive), queuedScripts);

	int index = 0;
	for (Servers::iterator it = g_ServerPool->GetServers()->begin(); it != g_ServerPool->GetServers()->end(); it++)
	{
		NewsServer* server = *it;

		AppendCondResponse(",\n", IsJson() && index++ > 0);
		AppendFmtResponse(IsJson() ? JSON_NEWSSERVER_ITEM : XML_NEWSSERVER_ITEM,
			server->GetId(), BoolToStr(server->GetActive()));
	}

	AppendResponse(IsJson() ? JSON_STATUS_END : XML_STATUS_END);
}

// struct[] log(idfrom, entries)
void LogXmlCommand::Execute()
{
	m_idFrom = 0;
	m_nrEntries = 0;
	if (!NextParamAsInt(&m_idFrom) || !NextParamAsInt(&m_nrEntries) || (m_nrEntries > 0 && m_idFrom > 0))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	debug("iIDFrom=%i", m_idFrom);
	debug("iNrEntries=%i", m_nrEntries);

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");
	MessageList* messages = LockMessages();

	int start = messages->size();
	if (m_nrEntries > 0)
	{
		if (m_nrEntries > (int)messages->size())
		{
			m_nrEntries = messages->size();
		}
		start = messages->size() - m_nrEntries;
	}
	if (m_idFrom > 0 && !messages->empty())
	{
		m_nrEntries = messages->size();
		start = m_idFrom - messages->front()->GetId();
		if (start < 0)
		{
			start = 0;
		}
	}

	const char* XML_LOG_ITEM =
		"<value><struct>\n"
		"<member><name>ID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Kind</name><value><string>%s</string></value></member>\n"
		"<member><name>Time</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Text</name><value><string>%s</string></value></member>\n"
		"</struct></value>\n";

	const char* JSON_LOG_ITEM =
		"{\n"
		"\"ID\" : %i,\n"
		"\"Kind\" : \"%s\",\n"
		"\"Time\" : %i,\n"
		"\"Text\" : \"%s\"\n"
		"}";

	const char* messageType[] = { "INFO", "WARNING", "ERROR", "DEBUG", "DETAIL" };

	int index = 0;

	for (uint32 i = (uint32)start; i < messages->size(); i++)
	{
		Message* message = (*messages)[i];

		char* xmltext = EncodeStr(message->GetText());

		AppendCondResponse(",\n", IsJson() && index++ > 0);
		AppendFmtResponse(IsJson() ? JSON_LOG_ITEM : XML_LOG_ITEM,
			message->GetId(), messageType[message->GetKind()], message->GetTime(), xmltext);

		free(xmltext);
	}

	UnlockMessages();
	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

MessageList* LogXmlCommand::LockMessages()
{
	return g_Log->LockMessages();
}

void LogXmlCommand::UnlockMessages()
{
	g_Log->UnlockMessages();
}

// struct[] listfiles(int IDFrom, int IDTo, int NZBID)
// For backward compatibility with 0.8 parameter "NZBID" is optional
void ListFilesXmlCommand::Execute()
{
	int idStart = 0;
	int idEnd = 0;
	if (NextParamAsInt(&idStart) && (!NextParamAsInt(&idEnd) || idEnd < idStart))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	// For backward compatibility with 0.8 parameter "NZBID" is optional (error checking omitted)
	int nzbId = 0;
	NextParamAsInt(&nzbId);

	if (nzbId > 0 && (idStart != 0 || idEnd != 0))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	debug("iIDStart=%i", idStart);
	debug("iIDEnd=%i", idEnd);

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");
	DownloadQueue* downloadQueue = DownloadQueue::Lock();

	const char* XML_LIST_ITEM =
		"<value><struct>\n"
		"<member><name>ID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>FileSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>FileSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>RemainingSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>RemainingSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>PostTime</name><value><i4>%i</i4></value></member>\n"
		"<member><name>FilenameConfirmed</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>Paused</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>NZBID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>NZBName</name><value><string>%s</string></value></member>\n"
		"<member><name>NZBNicename</name><value><string>%s</string></value></member>\n"	// deprecated, use "NZBName" instead
		"<member><name>NZBFilename</name><value><string>%s</string></value></member>\n"
		"<member><name>Subject</name><value><string>%s</string></value></member>\n"
		"<member><name>Filename</name><value><string>%s</string></value></member>\n"
		"<member><name>DestDir</name><value><string>%s</string></value></member>\n"
		"<member><name>Category</name><value><string>%s</string></value></member>\n"
		"<member><name>Priority</name><value><i4>%i</i4></value></member>\n"			// deprecated, use "Priority" of group instead
		"<member><name>ActiveDownloads</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Progress</name><value><i4>%u</i4></value></member>\n"
		"</struct></value>\n";

	const char* JSON_LIST_ITEM =
		"{\n"
		"\"ID\" : %i,\n"
		"\"FileSizeLo\" : %u,\n"
		"\"FileSizeHi\" : %u,\n"
		"\"RemainingSizeLo\" : %u,\n"
		"\"RemainingSizeHi\" : %u,\n"
		"\"PostTime\" : %i,\n"
		"\"FilenameConfirmed\" : %s,\n"
		"\"Paused\" : %s,\n"
		"\"NZBID\" : %i,\n"
		"\"NZBName\" : \"%s\",\n"
		"\"NZBNicename\" : \"%s\",\n" 		// deprecated, use "NZBName" instead
		"\"NZBFilename\" : \"%s\",\n"
		"\"Subject\" : \"%s\",\n"
		"\"Filename\" : \"%s\",\n"
		"\"DestDir\" : \"%s\",\n"
		"\"Category\" : \"%s\",\n"
		"\"Priority\" : %i,\n"				// deprecated, use "Priority" of group instead
		"\"ActiveDownloads\" : %i,\n"
		"\"Progress\" : %i\n"
		"}";

	int index = 0;

	for (NzbList::iterator it = downloadQueue->GetQueue()->begin(); it != downloadQueue->GetQueue()->end(); it++)
	{
		NzbInfo* nzbInfo = *it;
		for (FileList::iterator it2 = nzbInfo->GetFileList()->begin(); it2 != nzbInfo->GetFileList()->end(); it2++)
		{
			FileInfo* fileInfo = *it2;

			if ((nzbId > 0 && nzbId == fileInfo->GetNzbInfo()->GetId()) ||
				(nzbId == 0 && (idStart == 0 || (idStart <= fileInfo->GetId() && fileInfo->GetId() <= idEnd))))
			{
				uint32 fileSizeHi, fileSizeLo;
				uint32 remainingSizeLo, remainingSizeHi;
				Util::SplitInt64(fileInfo->GetSize(), &fileSizeHi, &fileSizeLo);
				Util::SplitInt64(fileInfo->GetRemainingSize(), &remainingSizeHi, &remainingSizeLo);
				char* xmlNzbFilename = EncodeStr(fileInfo->GetNzbInfo()->GetFilename());
				char* xmlSubject = EncodeStr(fileInfo->GetSubject());
				char* xmlFilename = EncodeStr(fileInfo->GetFilename());
				char* xmlDestDir = EncodeStr(fileInfo->GetNzbInfo()->GetDestDir());
				char* xmlCategory = EncodeStr(fileInfo->GetNzbInfo()->GetCategory());
				char* xmlNzbNicename = EncodeStr(fileInfo->GetNzbInfo()->GetName());

				int progress = fileInfo->GetFailedSize() == 0 && fileInfo->GetSuccessSize() == 0 ? 0 :
					(int)(1000 - fileInfo->GetRemainingSize() * 1000 / (fileInfo->GetSize() - fileInfo->GetMissedSize()));

				AppendCondResponse(",\n", IsJson() && index++ > 0);
				AppendFmtResponse(IsJson() ? JSON_LIST_ITEM : XML_LIST_ITEM,
					fileInfo->GetId(), fileSizeLo, fileSizeHi, remainingSizeLo, remainingSizeHi,
					fileInfo->GetTime(), BoolToStr(fileInfo->GetFilenameConfirmed()),
					BoolToStr(fileInfo->GetPaused()), fileInfo->GetNzbInfo()->GetId(), xmlNzbNicename,
					xmlNzbNicename, xmlNzbFilename, xmlSubject, xmlFilename, xmlDestDir, xmlCategory,
					fileInfo->GetNzbInfo()->GetPriority(), fileInfo->GetActiveDownloads(), progress);

				free(xmlNzbFilename);
				free(xmlSubject);
				free(xmlFilename);
				free(xmlDestDir);
				free(xmlCategory);
				free(xmlNzbNicename);
			}
		}
	}

	DownloadQueue::Unlock();
	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

void NzbInfoXmlCommand::AppendNzbInfoFields(NzbInfo* nzbInfo)
{
	const char* XML_NZB_ITEM_START =
		"<member><name>NZBID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>NZBName</name><value><string>%s</string></value></member>\n"
		"<member><name>NZBNicename</name><value><string>%s</string></value></member>\n"	// deprecated, use "NZBName" instead
		"<member><name>Kind</name><value><string>%s</string></value></member>\n"
		"<member><name>URL</name><value><string>%s</string></value></member>\n"
		"<member><name>NZBFilename</name><value><string>%s</string></value></member>\n"
		"<member><name>DestDir</name><value><string>%s</string></value></member>\n"
		"<member><name>FinalDir</name><value><string>%s</string></value></member>\n"
		"<member><name>Category</name><value><string>%s</string></value></member>\n"
		"<member><name>ParStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>ExParStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>UnpackStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>MoveStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>ScriptStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>DeleteStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>MarkStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>UrlStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>FileSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>FileSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>FileSizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>FileCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>MinPostTime</name><value><i4>%i</i4></value></member>\n"
		"<member><name>MaxPostTime</name><value><i4>%i</i4></value></member>\n"
		"<member><name>TotalArticles</name><value><i4>%i</i4></value></member>\n"
		"<member><name>SuccessArticles</name><value><i4>%i</i4></value></member>\n"
		"<member><name>FailedArticles</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Health</name><value><i4>%i</i4></value></member>\n"
		"<member><name>CriticalHealth</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DupeKey</name><value><string>%s</string></value></member>\n"
		"<member><name>DupeScore</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DupeMode</name><value><string>%s</string></value></member>\n"
		"<member><name>Deleted</name><value><boolean>%s</boolean></value></member>\n"	 // deprecated, use "DeleteStatus" instead
		"<member><name>DownloadedSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>DownloadedSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>DownloadedSizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DownloadTimeSec</name><value><i4>%i</i4></value></member>\n"
		"<member><name>PostTotalTimeSec</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ParTimeSec</name><value><i4>%i</i4></value></member>\n"
		"<member><name>RepairTimeSec</name><value><i4>%i</i4></value></member>\n"
		"<member><name>UnpackTimeSec</name><value><i4>%i</i4></value></member>\n"
		"<member><name>MessageCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ExtraParBlocks</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Parameters</name><value><array><data>\n";

	const char* XML_NZB_ITEM_SCRIPT_START =
		"</data></array></value></member>\n"
		"<member><name>ScriptStatuses</name><value><array><data>\n";

	const char* XML_NZB_ITEM_STATS_START =
		"</data></array></value></member>\n"
		"<member><name>ServerStats</name><value><array><data>\n";

	const char* XML_NZB_ITEM_END =
		"</data></array></value></member>\n";

	const char* JSON_NZB_ITEM_START =
		"\"NZBID\" : %i,\n"
		"\"NZBName\" : \"%s\",\n"
		"\"NZBNicename\" : \"%s\",\n"		// deprecated, use NZBName instead
		"\"Kind\" : \"%s\",\n"
		"\"URL\" : \"%s\",\n"
		"\"NZBFilename\" : \"%s\",\n"
		"\"DestDir\" : \"%s\",\n"
		"\"FinalDir\" : \"%s\",\n"
		"\"Category\" : \"%s\",\n"
		"\"ParStatus\" : \"%s\",\n"
		"\"ExParStatus\" : \"%s\",\n"
		"\"UnpackStatus\" : \"%s\",\n"
		"\"MoveStatus\" : \"%s\",\n"
		"\"ScriptStatus\" : \"%s\",\n"
		"\"DeleteStatus\" : \"%s\",\n"
		"\"MarkStatus\" : \"%s\",\n"
		"\"UrlStatus\" : \"%s\",\n"
		"\"FileSizeLo\" : %u,\n"
		"\"FileSizeHi\" : %u,\n"
		"\"FileSizeMB\" : %i,\n"
		"\"FileCount\" : %i,\n"
		"\"MinPostTime\" : %i,\n"
		"\"MaxPostTime\" : %i,\n"
		"\"TotalArticles\" : %i,\n"
		"\"SuccessArticles\" : %i,\n"
		"\"FailedArticles\" : %i,\n"
		"\"Health\" : %i,\n"
		"\"CriticalHealth\" : %i,\n"
		"\"DupeKey\" : \"%s\",\n"
		"\"DupeScore\" : %i,\n"
		"\"DupeMode\" : \"%s\",\n"
		"\"Deleted\" : %s,\n"			  // deprecated, use "DeleteStatus" instead
		"\"DownloadedSizeLo\" : %u,\n"
		"\"DownloadedSizeHi\" : %u,\n"
		"\"DownloadedSizeMB\" : %i,\n"
		"\"DownloadTimeSec\" : %i,\n"
		"\"PostTotalTimeSec\" : %i,\n"
		"\"ParTimeSec\" : %i,\n"
		"\"RepairTimeSec\" : %i,\n"
		"\"UnpackTimeSec\" : %i,\n"
		"\"MessageCount\" : %i,\n"
		"\"ExtraParBlocks\" : %i,\n"
		"\"Parameters\" : [\n";

	const char* JSON_NZB_ITEM_SCRIPT_START =
		"],\n"
		"\"ScriptStatuses\" : [\n";

	const char* JSON_NZB_ITEM_STATS_START =
		"],\n"
		"\"ServerStats\" : [\n";

	const char* JSON_NZB_ITEM_END =
		"]\n";

	const char* XML_PARAMETER_ITEM =
		"<value><struct>\n"
		"<member><name>Name</name><value><string>%s</string></value></member>\n"
		"<member><name>Value</name><value><string>%s</string></value></member>\n"
		"</struct></value>\n";

	const char* JSON_PARAMETER_ITEM =
		"{\n"
		"\"Name\" : \"%s\",\n"
		"\"Value\" : \"%s\"\n"
		"}";

	const char* XML_SCRIPT_ITEM =
		"<value><struct>\n"
		"<member><name>Name</name><value><string>%s</string></value></member>\n"
		"<member><name>Status</name><value><string>%s</string></value></member>\n"
		"</struct></value>\n";

	const char* JSON_SCRIPT_ITEM =
		"{\n"
		"\"Name\" : \"%s\",\n"
		"\"Status\" : \"%s\"\n"
		"}";

	const char* XML_STAT_ITEM =
		"<value><struct>\n"
		"<member><name>ServerID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>SuccessArticles</name><value><i4>%i</i4></value></member>\n"
		"<member><name>FailedArticles</name><value><i4>%i</i4></value></member>\n"
		"</struct></value>\n";

	const char* JSON_STAT_ITEM =
		"{\n"
		"\"ServerID\" : %i,\n"
		"\"SuccessArticles\" : %i,\n"
		"\"FailedArticles\" : %i\n"
		"}";

	const char* kindName[] = { "NZB", "URL" };
	const char* parStatusName[] = { "NONE", "NONE", "FAILURE", "SUCCESS", "REPAIR_POSSIBLE", "MANUAL" };
	const char* unpackStatusName[] = { "NONE", "NONE", "FAILURE", "SUCCESS", "SPACE", "PASSWORD" };
	const char* moveStatusName[] = { "NONE", "FAILURE", "SUCCESS" };
	const char* scriptStatusName[] = { "NONE", "FAILURE", "SUCCESS" };
	const char* deleteStatusName[] = { "NONE", "MANUAL", "HEALTH", "DUPE", "BAD", "GOOD", "COPY", "SCAN" };
	const char* markStatusName[] = { "NONE", "BAD", "GOOD", "SUCCESS" };
	const char* urlStatusName[] = { "NONE", "UNKNOWN", "SUCCESS", "FAILURE", "UNKNOWN", "SCAN_SKIPPED", "SCAN_FAILURE" };
	const char* dupeModeName[] = { "SCORE", "ALL", "FORCE" };

	uint32 fileSizeHi, fileSizeLo, fileSizeMB;
	Util::SplitInt64(nzbInfo->GetSize(), &fileSizeHi, &fileSizeLo);
	fileSizeMB = (int)(nzbInfo->GetSize() / 1024 / 1024);

	uint32 downloadedSizeHi, downloadedSizeLo, downloadedSizeMB;
	Util::SplitInt64(nzbInfo->GetDownloadedSize(), &downloadedSizeHi, &downloadedSizeLo);
	downloadedSizeMB = (int)(nzbInfo->GetDownloadedSize() / 1024 / 1024);

	int messageCount = nzbInfo->GetMessageCount() > 0 ? nzbInfo->GetMessageCount() : nzbInfo->GetCachedMessageCount();

	char* xmlUrl = EncodeStr(nzbInfo->GetUrl());
	char* xmlNzbFilename = EncodeStr(nzbInfo->GetFilename());
	char* xmlNzbNicename = EncodeStr(nzbInfo->GetName());
	char* xmlDestDir = EncodeStr(nzbInfo->GetDestDir());
	char* xmlFinalDir = EncodeStr(nzbInfo->GetFinalDir());
	char* xmlCategory = EncodeStr(nzbInfo->GetCategory());
	char* xmlDupeKey = EncodeStr(nzbInfo->GetDupeKey());
	const char* exParStatus = nzbInfo->GetExtraParBlocks() > 0 ? "RECIPIENT" : nzbInfo->GetExtraParBlocks() < 0 ? "DONOR" : "NONE";

	AppendFmtResponse(IsJson() ? JSON_NZB_ITEM_START : XML_NZB_ITEM_START,
			 nzbInfo->GetId(), xmlNzbNicename, xmlNzbNicename, kindName[nzbInfo->GetKind()],
			 xmlUrl, xmlNzbFilename, xmlDestDir, xmlFinalDir, xmlCategory,
			 parStatusName[nzbInfo->GetParStatus()], exParStatus,
			 unpackStatusName[nzbInfo->GetUnpackStatus()], moveStatusName[nzbInfo->GetMoveStatus()],
			 scriptStatusName[nzbInfo->GetScriptStatuses()->CalcTotalStatus()],
			 deleteStatusName[nzbInfo->GetDeleteStatus()], markStatusName[nzbInfo->GetMarkStatus()],
			 urlStatusName[nzbInfo->GetUrlStatus()],
			 fileSizeLo, fileSizeHi, fileSizeMB, nzbInfo->GetFileCount(),
			 nzbInfo->GetMinTime(), nzbInfo->GetMaxTime(),
			 nzbInfo->GetTotalArticles(), nzbInfo->GetCurrentSuccessArticles(), nzbInfo->GetCurrentFailedArticles(),
			 nzbInfo->CalcHealth(), nzbInfo->CalcCriticalHealth(false),
			 xmlDupeKey, nzbInfo->GetDupeScore(), dupeModeName[nzbInfo->GetDupeMode()],
			 BoolToStr(nzbInfo->GetDeleteStatus() != NzbInfo::dsNone),
			 downloadedSizeLo, downloadedSizeHi, downloadedSizeMB, nzbInfo->GetDownloadSec(),
			 nzbInfo->GetPostInfo() && nzbInfo->GetPostInfo()->GetStartTime() ? time(NULL) - nzbInfo->GetPostInfo()->GetStartTime() : nzbInfo->GetPostTotalSec(),
			 nzbInfo->GetParSec(), nzbInfo->GetRepairSec(), nzbInfo->GetUnpackSec(), messageCount, nzbInfo->GetExtraParBlocks());

	free(xmlUrl);
	free(xmlNzbNicename);
	free(xmlNzbFilename);
	free(xmlCategory);
	free(xmlDestDir);
	free(xmlFinalDir);
	free(xmlDupeKey);

	// Post-processing parameters
	int paramIndex = 0;
	for (NzbParameterList::iterator it = nzbInfo->GetParameters()->begin(); it != nzbInfo->GetParameters()->end(); it++)
	{
		NzbParameter* parameter = *it;

		char* xmlName = EncodeStr(parameter->GetName());
		char* xmlValue = EncodeStr(parameter->GetValue());

		AppendCondResponse(",\n", IsJson() && paramIndex++ > 0);
		AppendFmtResponse(IsJson() ? JSON_PARAMETER_ITEM : XML_PARAMETER_ITEM, xmlName, xmlValue);

		free(xmlName);
		free(xmlValue);
	}

	AppendResponse(IsJson() ? JSON_NZB_ITEM_SCRIPT_START : XML_NZB_ITEM_SCRIPT_START);

	// Script statuses
	int scriptIndex = 0;
	for (ScriptStatusList::iterator it = nzbInfo->GetScriptStatuses()->begin(); it != nzbInfo->GetScriptStatuses()->end(); it++)
	{
		ScriptStatus* scriptStatus = *it;

		char* xmlName = EncodeStr(scriptStatus->GetName());
		char* xmlStatus = EncodeStr(scriptStatusName[scriptStatus->GetStatus()]);

		AppendCondResponse(",\n", IsJson() && scriptIndex++ > 0);
		AppendFmtResponse(IsJson() ? JSON_SCRIPT_ITEM : XML_SCRIPT_ITEM, xmlName, xmlStatus);

		free(xmlName);
		free(xmlStatus);
	}

	AppendResponse(IsJson() ? JSON_NZB_ITEM_STATS_START : XML_NZB_ITEM_STATS_START);

	// Server stats
	int statIndex = 0;
	for (ServerStatList::iterator it = nzbInfo->GetCurrentServerStats()->begin(); it != nzbInfo->GetCurrentServerStats()->end(); it++)
	{
		ServerStat* serverStat = *it;

		AppendCondResponse(",\n", IsJson() && statIndex++ > 0);
		AppendFmtResponse(IsJson() ? JSON_STAT_ITEM : XML_STAT_ITEM,
				 serverStat->GetServerId(), serverStat->GetSuccessArticles(), serverStat->GetFailedArticles());
	}

	AppendResponse(IsJson() ? JSON_NZB_ITEM_END : XML_NZB_ITEM_END);
}

void NzbInfoXmlCommand::AppendPostInfoFields(PostInfo* postInfo, int logEntries, bool postQueue)
{
	const char* XML_GROUPQUEUE_ITEM_START =
		"<member><name>PostInfoText</name><value><string>%s</string></value></member>\n"
		"<member><name>PostStageProgress</name><value><i4>%i</i4></value></member>\n"
		"<member><name>PostStageTimeSec</name><value><i4>%i</i4></value></member>\n";
		// PostTotalTimeSec is printed by method "AppendNZBInfoFields"

	const char* XML_POSTQUEUE_ITEM_START =
		"<member><name>ProgressLabel</name><value><string>%s</string></value></member>\n"
		"<member><name>StageProgress</name><value><i4>%i</i4></value></member>\n"
		"<member><name>StageTimeSec</name><value><i4>%i</i4></value></member>\n"
		"<member><name>TotalTimeSec</name><value><i4>%i</i4></value></member>\n";

	const char* XML_LOG_START =
		"<member><name>Log</name><value><array><data>\n";

	const char* XML_POSTQUEUE_ITEM_END =
		"</data></array></value></member>\n";

	const char* JSON_GROUPQUEUE_ITEM_START =
		"\"PostInfoText\" : \"%s\",\n"
		"\"PostStageProgress\" : %i,\n"
		"\"PostStageTimeSec\" : %i,\n";

	const char* JSON_POSTQUEUE_ITEM_START =
		"\"ProgressLabel\" : \"%s\",\n"
		"\"StageProgress\" : %i,\n"
		"\"StageTimeSec\" : %i,\n"
		"\"TotalTimeSec\" : %i,\n";

	const char* JSON_LOG_START =
		"\"Log\" : [\n";

	const char* JSON_POSTQUEUE_ITEM_END =
		"]\n";

	const char* XML_LOG_ITEM =
		"<value><struct>\n"
		"<member><name>ID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Kind</name><value><string>%s</string></value></member>\n"
		"<member><name>Time</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Text</name><value><string>%s</string></value></member>\n"
		"</struct></value>\n";

	const char* JSON_LOG_ITEM =
		"{\n"
		"\"ID\" : %i,\n"
		"\"Kind\" : \"%s\",\n"
		"\"Time\" : %i,\n"
		"\"Text\" : \"%s\"\n"
		"}";

	const char* messageType[] = { "INFO", "WARNING", "ERROR", "DEBUG", "DETAIL"};

	const char* itemStart = postQueue ? IsJson() ? JSON_POSTQUEUE_ITEM_START : XML_POSTQUEUE_ITEM_START :
		IsJson() ? JSON_GROUPQUEUE_ITEM_START : XML_GROUPQUEUE_ITEM_START;

	if (postInfo)
	{
		time_t curTime = time(NULL);
		char* xmlProgressLabel = EncodeStr(postInfo->GetProgressLabel());

		AppendFmtResponse(itemStart, xmlProgressLabel, postInfo->GetStageProgress(),
			postInfo->GetStageTime() ? curTime - postInfo->GetStageTime() : 0,
			postInfo->GetStartTime() ? curTime - postInfo->GetStartTime() : 0);

		free(xmlProgressLabel);
	}
	else
	{
		AppendFmtResponse(itemStart, "NONE", "", 0, 0, 0, 0);
	}

	AppendResponse(IsJson() ? JSON_LOG_START : XML_LOG_START);

	if (logEntries > 0 && postInfo)
	{
		MessageList* messages = postInfo->GetNzbInfo()->LockCachedMessages();
		if (!messages->empty())
		{
			if (logEntries > (int)messages->size())
			{
				logEntries = messages->size();
			}
			int start = messages->size() - logEntries;

			int index = 0;
			for (uint32 i = (uint32)start; i < messages->size(); i++)
			{
				Message* message = (*messages)[i];

				char* xmltext = EncodeStr(message->GetText());

				AppendCondResponse(",\n", IsJson() && index++ > 0);
				AppendFmtResponse(IsJson() ? JSON_LOG_ITEM : XML_LOG_ITEM,
					message->GetId(), messageType[message->GetKind()], message->GetTime(), xmltext);

				free(xmltext);
			}
		}
		postInfo->GetNzbInfo()->UnlockCachedMessages();
	}

	AppendResponse(IsJson() ? JSON_POSTQUEUE_ITEM_END : XML_POSTQUEUE_ITEM_END);
}

// struct[] listgroups(int NumberOfLogEntries)
void ListGroupsXmlCommand::Execute()
{
	int nrEntries = 0;
	NextParamAsInt(&nrEntries);

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");

	const char* XML_LIST_ITEM_START =
		"<value><struct>\n"
		"<member><name>FirstID</name><value><i4>%i</i4></value></member>\n"				// deprecated, use "NZBID" instead
		"<member><name>LastID</name><value><i4>%i</i4></value></member>\n"				// deprecated, use "NZBID" instead
		"<member><name>RemainingSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>RemainingSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>RemainingSizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>PausedSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>PausedSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>PausedSizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>RemainingFileCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>RemainingParCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>MinPriority</name><value><i4>%i</i4></value></member>\n"
		"<member><name>MaxPriority</name><value><i4>%i</i4></value></member>\n"
		"<member><name>ActiveDownloads</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Status</name><value><string>%s</string></value></member>\n";

	const char* XML_LIST_ITEM_END =
		"</struct></value>\n";

	const char* JSON_LIST_ITEM_START =
		"{\n"
		"\"FirstID\" : %i,\n"					// deprecated, use "NZBID" instead
		"\"LastID\" : %i,\n"					// deprecated, use "NZBID" instead
		"\"RemainingSizeLo\" : %u,\n"
		"\"RemainingSizeHi\" : %u,\n"
		"\"RemainingSizeMB\" : %i,\n"
		"\"PausedSizeLo\" : %u,\n"
		"\"PausedSizeHi\" : %u,\n"
		"\"PausedSizeMB\" : %i,\n"
		"\"RemainingFileCount\" : %i,\n"
		"\"RemainingParCount\" : %i,\n"
		"\"MinPriority\" : %i,\n"
		"\"MaxPriority\" : %i,\n"
		"\"ActiveDownloads\" : %i,\n"
		"\"Status\" : \"%s\",\n";

	const char* JSON_LIST_ITEM_END =
		"}";

	int index = 0;

	DownloadQueue* downloadQueue = DownloadQueue::Lock();

	for (NzbList::iterator it = downloadQueue->GetQueue()->begin(); it != downloadQueue->GetQueue()->end(); it++)
	{
		NzbInfo* nzbInfo = *it;

		uint32 remainingSizeLo, remainingSizeHi, remainingSizeMB;
		uint32 pausedSizeLo, pausedSizeHi, pausedSizeMB;
		Util::SplitInt64(nzbInfo->GetRemainingSize(), &remainingSizeHi, &remainingSizeLo);
		remainingSizeMB = (int)(nzbInfo->GetRemainingSize() / 1024 / 1024);
		Util::SplitInt64(nzbInfo->GetPausedSize(), &pausedSizeHi, &pausedSizeLo);
		pausedSizeMB = (int)(nzbInfo->GetPausedSize() / 1024 / 1024);
		const char* status = DetectStatus(nzbInfo);

		AppendCondResponse(",\n", IsJson() && index++ > 0);
		AppendFmtResponse(IsJson() ? JSON_LIST_ITEM_START : XML_LIST_ITEM_START,
			nzbInfo->GetId(), nzbInfo->GetId(), remainingSizeLo, remainingSizeHi, remainingSizeMB,
			pausedSizeLo, pausedSizeHi, pausedSizeMB, (int)nzbInfo->GetFileList()->size(),
			nzbInfo->GetRemainingParCount(), nzbInfo->GetPriority(), nzbInfo->GetPriority(),
			nzbInfo->GetActiveDownloads(), status);

		AppendNzbInfoFields(nzbInfo);
		AppendCondResponse(",\n", IsJson());
		AppendPostInfoFields(nzbInfo->GetPostInfo(), nrEntries, false);

		AppendResponse(IsJson() ? JSON_LIST_ITEM_END : XML_LIST_ITEM_END);
	}

	DownloadQueue::Unlock();

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

const char* ListGroupsXmlCommand::DetectStatus(NzbInfo* nzbInfo)
{
	const char* postStageName[] = { "PP_QUEUED", "LOADING_PARS", "VERIFYING_SOURCES", "REPAIRING", "VERIFYING_REPAIRED", "RENAMING", "UNPACKING", "MOVING", "EXECUTING_SCRIPT", "PP_FINISHED" };

	const char* status = NULL;

	if (nzbInfo->GetPostInfo())
	{
		bool queueScriptActive = false;
		if (nzbInfo->GetPostInfo()->GetStage() == PostInfo::ptQueued &&
			g_QueueScriptCoordinator->HasJob(nzbInfo->GetId(), &queueScriptActive))
		{
			status = queueScriptActive ? "QS_EXECUTING" : "QS_QUEUED";
		}
		else
		{
			status = postStageName[nzbInfo->GetPostInfo()->GetStage()];
		}
	}
	else if (nzbInfo->GetActiveDownloads() > 0)
	{
		status = nzbInfo->GetKind() == NzbInfo::nkUrl ? "FETCHING" : "DOWNLOADING";
	}
	else if ((nzbInfo->GetPausedSize() > 0) && (nzbInfo->GetRemainingSize() == nzbInfo->GetPausedSize()))
	{
		status = "PAUSED";
	}
	else
	{
		status = "QUEUED";
	}

	return status;
}

typedef struct
{
	int				actionId;
	const char*	actionName;
} EditCommandEntry;

EditCommandEntry EditCommandNameMap[] = {
	{ DownloadQueue::eaFileMoveOffset, "FileMoveOffset" },
	{ DownloadQueue::eaFileMoveTop, "FileMoveTop" },
	{ DownloadQueue::eaFileMoveBottom, "FileMoveBottom" },
	{ DownloadQueue::eaFilePause, "FilePause" },
	{ DownloadQueue::eaFileResume, "FileResume" },
	{ DownloadQueue::eaFileDelete, "FileDelete" },
	{ DownloadQueue::eaFilePauseAllPars, "FilePauseAllPars" },
	{ DownloadQueue::eaFilePauseExtraPars, "FilePauseExtraPars" },
	{ DownloadQueue::eaFileReorder, "FileReorder" },
	{ DownloadQueue::eaFileSplit, "FileSplit" },
	{ DownloadQueue::eaGroupMoveOffset, "GroupMoveOffset" },
	{ DownloadQueue::eaGroupMoveTop, "GroupMoveTop" },
	{ DownloadQueue::eaGroupMoveBottom, "GroupMoveBottom" },
	{ DownloadQueue::eaGroupPause, "GroupPause" },
	{ DownloadQueue::eaGroupResume, "GroupResume" },
	{ DownloadQueue::eaGroupDelete, "GroupDelete" },
	{ DownloadQueue::eaGroupDupeDelete, "GroupDupeDelete" },
	{ DownloadQueue::eaGroupFinalDelete, "GroupFinalDelete" },
	{ DownloadQueue::eaGroupPauseAllPars, "GroupPauseAllPars" },
	{ DownloadQueue::eaGroupPauseExtraPars, "GroupPauseExtraPars" },
	{ DownloadQueue::eaGroupSetPriority, "GroupSetPriority" },
	{ DownloadQueue::eaGroupSetCategory, "GroupSetCategory" },
	{ DownloadQueue::eaGroupApplyCategory, "GroupApplyCategory" },
	{ DownloadQueue::eaGroupMerge, "GroupMerge" },
	{ DownloadQueue::eaGroupSetParameter, "GroupSetParameter" },
	{ DownloadQueue::eaGroupSetName, "GroupSetName" },
	{ DownloadQueue::eaGroupSetDupeKey, "GroupSetDupeKey" },
	{ DownloadQueue::eaGroupSetDupeScore, "GroupSetDupeScore" },
	{ DownloadQueue::eaGroupSetDupeMode, "GroupSetDupeMode" },
	{ DownloadQueue::eaGroupSort, "GroupSort" },
	{ DownloadQueue::eaPostDelete, "PostDelete" },
	{ DownloadQueue::eaHistoryDelete, "HistoryDelete" },
	{ DownloadQueue::eaHistoryFinalDelete, "HistoryFinalDelete" },
	{ DownloadQueue::eaHistoryReturn, "HistoryReturn" },
	{ DownloadQueue::eaHistoryProcess, "HistoryProcess" },
	{ DownloadQueue::eaHistoryRedownload, "HistoryRedownload" },
	{ DownloadQueue::eaHistorySetParameter, "HistorySetParameter" },
	{ DownloadQueue::eaHistorySetDupeKey, "HistorySetDupeKey" },
	{ DownloadQueue::eaHistorySetDupeScore, "HistorySetDupeScore" },
	{ DownloadQueue::eaHistorySetDupeMode, "HistorySetDupeMode" },
	{ DownloadQueue::eaHistorySetDupeBackup, "HistorySetDupeBackup" },
	{ DownloadQueue::eaHistoryMarkBad, "HistoryMarkBad" },
	{ DownloadQueue::eaHistoryMarkGood, "HistoryMarkGood" },
	{ DownloadQueue::eaHistoryMarkSuccess, "HistoryMarkSuccess" },
	{ DownloadQueue::eaHistorySetCategory, "HistorySetCategory" },
	{ DownloadQueue::eaHistorySetName, "HistorySetName" },
	{ 0, NULL }
};

void EditQueueXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	char* editCommand;
	if (!NextParamAsStr(&editCommand))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}
	debug("EditCommand=%s", editCommand);

	int action = -1;
	for (int i = 0; const char* name = EditCommandNameMap[i].actionName; i++)
	{
		if (!strcasecmp(editCommand, name))
		{
			action = EditCommandNameMap[i].actionId;
			break;
		}
	}

	if (action == -1)
	{
		BuildErrorResponse(3, "Invalid action");
		return;
	}

	int offset = 0;
	if (!NextParamAsInt(&offset))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	char* editText;
	if (!NextParamAsStr(&editText))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}
	debug("EditText=%s", editText);

	DecodeStr(editText);

	IdList cIdList;
	int id = 0;
	while (NextParamAsInt(&id))
	{
		cIdList.push_back(id);
	}

	DownloadQueue* downloadQueue = DownloadQueue::Lock();
	bool ok = downloadQueue->EditList(&cIdList, NULL, DownloadQueue::mmId, (DownloadQueue::EEditAction)action, offset, editText);
	DownloadQueue::Unlock();

	BuildBoolResponse(ok);
}

// v16:
//   int append(string NZBFilename, string NZBContent, string Category, int Priority, bool AddToTop, bool AddPaused, string DupeKey, int DupeScore, string DupeMode, struct[] Parameters)
// v13 (new param order and new result type):
//   int append(string NZBFilename, string NZBContent, string Category, int Priority, bool AddToTop, bool AddPaused, string DupeKey, int DupeScore, string DupeMode)
// v12 (backward compatible, some params are optional):
//   bool append(string NZBFilename, string Category, int Priority, bool AddToTop, string Content, bool AddPaused, string DupeKey, int DupeScore, string DupeMode)
void DownloadXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	bool v13 = true;

	char* nzbFilename;
	if (!NextParamAsStr(&nzbFilename))
	{
		BuildErrorResponse(2, "Invalid parameter (NZBFileName)");
		return;
	}

	char* nzbContent;
	if (!NextParamAsStr(&nzbContent))
	{
		BuildErrorResponse(2, "Invalid parameter (NZBContent)");
		return;
	}

	char* category;
	if (!NextParamAsStr(&category))
	{
		v13 = false;
		category = nzbContent;
	}

	DecodeStr(nzbFilename);
	DecodeStr(category);

	debug("FileName=%s", nzbFilename);

	// For backward compatibility with 0.8 parameter "Priority" is optional (error checking omitted)
	int priority = 0;
	NextParamAsInt(&priority);

	bool addTop;
	if (!NextParamAsBool(&addTop))
	{
		BuildErrorResponse(2, "Invalid parameter (AddTop)");
		return;
	}

	if (!v13 && !NextParamAsStr(&nzbContent))
	{
		BuildErrorResponse(2, "Invalid parameter (FileContent)");
		return;
	}
	DecodeStr(nzbContent);

	bool addPaused = false;
	char* dupeKey = NULL;
	int dupeScore = 0;
	EDupeMode dupeMode = dmScore;
	if (NextParamAsBool(&addPaused))
	{
		if (!NextParamAsStr(&dupeKey))
		{
			BuildErrorResponse(2, "Invalid parameter (DupeKey)");
			return;
		}
		DecodeStr(dupeKey);
		if (!NextParamAsInt(&dupeScore))
		{
			BuildErrorResponse(2, "Invalid parameter (DupeScore)");
			return;
		}
		char* dupeModeStr = NULL;
		if (!NextParamAsStr(&dupeModeStr) ||
			(strcasecmp(dupeModeStr, "score") && strcasecmp(dupeModeStr, "all") && strcasecmp(dupeModeStr, "force")))
		{
			BuildErrorResponse(2, "Invalid parameter (DupeMode)");
			return;
		}
		dupeMode = !strcasecmp(dupeModeStr, "all") ? dmAll :
			!strcasecmp(dupeModeStr, "force") ? dmForce : dmScore;
	}
	else if (v13)
	{
		BuildErrorResponse(2, "Invalid parameter (AddPaused)");
		return;
	}

	NzbParameterList Params;
	if (v13)
	{
		char* paramName = NULL;
		char* paramValue = NULL;
		while (NextParamAsStr(&paramName))
		{
			if (!NextParamAsStr(&paramValue))
			{
				BuildErrorResponse(2, "Invalid parameter (Parameters)");
				return;
			}
			Params.SetParameter(paramName, paramValue);
		}
	}

	if (!strncasecmp(nzbContent, "http://", 6) || !strncasecmp(nzbContent, "https://", 7))
	{
		// add url
		NzbInfo* nzbInfo = new NzbInfo();
		nzbInfo->SetKind(NzbInfo::nkUrl);
		nzbInfo->SetUrl(nzbContent);
		nzbInfo->SetFilename(nzbFilename);
		nzbInfo->SetCategory(category);
		nzbInfo->SetPriority(priority);
		nzbInfo->SetAddUrlPaused(addPaused);
		nzbInfo->SetDupeKey(dupeKey ? dupeKey : "");
		nzbInfo->SetDupeScore(dupeScore);
		nzbInfo->SetDupeMode(dupeMode);
		nzbInfo->GetParameters()->CopyFrom(&Params);
		int nzbId = nzbInfo->GetId();

		char nicename[1024];
		nzbInfo->MakeNiceUrlName(nzbContent, nzbFilename, nicename, sizeof(nicename));
		info("Queue %s", nicename);

		DownloadQueue* downloadQueue = DownloadQueue::Lock();
		downloadQueue->GetQueue()->Add(nzbInfo, addTop);
		downloadQueue->Save();
		DownloadQueue::Unlock();

		if (v13)
		{
			BuildIntResponse(nzbId);
		}
		else
		{
			BuildBoolResponse(true);
		}
	}
	else
	{
		// add file content
		int len = WebUtil::DecodeBase64(nzbContent, 0, nzbContent);
		nzbContent[len] = '\0';
		//debug("FileContent=%s", szFileContent);

		int nzbId = -1;
		g_Scanner->AddExternalFile(nzbFilename, category, priority,
			dupeKey, dupeScore, dupeMode, Params.empty() ? NULL : &Params, addTop, addPaused, NULL,
			NULL, nzbContent, len, &nzbId);

		if (v13)
		{
			BuildIntResponse(nzbId);
		}
		else
		{
			BuildBoolResponse(nzbId > 0);
		}
	}
}

// deprecated
void PostQueueXmlCommand::Execute()
{
	int nrEntries = 0;
	NextParamAsInt(&nrEntries);

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");

	const char* XML_POSTQUEUE_ITEM_START =
		"<value><struct>\n"
		"<member><name>ID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>InfoName</name><value><string>%s</string></value></member>\n"
		"<member><name>ParFilename</name><value><string></string></value></member>\n"		// deprecated, always empty
		"<member><name>Stage</name><value><string>%s</string></value></member>\n"
		"<member><name>FileProgress</name><value><i4>%i</i4></value></member>\n";

	const char* XML_POSTQUEUE_ITEM_END =
		"</struct></value>\n";

	const char* JSON_POSTQUEUE_ITEM_START =
		"{\n"
		"\"ID\" : %i,\n"
		"\"InfoName\" : \"%s\",\n"
		"\"ParFilename\" : \"\",\n"		// deprecated, always empty
		"\"Stage\" : \"%s\",\n"
		"\"FileProgress\" : %i,\n";

	const char* JSON_POSTQUEUE_ITEM_END =
		"}";

	const char* postStageName[] = { "QUEUED", "LOADING_PARS", "VERIFYING_SOURCES", "REPAIRING", "VERIFYING_REPAIRED", "RENAMING", "UNPACKING", "MOVING", "EXECUTING_SCRIPT", "FINISHED" };

	NzbList* nzbList = DownloadQueue::Lock()->GetQueue();

	int index = 0;

	for (NzbList::iterator it = nzbList->begin(); it != nzbList->end(); it++)
	{
		NzbInfo* nzbInfo = *it;
		PostInfo* postInfo = nzbInfo->GetPostInfo();
		if (!postInfo)
		{
			continue;
		}

		char* xmlInfoName = EncodeStr(postInfo->GetNzbInfo()->GetName());

		AppendCondResponse(",\n", IsJson() && index++ > 0);
		AppendFmtResponse(IsJson() ? JSON_POSTQUEUE_ITEM_START : XML_POSTQUEUE_ITEM_START,
			nzbInfo->GetId(), xmlInfoName, postStageName[postInfo->GetStage()], postInfo->GetFileProgress());

		free(xmlInfoName);

		AppendNzbInfoFields(postInfo->GetNzbInfo());
		AppendCondResponse(",\n", IsJson());
		AppendPostInfoFields(postInfo, nrEntries, true);

		AppendResponse(IsJson() ? JSON_POSTQUEUE_ITEM_END : XML_POSTQUEUE_ITEM_END);
	}

	DownloadQueue::Unlock();

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

void WriteLogXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	char* kind;
	char* text;
	if (!NextParamAsStr(&kind) || !NextParamAsStr(&text))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	DecodeStr(text);

	debug("Kind=%s, Text=%s", kind, text);

	if (!strcmp(kind, "INFO"))
	{
		info("%s", text);
	}
	else if (!strcmp(kind, "WARNING"))
	{
		warn("%s", text);
	}
	else if (!strcmp(kind, "ERROR"))
	{
		error("%s", text);
	}
	else if (!strcmp(kind, "DETAIL"))
	{
		detail("%s", text);
	}
	else if (!strcmp(kind, "DEBUG"))
	{
		debug("%s", text);
	}
	else
	{
		BuildErrorResponse(3, "Invalid Kind");
		return;
	}

	BuildBoolResponse(true);
}

void ClearLogXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	g_Log->Clear();

	BuildBoolResponse(true);
}

void ScanXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	bool syncMode = false;
	// optional parameter "SyncMode"
	NextParamAsBool(&syncMode);

	g_Scanner->ScanNzbDir(syncMode);
	BuildBoolResponse(true);
}

// struct[] history(bool hidden)
// Parameter "hidden" is optional (new in v12)
void HistoryXmlCommand::Execute()
{
	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");

	const char* XML_HISTORY_ITEM_START =
		"<value><struct>\n"
		"<member><name>ID</name><value><i4>%i</i4></value></member>\n"					// Deprecated, use "NZBID" instead
		"<member><name>Name</name><value><string>%s</string></value></member>\n"
		"<member><name>RemainingFileCount</name><value><i4>%i</i4></value></member>\n"
		"<member><name>HistoryTime</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Status</name><value><string>%s</string></value></member>\n"
		"<member><name>Log</name><value><array><data></data></array></value></member>\n";	// Deprected, always empty

	const char* JSON_HISTORY_ITEM_START =
		"{\n"
		"\"ID\" : %i,\n"								// Deprecated, use "NZBID" instead
		"\"Name\" : \"%s\",\n"
		"\"RemainingFileCount\" : %i,\n"
		"\"HistoryTime\" : %i,\n"
		"\"Status\" : \"%s\",\n"
		"\"Log\" : [],\n";								// Deprected, always empty

	const char* XML_HISTORY_ITEM_END =
		"</struct></value>";

	const char* JSON_HISTORY_ITEM_END =
		"}";

	const char* XML_HISTORY_DUP_ITEM =
		"<value><struct>\n"
		"<member><name>ID</name><value><i4>%i</i4></value></member>\n"				// Deprecated, use "NZBID" instead
		"<member><name>NZBID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Kind</name><value><string>%s</string></value></member>\n"
		"<member><name>Name</name><value><string>%s</string></value></member>\n"
		"<member><name>HistoryTime</name><value><i4>%i</i4></value></member>\n"
		"<member><name>FileSizeLo</name><value><i4>%u</i4></value></member>\n"
		"<member><name>FileSizeHi</name><value><i4>%u</i4></value></member>\n"
		"<member><name>FileSizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DupeKey</name><value><string>%s</string></value></member>\n"
		"<member><name>DupeScore</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DupeMode</name><value><string>%s</string></value></member>\n"
		"<member><name>DupStatus</name><value><string>%s</string></value></member>\n"
		"<member><name>Status</name><value><string>%s</string></value></member>\n";

	const char* JSON_HISTORY_DUP_ITEM =
		"{\n"
		"\"ID\" : %i,\n"							// Deprecated, use "NZBID" instead
		"\"NZBID\" : %i,\n"
		"\"Kind\" : \"%s\",\n"
		"\"Name\" : \"%s\",\n"
		"\"HistoryTime\" : %i,\n"
		"\"FileSizeLo\" : %i,\n"
		"\"FileSizeHi\" : %i,\n"
		"\"FileSizeMB\" : %i,\n"
		"\"DupeKey\" : \"%s\",\n"
		"\"DupeScore\" : %i,\n"
		"\"DupeMode\" : \"%s\",\n"
		"\"DupStatus\" : \"%s\",\n"
		"\"Status\" : \"%s\"\n";

	const char* dupStatusName[] = { "UNKNOWN", "SUCCESS", "FAILURE", "DELETED", "DUPE", "BAD", "GOOD" };
	const char* dupeModeName[] = { "SCORE", "ALL", "FORCE" };

	bool dup = false;
	NextParamAsBool(&dup);

	DownloadQueue* downloadQueue = DownloadQueue::Lock();

	int index = 0;

	for (HistoryList::iterator it = downloadQueue->GetHistory()->begin(); it != downloadQueue->GetHistory()->end(); it++)
	{
		HistoryInfo* historyInfo = *it;

		if (historyInfo->GetKind() == HistoryInfo::hkDup && !dup)
		{
			continue;
		}

		NzbInfo* nzbInfo = NULL;
		char nicename[1024];
		historyInfo->GetName(nicename, sizeof(nicename));

		char *xmlNicename = EncodeStr(nicename);
		const char* status = DetectStatus(historyInfo);

		AppendCondResponse(",\n", IsJson() && index++ > 0);

		if (historyInfo->GetKind() == HistoryInfo::hkNzb ||
			historyInfo->GetKind() == HistoryInfo::hkUrl)
		{
			nzbInfo = historyInfo->GetNzbInfo();

			AppendFmtResponse(IsJson() ? JSON_HISTORY_ITEM_START : XML_HISTORY_ITEM_START,
				historyInfo->GetId(), xmlNicename, nzbInfo->GetParkedFileCount(),
				historyInfo->GetTime(), status);
		}
		else if (historyInfo->GetKind() == HistoryInfo::hkDup)
		{
			DupInfo* dupInfo = historyInfo->GetDupInfo();

			uint32 fileSizeHi, fileSizeLo, fileSizeMB;
			Util::SplitInt64(dupInfo->GetSize(), &fileSizeHi, &fileSizeLo);
			fileSizeMB = (int)(dupInfo->GetSize() / 1024 / 1024);

			char* xmlDupeKey = EncodeStr(dupInfo->GetDupeKey());

			AppendFmtResponse(IsJson() ? JSON_HISTORY_DUP_ITEM : XML_HISTORY_DUP_ITEM,
				historyInfo->GetId(), historyInfo->GetId(), "DUP", xmlNicename, historyInfo->GetTime(),
				fileSizeLo, fileSizeHi, fileSizeMB, xmlDupeKey, dupInfo->GetDupeScore(),
				dupeModeName[dupInfo->GetDupeMode()], dupStatusName[dupInfo->GetStatus()],
				status);

			free(xmlDupeKey);
		}

		free(xmlNicename);

		if (nzbInfo)
		{
			AppendNzbInfoFields(nzbInfo);
		}

		AppendResponse(IsJson() ? JSON_HISTORY_ITEM_END : XML_HISTORY_ITEM_END);
	}

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");

	DownloadQueue::Unlock();
}

const char* HistoryXmlCommand::DetectStatus(HistoryInfo* historyInfo)
{
	const char* status = "FAILURE/INTERNAL_ERROR";

	if (historyInfo->GetKind() == HistoryInfo::hkNzb || historyInfo->GetKind() == HistoryInfo::hkUrl)
	{
		NzbInfo* nzbInfo = historyInfo->GetNzbInfo();
		status = nzbInfo->MakeTextStatus(false);
	}
	else if (historyInfo->GetKind() == HistoryInfo::hkDup)
	{
		DupInfo* dupInfo = historyInfo->GetDupInfo();
		const char* dupStatusName[] = { "FAILURE/INTERNAL_ERROR", "SUCCESS/HIDDEN", "FAILURE/HIDDEN",
			"DELETED/MANUAL", "DELETED/DUPE", "FAILURE/BAD", "SUCCESS/GOOD" };
		status = dupStatusName[dupInfo->GetStatus()];
	}

	return status;
}

// Deprecated in v13
void UrlQueueXmlCommand::Execute()
{
	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");

	const char* XML_URLQUEUE_ITEM =
		"<value><struct>\n"
		"<member><name>ID</name><value><i4>%i</i4></value></member>\n"
		"<member><name>NZBFilename</name><value><string>%s</string></value></member>\n"
		"<member><name>URL</name><value><string>%s</string></value></member>\n"
		"<member><name>Name</name><value><string>%s</string></value></member>\n"
		"<member><name>Category</name><value><string>%s</string></value></member>\n"
		"<member><name>Priority</name><value><i4>%i</i4></value></member>\n"
		"</struct></value>\n";

	const char* JSON_URLQUEUE_ITEM =
		"{\n"
		"\"ID\" : %i,\n"
		"\"NZBFilename\" : \"%s\",\n"
		"\"URL\" : \"%s\",\n"
		"\"Name\" : \"%s\",\n"
		"\"Category\" : \"%s\",\n"
		"\"Priority\" : %i\n"
		"}";

	DownloadQueue* downloadQueue = DownloadQueue::Lock();

	int index = 0;

	for (NzbList::iterator it = downloadQueue->GetQueue()->begin(); it != downloadQueue->GetQueue()->end(); it++)
	{
		NzbInfo* nzbInfo = *it;

		if (nzbInfo->GetKind() == NzbInfo::nkUrl)
		{
			char* xmlNicename = EncodeStr(nzbInfo->GetName());
			char* xmlNzbFilename = EncodeStr(nzbInfo->GetFilename());
			char* xmlUrl = EncodeStr(nzbInfo->GetUrl());
			char* xmlCategory = EncodeStr(nzbInfo->GetCategory());

			AppendCondResponse(",\n", IsJson() && index++ > 0);
			AppendFmtResponse(IsJson() ? JSON_URLQUEUE_ITEM : XML_URLQUEUE_ITEM,
				nzbInfo->GetId(), xmlNzbFilename, xmlUrl, xmlNicename, xmlCategory, nzbInfo->GetPriority());

			free(xmlNicename);
			free(xmlNzbFilename);
			free(xmlUrl);
			free(xmlCategory);
		}
	}

	DownloadQueue::Unlock();

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

// struct[] config()
void ConfigXmlCommand::Execute()
{
	const char* XML_CONFIG_ITEM =
		"<value><struct>\n"
		"<member><name>Name</name><value><string>%s</string></value></member>\n"
		"<member><name>Value</name><value><string>%s</string></value></member>\n"
		"</struct></value>\n";

	const char* JSON_CONFIG_ITEM =
		"{\n"
		"\"Name\" : \"%s\",\n"
		"\"Value\" : \"%s\"\n"
		"}";

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");

	int index = 0;

	Options::OptEntries* optEntries = g_Options->LockOptEntries();

	for (Options::OptEntries::iterator it = optEntries->begin(); it != optEntries->end(); it++)
	{
		Options::OptEntry* optEntry = *it;

		char* xmlName = EncodeStr(optEntry->GetName());
		char* xmlValue = EncodeStr(m_userAccess == XmlRpcProcessor::uaRestricted &&
			optEntry->Restricted() ? "***" : optEntry->GetValue());

		AppendCondResponse(",\n", IsJson() && index++ > 0);
		AppendFmtResponse(IsJson() ? JSON_CONFIG_ITEM : XML_CONFIG_ITEM, xmlName, xmlValue);

		free(xmlName);
		free(xmlValue);
	}

	g_Options->UnlockOptEntries();

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

// struct[] loadconfig()
void LoadConfigXmlCommand::Execute()
{
	const char* XML_CONFIG_ITEM =
		"<value><struct>\n"
		"<member><name>Name</name><value><string>%s</string></value></member>\n"
		"<member><name>Value</name><value><string>%s</string></value></member>\n"
		"</struct></value>\n";

	const char* JSON_CONFIG_ITEM =
		"{\n"
		"\"Name\" : \"%s\",\n"
		"\"Value\" : \"%s\"\n"
		"}";

	Options::OptEntries* optEntries = new Options::OptEntries();
	if (!g_ScriptConfig->LoadConfig(optEntries))
	{
		BuildErrorResponse(3, "Could not read configuration file");
		delete optEntries;
		return;
	}

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");

	int index = 0;

	for (Options::OptEntries::iterator it = optEntries->begin(); it != optEntries->end(); it++)
	{
		Options::OptEntry* optEntry = *it;

		char* xmlName = EncodeStr(optEntry->GetName());
		char* xmlValue = EncodeStr(m_userAccess == XmlRpcProcessor::uaRestricted &&
			optEntry->Restricted() ? "***" : optEntry->GetValue());

		AppendCondResponse(",\n", IsJson() && index++ > 0);
		AppendFmtResponse(IsJson() ? JSON_CONFIG_ITEM : XML_CONFIG_ITEM, xmlName, xmlValue);

		free(xmlName);
		free(xmlValue);
	}

	delete optEntries;

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

// bool saveconfig(struct[] data)
void SaveConfigXmlCommand::Execute()
{
	Options::OptEntries* optEntries = new Options::OptEntries();

	char* name;
	char* value;
	char* dummy;
	while ((IsJson() && NextParamAsStr(&dummy) && NextParamAsStr(&name) &&
			NextParamAsStr(&dummy) && NextParamAsStr(&value)) ||
		   (!IsJson() && NextParamAsStr(&name) && NextParamAsStr(&value)))
	{
		DecodeStr(name);
		DecodeStr(value);
		optEntries->push_back(new Options::OptEntry(name, value));
	}

	// save to config file
	bool ok = g_ScriptConfig->SaveConfig(optEntries);

	delete optEntries;

	BuildBoolResponse(ok);
}

// struct[] configtemplates(bool loadFromDisk)
// parameter "loadFromDisk" is optional (new in v14)
void ConfigTemplatesXmlCommand::Execute()
{
	const char* XML_CONFIG_ITEM =
		"<value><struct>\n"
		"<member><name>Name</name><value><string>%s</string></value></member>\n"
		"<member><name>DisplayName</name><value><string>%s</string></value></member>\n"
		"<member><name>PostScript</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>ScanScript</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>QueueScript</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>SchedulerScript</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>FeedScript</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>Template</name><value><string>%s</string></value></member>\n"
		"</struct></value>\n";

	const char* JSON_CONFIG_ITEM =
		"{\n"
		"\"Name\" : \"%s\",\n"
		"\"DisplayName\" : \"%s\",\n"
		"\"PostScript\" : %s,\n"
		"\"ScanScript\" : %s,\n"
		"\"QueueScript\" : %s,\n"
		"\"SchedulerScript\" : %s,\n"
		"\"FeedScript\" : %s,\n"
		"\"Template\" : \"%s\"\n"
		"}";

	bool loadFromDisk = false;
	NextParamAsBool(&loadFromDisk);

	ScriptConfig::ConfigTemplates* configTemplates = g_ScriptConfig->GetConfigTemplates();

	if (loadFromDisk)
	{
		configTemplates = new ScriptConfig::ConfigTemplates();
		if (!g_ScriptConfig->LoadConfigTemplates(configTemplates))
		{
			BuildErrorResponse(3, "Could not read configuration templates");
			delete configTemplates;
			return;
		}
	}

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");

	int index = 0;

	for (ScriptConfig::ConfigTemplates::iterator it = configTemplates->begin(); it != configTemplates->end(); it++)
	{
		ScriptConfig::ConfigTemplate* configTemplate = *it;

		char* xmlName = EncodeStr(configTemplate->GetScript() ? configTemplate->GetScript()->GetName() : "");
		char* xmlDisplayName = EncodeStr(configTemplate->GetScript() ? configTemplate->GetScript()->GetDisplayName() : "");
		char* xmlTemplate = EncodeStr(configTemplate->GetTemplate());

		AppendCondResponse(",\n", IsJson() && index++ > 0);
		AppendFmtResponse(IsJson() ? JSON_CONFIG_ITEM : XML_CONFIG_ITEM,
			xmlName, xmlDisplayName,
			BoolToStr(configTemplate->GetScript() && configTemplate->GetScript()->GetPostScript()),
			BoolToStr(configTemplate->GetScript() && configTemplate->GetScript()->GetScanScript()),
			BoolToStr(configTemplate->GetScript() && configTemplate->GetScript()->GetQueueScript()),
			BoolToStr(configTemplate->GetScript() && configTemplate->GetScript()->GetSchedulerScript()),
			BoolToStr(configTemplate->GetScript() && configTemplate->GetScript()->GetFeedScript()),
			xmlTemplate);

		free(xmlName);
		free(xmlDisplayName);
		free(xmlTemplate);
	}

	if (loadFromDisk)
	{
		delete configTemplates;
	}

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

ViewFeedXmlCommand::ViewFeedXmlCommand(bool preview)
{
	m_preview = preview;
}

// struct[] viewfeed(int id)
// v12:
// struct[] previewfeed(string name, string url, string filter, bool pauseNzb, string category,
//		int priority, bool includeNonMatching, int cacheTimeSec, string cacheId)
// v16:
// struct[] previewfeed(int id, string name, string url, string filter, bool backlog, bool pauseNzb, string category,
//		int priority, int interval, string feedfilter, bool includeNonMatching, int cacheTimeSec, string cacheId)
void ViewFeedXmlCommand::Execute()
{
	bool ok = false;
	bool includeNonMatching = false;
	FeedItemInfos* feedItemInfos = NULL;

	if (m_preview)
	{
		int id = 0;
		char* name;
		char* url;
		char* filter;
		bool backlog = true;
		bool pauseNzb;
		char* category;
		int interval = 0;
		int priority;
		char* feedFilter = NULL;
		char* cacheId;
		int cacheTimeSec;

		// if the first parameter is int then it's the v16 signature
		bool v16 = NextParamAsInt(&id);

		if (!NextParamAsStr(&name) || !NextParamAsStr(&url) || !NextParamAsStr(&filter) ||
			(v16 && !NextParamAsBool(&backlog)) || !NextParamAsBool(&pauseNzb) ||
			!NextParamAsStr(&category) || !NextParamAsInt(&priority) ||
			(v16 && (!NextParamAsInt(&interval) || !NextParamAsStr(&feedFilter))) ||
			!NextParamAsBool(&includeNonMatching) || !NextParamAsInt(&cacheTimeSec) ||
			!NextParamAsStr(&cacheId))
		{
			BuildErrorResponse(2, "Invalid parameter");
			return;
		}

		DecodeStr(name);
		DecodeStr(url);
		DecodeStr(filter);
		DecodeStr(cacheId);
		DecodeStr(category);

		debug("Url=%s", url);
		debug("Filter=%s", filter);

		ok = g_FeedCoordinator->PreviewFeed(id, name, url, filter, backlog, pauseNzb,
			category, priority, interval, feedFilter, cacheTimeSec, cacheId, &feedItemInfos);
	}
	else
	{
		int id = 0;
		if (!NextParamAsInt(&id) || !NextParamAsBool(&includeNonMatching))
		{
			BuildErrorResponse(2, "Invalid parameter");
			return;
		}

		debug("ID=%i", id);

		ok = g_FeedCoordinator->ViewFeed(id, &feedItemInfos);
	}

	if (!ok)
	{
		BuildErrorResponse(3, "Could not read feed");
		return;
	}

	const char* XML_FEED_ITEM =
		"<value><struct>\n"
		"<member><name>Title</name><value><string>%s</string></value></member>\n"
		"<member><name>Filename</name><value><string>%s</string></value></member>\n"
		"<member><name>URL</name><value><string>%s</string></value></member>\n"
		"<member><name>SizeLo</name><value><i4>%i</i4></value></member>\n"
		"<member><name>SizeHi</name><value><i4>%i</i4></value></member>\n"
		"<member><name>SizeMB</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Category</name><value><string>%s</string></value></member>\n"
		"<member><name>AddCategory</name><value><string>%s</string></value></member>\n"
		"<member><name>PauseNzb</name><value><boolean>%s</boolean></value></member>\n"
		"<member><name>Priority</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Time</name><value><i4>%i</i4></value></member>\n"
		"<member><name>Match</name><value><string>%s</string></value></member>\n"
		"<member><name>Rule</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DupeKey</name><value><string>%s</string></value></member>\n"
		"<member><name>DupeScore</name><value><i4>%i</i4></value></member>\n"
		"<member><name>DupeMode</name><value><string>%s</string></value></member>\n"
		"<member><name>Status</name><value><string>%s</string></value></member>\n"
		"</struct></value>\n";

	const char* JSON_FEED_ITEM =
		"{\n"
		"\"Title\" : \"%s\",\n"
		"\"Filename\" : \"%s\",\n"
		"\"URL\" : \"%s\",\n"
		"\"SizeLo\" : %i,\n"
		"\"SizeHi\" : %i,\n"
		"\"SizeMB\" : %i,\n"
		"\"Category\" : \"%s\",\n"
		"\"AddCategory\" : \"%s\",\n"
		"\"PauseNzb\" : %s,\n"
		"\"Priority\" : %i,\n"
		"\"Time\" : %i,\n"
		"\"Match\" : \"%s\",\n"
		"\"Rule\" : %i,\n"
		"\"DupeKey\" : \"%s\",\n"
		"\"DupeScore\" : %i,\n"
		"\"DupeMode\" : \"%s\",\n"
		"\"Status\" : \"%s\"\n"
		"}";

	const char* statusType[] = { "UNKNOWN", "BACKLOG", "FETCHED", "NEW" };
	const char* matchStatusType[] = { "IGNORED", "ACCEPTED", "REJECTED" };
	const char* dupeModeType[] = { "SCORE", "ALL", "FORCE" };

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");
	int index = 0;

	for (FeedItemInfos::iterator it = feedItemInfos->begin(); it != feedItemInfos->end(); it++)
	{
		FeedItemInfo* feedItemInfo = *it;

		if (includeNonMatching || feedItemInfo->GetMatchStatus() == FeedItemInfo::msAccepted)
		{
			uint32 sizeHi, sizeLo;
			Util::SplitInt64(feedItemInfo->GetSize(), &sizeHi, &sizeLo);
			int sizeMB = (int)(feedItemInfo->GetSize() / 1024 / 1024);

			char* xmltitle = EncodeStr(feedItemInfo->GetTitle());
			char* xmlfilename = EncodeStr(feedItemInfo->GetFilename());
			char* xmlurl = EncodeStr(feedItemInfo->GetUrl());
			char* xmlcategory = EncodeStr(feedItemInfo->GetCategory());
			char* xmladdcategory = EncodeStr(feedItemInfo->GetAddCategory());
			char* xmldupekey = EncodeStr(feedItemInfo->GetDupeKey());

			AppendCondResponse(",\n", IsJson() && index++ > 0);
			AppendFmtResponse(IsJson() ? JSON_FEED_ITEM : XML_FEED_ITEM,
				xmltitle, xmlfilename, xmlurl, sizeLo, sizeHi, sizeMB, xmlcategory, xmladdcategory,
				BoolToStr(feedItemInfo->GetPauseNzb()), feedItemInfo->GetPriority(), feedItemInfo->GetTime(),
				matchStatusType[feedItemInfo->GetMatchStatus()], feedItemInfo->GetMatchRule(),
				xmldupekey, feedItemInfo->GetDupeScore(), dupeModeType[feedItemInfo->GetDupeMode()],
				statusType[feedItemInfo->GetStatus()]);

			free(xmltitle);
			free(xmlfilename);
			free(xmlurl);
			free(xmlcategory);
			free(xmladdcategory);
			free(xmldupekey);
		}
	}

	feedItemInfos->Release();

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

// bool fetchfeed(int ID)
void FetchFeedXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	int id;
	if (!NextParamAsInt(&id))
	{
		BuildErrorResponse(2, "Invalid parameter (ID)");
		return;
	}

	g_FeedCoordinator->FetchFeed(id);

	BuildBoolResponse(true);
}

// bool editserver(int ID, bool Active)
void EditServerXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	bool ok = false;
	int first = true;

	int id;
	while (NextParamAsInt(&id))
	{
		first = false;

		bool active;
		if (!NextParamAsBool(&active))
		{
			BuildErrorResponse(2, "Invalid parameter");
			return;
		}

		for (Servers::iterator it = g_ServerPool->GetServers()->begin(); it != g_ServerPool->GetServers()->end(); it++)
		{
			NewsServer* server = *it;
			if (server->GetId() == id)
			{
				server->SetActive(active);
				ok = true;
			}
		}
	}

	if (first)
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	if (ok)
	{
		g_ServerPool->Changed();
	}

	BuildBoolResponse(ok);
}

// string readurl(string url, string infoname)
void ReadUrlXmlCommand::Execute()
{
	char* url;
	if (!NextParamAsStr(&url))
	{
		BuildErrorResponse(2, "Invalid parameter (URL)");
		return;
	}
	DecodeStr(url);

	char* infoName;
	if (!NextParamAsStr(&infoName))
	{
		BuildErrorResponse(2, "Invalid parameter (InfoName)");
		return;
	}
	DecodeStr(infoName);

	// generate temp file name
	char tempFileName[1024];
	int num = 1;
	while (num == 1 || Util::FileExists(tempFileName))
	{
		snprintf(tempFileName, 1024, "%sreadurl-%i.tmp", g_Options->GetTempDir(), num);
		tempFileName[1024-1] = '\0';
		num++;
	}

	WebDownloader* downloader = new WebDownloader();
	downloader->SetUrl(url);
	downloader->SetForce(true);
	downloader->SetRetry(false);
	downloader->SetOutputFilename(tempFileName);
	downloader->SetInfoName(infoName);

	// do sync download
	WebDownloader::EStatus status = downloader->DownloadWithRedirects(5);
	bool ok = status == WebDownloader::adFinished;

	delete downloader;

	if (ok)
	{
		char* fileContent = NULL;
		int fileContentLen = 0;
		Util::LoadFileIntoBuffer(tempFileName, &fileContent, &fileContentLen);
		char* xmlContent = EncodeStr(fileContent);
		free(fileContent);
		AppendResponse(IsJson() ? "\"" : "<string>");
		AppendResponse(xmlContent);
		AppendResponse(IsJson() ? "\"" : "</string>");
		free(xmlContent);
	}
	else
	{
		BuildErrorResponse(3, "Could not read url");
	}

	remove(tempFileName);
}

// string checkupdates()
void CheckUpdatesXmlCommand::Execute()
{
	char* updateInfo = NULL;
	bool ok = g_Maintenance->CheckUpdates(&updateInfo);

	if (ok)
	{
		char* xmlContent = EncodeStr(updateInfo);
		free(updateInfo);
		AppendResponse(IsJson() ? "\"" : "<string>");
		AppendResponse(xmlContent);
		AppendResponse(IsJson() ? "\"" : "</string>");
		free(xmlContent);
	}
	else
	{
		BuildErrorResponse(3, "Could not read update info from update-info-script");
	}
}

// bool startupdate(string branch)
void StartUpdateXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	char* branchName;
	if (!NextParamAsStr(&branchName))
	{
		BuildErrorResponse(2, "Invalid parameter (Branch)");
		return;
	}
	DecodeStr(branchName);

	Maintenance::EBranch branch;
	if (!strcasecmp(branchName, "stable"))
	{
		branch = Maintenance::brStable;
	}
	else if (!strcasecmp(branchName, "testing"))
	{
		branch = Maintenance::brTesting;
	}
	else if (!strcasecmp(branchName, "devel"))
	{
		branch = Maintenance::brDevel;
	}
	else
	{
		BuildErrorResponse(2, "Invalid parameter (Branch)");
		return;
	}

	bool ok = g_Maintenance->StartUpdate(branch);

	BuildBoolResponse(ok);
}

// struct[] logupdate(idfrom, entries)
MessageList* LogUpdateXmlCommand::LockMessages()
{
	return g_Maintenance->LockMessages();
}

void LogUpdateXmlCommand::UnlockMessages()
{
	g_Maintenance->UnlockMessages();
}

// struct[] servervolumes()
void ServerVolumesXmlCommand::Execute()
{
	const char* XML_VOLUME_ITEM_START =
	"<value><struct>\n"
	"<member><name>ServerID</name><value><i4>%i</i4></value></member>\n"
	"<member><name>DataTime</name><value><i4>%i</i4></value></member>\n"
	"<member><name>FirstDay</name><value><i4>%i</i4></value></member>\n"
	"<member><name>TotalSizeLo</name><value><i4>%u</i4></value></member>\n"
	"<member><name>TotalSizeHi</name><value><i4>%u</i4></value></member>\n"
	"<member><name>TotalSizeMB</name><value><i4>%i</i4></value></member>\n"
	"<member><name>CustomSizeLo</name><value><i4>%u</i4></value></member>\n"
	"<member><name>CustomSizeHi</name><value><i4>%u</i4></value></member>\n"
	"<member><name>CustomSizeMB</name><value><i4>%i</i4></value></member>\n"
	"<member><name>CustomTime</name><value><i4>%i</i4></value></member>\n"
	"<member><name>SecSlot</name><value><i4>%i</i4></value></member>\n"
	"<member><name>MinSlot</name><value><i4>%i</i4></value></member>\n"
	"<member><name>HourSlot</name><value><i4>%i</i4></value></member>\n"
	"<member><name>DaySlot</name><value><i4>%i</i4></value></member>\n";

	const char* XML_BYTES_ARRAY_START =
	"<member><name>%s</name><value><array><data>\n";

	const char* XML_BYTES_ARRAY_ITEM =
	"<value><struct>\n"
	"<member><name>SizeLo</name><value><i4>%u</i4></value></member>\n"
	"<member><name>SizeHi</name><value><i4>%u</i4></value></member>\n"
	"<member><name>SizeMB</name><value><i4>%i</i4></value></member>\n"
	"</struct></value>\n";

	const char* XML_BYTES_ARRAY_END =
	"</data></array></value></member>\n";

	const char* XML_VOLUME_ITEM_END =
	"</struct></value>\n";

	const char* JSON_VOLUME_ITEM_START =
	"{\n"
	"\"ServerID\" : %i,\n"
	"\"DataTime\" : %i,\n"
	"\"FirstDay\" : %i,\n"
	"\"TotalSizeLo\" : %i,\n"
	"\"TotalSizeHi\" : %i,\n"
	"\"TotalSizeMB\" : %i,\n"
	"\"CustomSizeLo\" : %i,\n"
	"\"CustomSizeHi\" : %i,\n"
	"\"CustomSizeMB\" : %i,\n"
	"\"CustomTime\" : %i,\n"
	"\"SecSlot\" : %i,\n"
	"\"MinSlot\" : %i,\n"
	"\"HourSlot\" : %i,\n"
	"\"DaySlot\" : %i,\n";

	const char* JSON_BYTES_ARRAY_START =
	"\"%s\" : [\n";

	const char* JSON_BYTES_ARRAY_ITEM =
	"{\n"
	"\"SizeLo\" : %u,\n"
	"\"SizeHi\" : %u,\n"
	"\"SizeMB\" : %i\n"
	"}";

	const char* JSON_BYTES_ARRAY_END =
	"]\n";

	const char* JSON_VOLUME_ITEM_END =
	"}\n";

	AppendResponse(IsJson() ? "[\n" : "<array><data>\n");

	ServerVolumes* serverVolumes = g_StatMeter->LockServerVolumes();

	int index = 0;

	for (ServerVolumes::iterator it = serverVolumes->begin(); it != serverVolumes->end(); it++, index++)
	{
		ServerVolume* serverVolume = *it;

		uint32 totalSizeHi, totalSizeLo, totalSizeMB;
		Util::SplitInt64(serverVolume->GetTotalBytes(), &totalSizeHi, &totalSizeLo);
		totalSizeMB = (int)(serverVolume->GetTotalBytes() / 1024 / 1024);

		uint32 customSizeHi, customSizeLo, customSizeMB;
		Util::SplitInt64(serverVolume->GetCustomBytes(), &customSizeHi, &customSizeLo);
		customSizeMB = (int)(serverVolume->GetCustomBytes() / 1024 / 1024);

		AppendCondResponse(",\n", IsJson() && index > 0);
		AppendFmtResponse(IsJson() ? JSON_VOLUME_ITEM_START : XML_VOLUME_ITEM_START,
				 index, (int)serverVolume->GetDataTime(), serverVolume->GetFirstDay(),
				 totalSizeLo, totalSizeHi, totalSizeMB, customSizeLo, customSizeHi, customSizeMB,
				 (int)serverVolume->GetCustomTime(), serverVolume->GetSecSlot(),
				 serverVolume->GetMinSlot(), serverVolume->GetHourSlot(), serverVolume->GetDaySlot());

		ServerVolume::VolumeArray* VolumeArrays[] = { serverVolume->BytesPerSeconds(),
			serverVolume->BytesPerMinutes(), serverVolume->BytesPerHours(), serverVolume->BytesPerDays() };
		const char* VolumeNames[] = { "BytesPerSeconds", "BytesPerMinutes", "BytesPerHours", "BytesPerDays" };

		for (int i=0; i<4; i++)
		{
			ServerVolume::VolumeArray* volumeArray = VolumeArrays[i];
			const char* arrayName = VolumeNames[i];

			AppendFmtResponse(IsJson() ? JSON_BYTES_ARRAY_START : XML_BYTES_ARRAY_START, arrayName);

			int index2 = 0;
			for (ServerVolume::VolumeArray::iterator it2 = volumeArray->begin(); it2 != volumeArray->end(); it2++)
			{
				int64 bytes = *it2;
				uint32 sizeHi, sizeLo, sizeMB;
				Util::SplitInt64(bytes, &sizeHi, &sizeLo);
				sizeMB = (int)(bytes / 1024 / 1024);

				AppendCondResponse(",\n", IsJson() && index2++ > 0);
				AppendFmtResponse(IsJson() ? JSON_BYTES_ARRAY_ITEM : XML_BYTES_ARRAY_ITEM,
						 sizeLo, sizeHi, sizeMB);
			}

			AppendResponse(IsJson() ? JSON_BYTES_ARRAY_END : XML_BYTES_ARRAY_END);
			AppendCondResponse(",\n", IsJson() && i < 3);
		}
		AppendResponse(IsJson() ? JSON_VOLUME_ITEM_END : XML_VOLUME_ITEM_END);
	}

	g_StatMeter->UnlockServerVolumes();

	AppendResponse(IsJson() ? "\n]" : "</data></array>\n");
}

// bool resetservervolume(int serverid, string counter);
void ResetServerVolumeXmlCommand::Execute()
{
	if (!CheckSafeMethod())
	{
		return;
	}

	int serverId;
	char* counter;
	if (!NextParamAsInt(&serverId) || !NextParamAsStr(&counter))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	if (strcmp(counter, "CUSTOM"))
	{
		BuildErrorResponse(3, "Invalid Counter");
		return;
	}

	bool ok = false;
	ServerVolumes* serverVolumes = g_StatMeter->LockServerVolumes();
	int index = 0;
	for (ServerVolumes::iterator it = serverVolumes->begin(); it != serverVolumes->end(); it++, index++)
	{
		ServerVolume* serverVolume = *it;
		if (index == serverId || serverId == -1)
		{
			serverVolume->ResetCustom();
			ok = true;
		}
	}
	g_StatMeter->UnlockServerVolumes();

	BuildBoolResponse(ok);
}

// struct[] loadlog(nzbid, logidfrom, logentries)
void LoadLogXmlCommand::Execute()
{
	m_nzbInfo = NULL;
	m_nzbId = 0;
	if (!NextParamAsInt(&m_nzbId))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	LogXmlCommand::Execute();
}

MessageList* LoadLogXmlCommand::LockMessages()
{
	// TODO: optimize for m_iIDFrom and m_iNrEntries
	g_DiskState->LoadNzbMessages(m_nzbId, &m_messages);

	if (m_messages.empty())
	{
		DownloadQueue* downloadQueue = DownloadQueue::Lock();
		m_nzbInfo = downloadQueue->GetQueue()->Find(m_nzbId);
		if (m_nzbInfo)
		{
			return m_nzbInfo->LockCachedMessages();
		}
		else
		{
			DownloadQueue::Unlock();
		}
	}

	return &m_messages;
}

void LoadLogXmlCommand::UnlockMessages()
{
	if (m_nzbInfo)
	{
		m_nzbInfo->UnlockCachedMessages();
		DownloadQueue::Unlock();
	}
}

// string testserver(string host, int port, string username, string password, bool encryption, string cipher, int timeout);
void TestServerXmlCommand::Execute()
{
	const char* XML_RESPONSE_STR_BODY = "<string>%s</string>";
	const char* JSON_RESPONSE_STR_BODY = "\"%s\"";

	if (!CheckSafeMethod())
	{
		return;
	}

	char* host;
	int port;
	char* username;
	char* password;
	bool encryption;
	char* cipher;
	int timeout;

	if (!NextParamAsStr(&host) || !NextParamAsInt(&port) || !NextParamAsStr(&username) ||
		!NextParamAsStr(&password) || !NextParamAsBool(&encryption) ||
		!NextParamAsStr(&cipher) || !NextParamAsInt(&timeout))
	{
		BuildErrorResponse(2, "Invalid parameter");
		return;
	}

	NewsServer server(0, true, "test server", host, port, username, password, false, encryption, cipher, 1, 0, 0, 0);
	TestConnection* connection = new TestConnection(&server, this);
	connection->SetTimeout(timeout == 0 ? g_Options->GetArticleTimeout() : timeout);
	connection->SetSuppressErrors(false);
	m_errText = NULL;

	bool ok = connection->Connect();

	char content[1024];
	snprintf(content, 1024, IsJson() ? JSON_RESPONSE_STR_BODY : XML_RESPONSE_STR_BODY,
		ok ? "" : Util::EmptyStr(m_errText) ? "Unknown error" : m_errText);
	content[1024-1] = '\0';

	AppendResponse(content);

	delete connection;
}

void TestServerXmlCommand::PrintError(const char* errMsg)
{
	if (!m_errText)
	{
		m_errText = EncodeStr(errMsg);
	}
}
