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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include "win32.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <errno.h>

#ifdef HAVE_OPENSSL
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/pem.h>
#endif /* HAVE_OPENSSL */

#include "nzbget.h"
#include "Log.h"
#include "Util.h"
#include "Maintenance.h"
#include "Options.h"
#include "CommandLineParser.h"

extern void ExitProc();
extern int g_iArgumentCount;
extern char* (*g_szArguments)[];


#ifdef HAVE_OPENSSL
class Signature
{
private:
	const char*			m_inFilename;
	const char*			m_sigFilename;
	const char*			m_pubKeyFilename;
    unsigned char		m_inHash[SHA256_DIGEST_LENGTH];
    unsigned char		m_signature[256];
	RSA*				m_pubKey;

	bool				ReadSignature();
	bool				ComputeInHash();
	bool				ReadPubKey();

public:
						Signature(const char* inFilename, const char* sigFilename, const char* pubKeyFilename);
						~Signature();
	bool				Verify();
};
#endif


Maintenance::Maintenance()
{
	m_idMessageGen = 0;
	m_updateScriptController = NULL;
	m_updateScript = NULL;
}

Maintenance::~Maintenance()
{
	m_controllerMutex.Lock();
	if (m_updateScriptController)
	{
		m_updateScriptController->Detach();
		m_controllerMutex.Unlock();
		while (m_updateScriptController)
		{
			usleep(20*1000);
		}
	}

	m_messages.Clear();

	free(m_updateScript);
}

void Maintenance::ResetUpdateController()
{
	m_controllerMutex.Lock();
	m_updateScriptController = NULL;
	m_controllerMutex.Unlock();
}

MessageList* Maintenance::LockMessages()
{
	m_logMutex.Lock();
	return &m_messages;
}

void Maintenance::UnlockMessages()
{
	m_logMutex.Unlock();
}

void Maintenance::AddMessage(Message::EKind kind, time_t time, const char * text)
{
	if (time == 0)
	{
		time = ::time(NULL);
	}

	m_logMutex.Lock();
	Message* message = new Message(++m_idMessageGen, kind, time, text);
	m_messages.push_back(message);
	m_logMutex.Unlock();
}

bool Maintenance::StartUpdate(EBranch branch)
{
	m_controllerMutex.Lock();
	bool alreadyUpdating = m_updateScriptController != NULL;
	m_controllerMutex.Unlock();

	if (alreadyUpdating)
	{
		error("Could not start update-script: update-script is already running");
		return false;
	}

	if (m_updateScript)
	{
		free(m_updateScript);
		m_updateScript = NULL;
	}

	if (!ReadPackageInfoStr("install-script", &m_updateScript))
	{
		return false;
	}

	// make absolute path
	if (m_updateScript[0] != PATH_SEPARATOR
#ifdef WIN32
		&& !(strlen(m_updateScript) > 2 && m_updateScript[1] == ':')
#endif
		)
	{
		char filename[MAX_PATH + 100];
		snprintf(filename, sizeof(filename), "%s%c%s", g_pOptions->GetAppDir(), PATH_SEPARATOR, m_updateScript);
		free(m_updateScript);
		m_updateScript = strdup(filename);
	}

	m_messages.Clear();

	m_updateScriptController = new UpdateScriptController();
	m_updateScriptController->SetScript(m_updateScript);
	m_updateScriptController->SetBranch(branch);
	m_updateScriptController->SetAutoDestroy(true);

	m_updateScriptController->Start();

	return true;
}

bool Maintenance::CheckUpdates(char** updateInfo)
{
	char* updateInfoScript;
	if (!ReadPackageInfoStr("update-info-script", &updateInfoScript))
	{
		return false;
	}

	*updateInfo = NULL;
	UpdateInfoScriptController::ExecuteScript(updateInfoScript, updateInfo);

	free(updateInfoScript);

	return *updateInfo;
}

