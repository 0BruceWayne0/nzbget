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
#include "SchedulerScript.h"
#include "Options.h"
#include "Log.h"
#include "Util.h"

void SchedulerScriptController::StartScript(const char* param, bool externalProcess, int taskId)
{
	char** argv = NULL;
	if (externalProcess && !Util::SplitCommandLine(param, &argv))
	{
		error("Could not execute scheduled process-script, failed to parse command line: %s", param);
		return;
	}

	SchedulerScriptController* scriptController = new SchedulerScriptController();

	scriptController->m_externalProcess = externalProcess;
	scriptController->m_script = param;
	scriptController->m_taskId = taskId;

	if (externalProcess)
	{
		scriptController->SetScript(argv[0]);
		scriptController->SetArgs((const char**)argv, true);
	}

	scriptController->SetAutoDestroy(true);

	scriptController->Start();
}

void SchedulerScriptController::Run()
{
	if (m_externalProcess)
	{
		ExecuteExternalProcess();
	}
	else
	{
		ExecuteScriptList(m_script);
	}
}

void SchedulerScriptController::ExecuteScript(ScriptConfig::Script* script)
{
	if (!script->GetSchedulerScript())
	{
		return;
	}

	PrintMessage(Message::mkInfo, "Executing scheduler-script %s for Task%i", script->GetName(), m_taskId);

	SetScript(script->GetLocation());
	SetArgs(NULL, false);

	char infoName[1024];
	snprintf(infoName, 1024, "scheduler-script %s for Task%i", script->GetName(), m_taskId);
	infoName[1024-1] = '\0';
	SetInfoName(infoName);

	SetLogPrefix(script->GetDisplayName());
	PrepareParams(script->GetName());

	Execute();

	SetLogPrefix(NULL);
}

void SchedulerScriptController::PrepareParams(const char* scriptName)
{
	ResetEnv();

	SetIntEnvVar("NZBSP_TASKID", m_taskId);

	PrepareEnvScript(NULL, scriptName);
}

void SchedulerScriptController::ExecuteExternalProcess()
{
	info("Executing scheduled process-script %s for Task%i", Util::BaseFileName(GetScript()), m_taskId);

	char infoName[1024];
	snprintf(infoName, 1024, "scheduled process-script %s for Task%i", Util::BaseFileName(GetScript()), m_taskId);
	infoName[1024-1] = '\0';
	SetInfoName(infoName);

	char logPrefix[1024];
	strncpy(logPrefix, Util::BaseFileName(GetScript()), 1024);
	logPrefix[1024-1] = '\0';
	if (char* ext = strrchr(logPrefix, '.')) *ext = '\0'; // strip file extension
	SetLogPrefix(logPrefix);

	Execute();
}