bool Maintenance::ReadPackageInfoStr(const char* key, char** value)
{
	char fileName[1024];
	snprintf(fileName, 1024, "%s%cpackage-info.json", g_pOptions->GetWebDir(), PATH_SEPARATOR);
	fileName[1024-1] = '\0';

	char* packageInfo;
	int packageInfoLen;
	if (!Util::LoadFileIntoBuffer(fileName, &packageInfo, &packageInfoLen))
	{
		error("Could not load file %s", fileName);
		return false;
	}

	char keyStr[100];
	snprintf(keyStr, 100, "\"%s\"", key);
	keyStr[100-1] = '\0';

	char* p = strstr(packageInfo, keyStr);
	if (!p)
	{
		error("Could not parse file %s", fileName);
		free(packageInfo);
		return false;
	}

	p = strchr(p + strlen(keyStr), '"');
	if (!p)
	{
		error("Could not parse file %s", fileName);
		free(packageInfo);
		return false;
	}

	p++;
	char* pend = strchr(p, '"');
	if (!pend)
	{
		error("Could not parse file %s", fileName);
		free(packageInfo);
		return false;
	}

	int len = pend - p;
	if (len >= sizeof(fileName))
	{
		error("Could not parse file %s", fileName);
		free(packageInfo);
		return false;
	}

	*value = (char*)malloc(len+1);
	strncpy(*value, p, len);
	(*value)[len] = '\0';

	WebUtil::JsonDecode(*value);

	free(packageInfo);

	return true;
}

bool Maintenance::VerifySignature(const char* inFilename, const char* sigFilename, const char* pubKeyFilename)
{
#ifdef HAVE_OPENSSL
	Signature signature(inFilename, sigFilename, pubKeyFilename);
	return signature.Verify();
#else
	return false;
#endif
}

void UpdateScriptController::Run()
{
	// the update-script should not be automatically terminated when the program quits
	UnregisterRunningScript();

	m_prefixLen = 0;
	PrintMessage(Message::mkInfo, "Executing update-script %s", GetScript());

	char infoName[1024];
	snprintf(infoName, 1024, "update-script %s", Util::BaseFileName(GetScript()));
	infoName[1024-1] = '\0';
	SetInfoName(infoName);

    const char* branchName[] = { "STABLE", "TESTING", "DEVEL" };
	SetEnvVar("NZBUP_BRANCH", branchName[m_branch]);

	SetEnvVar("NZBUP_RUNMODE", g_pCommandLineParser->GetDaemonMode() ? "DAEMON" : "SERVER");

	for (int i = 0; i < g_iArgumentCount; i++)
	{
		char envName[40];
		snprintf(envName, 40, "NZBUP_CMDLINE%i", i);
		infoName[40-1] = '\0';
		SetEnvVar(envName, (*g_szArguments)[i]);
	}

	char processId[20];
#ifdef WIN32
	int pid = (int)GetCurrentProcessId();
#else
	int pid = (int)getpid();
#endif
	snprintf(processId, 20, "%i", pid);
	processId[20-1] = '\0';
	SetEnvVar("NZBUP_PROCESSID", processId);

	char logPrefix[100];
	strncpy(logPrefix, Util::BaseFileName(GetScript()), 100);
	logPrefix[100-1] = '\0';
	if (char* ext = strrchr(logPrefix, '.')) *ext = '\0'; // strip file extension
	SetLogPrefix(logPrefix);
	m_prefixLen = strlen(logPrefix) + 2; // 2 = strlen(": ");

	Execute();

	g_pMaintenance->ResetUpdateController();
}

void UpdateScriptController::AddMessage(Message::EKind kind, const char* text)
{
	text = text + m_prefixLen;

	if (!strncmp(text, "[NZB] ", 6))
	{
		debug("Command %s detected", text + 6);
		if (!strcmp(text + 6, "QUIT"))
		{
			Detach();
			ExitProc();
		}
		else
		{
			error("Invalid command \"%s\" received", text);
		}
	}
	else
	{
		g_pMaintenance->AddMessage(kind, time(NULL), text);
		ScriptController::AddMessage(kind, text);
	}
}

void UpdateInfoScriptController::ExecuteScript(const char* script, char** updateInfo)
{
	detail("Executing update-info-script %s", Util::BaseFileName(script));

	UpdateInfoScriptController* scriptController = new UpdateInfoScriptController();
	scriptController->SetScript(script);

	char infoName[1024];
	snprintf(infoName, 1024, "update-info-script %s", Util::BaseFileName(script));
	infoName[1024-1] = '\0';
	scriptController->SetInfoName(infoName);

	char logPrefix[1024];
	strncpy(logPrefix, Util::BaseFileName(script), 1024);
	logPrefix[1024-1] = '\0';
	if (char* ext = strrchr(logPrefix, '.')) *ext = '\0'; // strip file extension
	scriptController->SetLogPrefix(logPrefix);
	scriptController->m_prefixLen = strlen(logPrefix) + 2; // 2 = strlen(": ");

	scriptController->Execute();

	if (scriptController->m_updateInfo.GetBuffer())
	{
		int len = strlen(scriptController->m_updateInfo.GetBuffer());
		*updateInfo = (char*)malloc(len + 1);
		strncpy(*updateInfo, scriptController->m_updateInfo.GetBuffer(), len);
		(*updateInfo)[len] = '\0';
	}

	delete scriptController;
}

void UpdateInfoScriptController::AddMessage(Message::EKind kind, const char* text)
{
	text = text + m_prefixLen;

	if (!strncmp(text, "[NZB] ", 6))
	{
		debug("Command %s detected", text + 6);
		if (!strncmp(text + 6, "[UPDATEINFO]", 12))
		{
			m_updateInfo.Append(text + 6 + 12);
		}
		else
		{
			error("Invalid command \"%s\" received from %s", text, GetInfoName());
		}
	}
	else
	{
		ScriptController::AddMessage(kind, text);
	}
}

#ifdef HAVE_OPENSSL
Signature::Signature(const char *inFilename, const char *sigFilename, const char *pubKeyFilename)
{
	m_inFilename = inFilename;
	m_sigFilename = sigFilename;
	m_pubKeyFilename = pubKeyFilename;
	m_pubKey = NULL;
}

Signature::~Signature()
{
	RSA_free(m_pubKey);
}

// Calculate SHA-256 for input file (m_szInFilename)
bool Signature::ComputeInHash()
{
    FILE* infile = fopen(m_inFilename, FOPEN_RB);
    if (!infile)
	{
		return false;
	}
    SHA256_CTX sha256;
	SHA256_Init(&sha256);
    const int bufSize = 32*1024;
    char* buffer = (char*)malloc(bufSize);
    while(int bytesRead = fread(buffer, 1, bufSize, infile))
    {
        SHA256_Update(&sha256, buffer, bytesRead);
    }
    SHA256_Final(m_inHash, &sha256);
    free(buffer);
    fclose(infile);
	return true;
}

// Read signature from file (m_szSigFilename) into memory 
bool Signature::ReadSignature()
{
	char sigTitle[256];
	snprintf(sigTitle, sizeof(sigTitle), "\"RSA-SHA256(%s)\" : \"", Util::BaseFileName(m_inFilename));
	sigTitle[256-1] = '\0';

	FILE* infile = fopen(m_sigFilename, FOPEN_RB);
    if (!infile)
	{
		return false;
	}

	bool ok = false;
	int titLen = strlen(sigTitle);
	char buf[1024];
	unsigned char* output = m_signature;
	while (fgets(buf, sizeof(buf) - 1, infile))
	{
		if (!strncmp(buf, sigTitle, titLen))
		{
			char* hexSig = buf + titLen;
			int sigLen = strlen(hexSig);
			if (sigLen > 2)
			{
				hexSig[sigLen - 2] = '\0'; // trim trailing ",
			}
			for (; *hexSig && *(hexSig+1);)
			{
				unsigned char c1 = *hexSig++;
				unsigned char c2 = *hexSig++;
				c1 = '0' <= c1 && c1 <= '9' ? c1 - '0' : 'A' <= c1 && c1 <= 'F' ? c1 - 'A' + 10 :
					'a' <= c1 && c1 <= 'f' ? c1 - 'a' + 10 : 0;
				c2 = '0' <= c2 && c2 <= '9' ? c2 - '0' : 'A' <= c2 && c2 <= 'F' ? c2 - 'A' + 10 :
					'a' <= c2 && c2 <= 'f' ? c2 - 'a' + 10 : 0;
				unsigned char ch = (c1 << 4) + c2;
				*output++ = (char)ch;
			}
			ok = output == m_signature + sizeof(m_signature);

			break;
		}
	}

	fclose(infile);
	return ok;
}

// Read public key from file (m_szPubKeyFilename) into memory
bool Signature::ReadPubKey()
{
	char* keybuf;
	int keybuflen;
	if (!Util::LoadFileIntoBuffer(m_pubKeyFilename, &keybuf, &keybuflen))
	{
		return false;
	}
	BIO* mem = BIO_new_mem_buf(keybuf, keybuflen);
	m_pubKey = PEM_read_bio_RSA_PUBKEY(mem, NULL, NULL, NULL);
	BIO_free(mem);
	free(keybuf);
	return m_pubKey != NULL;
}

bool Signature::Verify()
{
	return ComputeInHash() && ReadSignature() && ReadPubKey() &&
		RSA_verify(NID_sha256, m_inHash, sizeof(m_inHash), m_signature, sizeof(m_signature), m_pubKey) == 1;
}
#endif /* HAVE_OPENSSL */
