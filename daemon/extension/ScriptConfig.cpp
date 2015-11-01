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
#include <sys/stat.h>
#include <set>

#include "nzbget.h"
#include "Util.h"
#include "Options.h"
#include "Log.h"
#include "ScriptConfig.h"

static const char* BEGIN_SCRIPT_SIGNATURE = "### NZBGET ";
static const char* POST_SCRIPT_SIGNATURE = "POST-PROCESSING";
static const char* SCAN_SCRIPT_SIGNATURE = "SCAN";
static const char* QUEUE_SCRIPT_SIGNATURE = "QUEUE";
static const char* SCHEDULER_SCRIPT_SIGNATURE = "SCHEDULER";
static const char* FEED_SCRIPT_SIGNATURE = "FEED";
static const char* END_SCRIPT_SIGNATURE = " SCRIPT";
static const char* QUEUE_EVENTS_SIGNATURE = "### QUEUE EVENTS:";

ScriptConfig* g_ScriptConfig = NULL;


ScriptConfig::ConfigTemplate::ConfigTemplate(Script* script, const char* templ)
{
	m_script = script;
	m_template = strdup(templ ? templ : "");
}

ScriptConfig::ConfigTemplate::~ConfigTemplate()
{
	delete m_script;
	free(m_template);
}

ScriptConfig::ConfigTemplates::~ConfigTemplates()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
}


ScriptConfig::Script::Script(const char* name, const char* location)
{
	m_name = strdup(name);
	m_location = strdup(location);
	m_displayName = strdup(name);
	m_postScript = false;
	m_scanScript = false;
	m_queueScript = false;
	m_schedulerScript = false;
	m_feedScript = false;
	m_queueEvents = NULL;
}

ScriptConfig::Script::~Script()
{
	free(m_name);
	free(m_location);
	free(m_displayName);
	free(m_queueEvents);
}

void ScriptConfig::Script::SetDisplayName(const char* displayName)
{
	free(m_displayName);
	m_displayName = strdup(displayName);
}

void ScriptConfig::Script::SetQueueEvents(const char* queueEvents)
{
	free(m_queueEvents);
	m_queueEvents = queueEvents ? strdup(queueEvents) : NULL;
}


ScriptConfig::Scripts::~Scripts()
{
	Clear();
}

void ScriptConfig::Scripts::Clear()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
	clear();
}

ScriptConfig::Script* ScriptConfig::Scripts::Find(const char* name)
{
	for (iterator it = begin(); it != end(); it++)
	{
		Script* script = *it;
		if (!strcmp(script->GetName(), name))
		{
			return script;
		}
	}

	return NULL;
}


ScriptConfig::ScriptConfig()
{
	InitScripts();
	InitConfigTemplates();
}

ScriptConfig::~ScriptConfig()
{
}

bool ScriptConfig::LoadConfig(Options::OptEntries* optEntries)
{
	// read config file
	FILE* infile = fopen(g_Options->GetConfigFilename(), FOPEN_RB);

	if (!infile)
	{
		return false;
	}

	int bufLen = (int)Util::FileSize(g_Options->GetConfigFilename()) + 1;
	char* buf = (char*)malloc(bufLen);

	while (fgets(buf, bufLen - 1, infile))
	{
		// remove trailing '\n' and '\r' and spaces
		Util::TrimRight(buf);

		// skip comments and empty lines
		if (buf[0] == 0 || buf[0] == '#' || strspn(buf, " ") == strlen(buf))
		{
			continue;
		}

		char* optname;
		char* optvalue;
		if (g_Options->SplitOptionString(buf, &optname, &optvalue))
		{
			Options::OptEntry* optEntry = new Options::OptEntry();
			optEntry->SetName(optname);
			optEntry->SetValue(optvalue);
			optEntries->push_back(optEntry);

			free(optname);
			free(optvalue);
		}
	}

	fclose(infile);
	free(buf);

	return true;
}

bool ScriptConfig::SaveConfig(Options::OptEntries* optEntries)
{
	// save to config file
	FILE* infile = fopen(g_Options->GetConfigFilename(), FOPEN_RBP);

	if (!infile)
	{
		return false;
	}

	std::vector<char*> config;
	std::set<Options::OptEntry*> writtenOptions;

	// read config file into memory array
	int bufLen = (int)Util::FileSize(g_Options->GetConfigFilename()) + 1;
	char* buf = (char*)malloc(bufLen);
	while (fgets(buf, bufLen - 1, infile))
	{
		config.push_back(strdup(buf));
	}
	free(buf);

	// write config file back to disk, replace old values of existing options with new values
	rewind(infile);
	for (std::vector<char*>::iterator it = config.begin(); it != config.end(); it++)
    {
        char* buf = *it;

		const char* eq = strchr(buf, '=');
		if (eq && buf[0] != '#')
		{
			// remove trailing '\n' and '\r' and spaces
			Util::TrimRight(buf);

			char* optname;
			char* optvalue;
			if (g_Options->SplitOptionString(buf, &optname, &optvalue))
			{
				Options::OptEntry *optEntry = optEntries->FindOption(optname);
				if (optEntry)
				{
					fputs(optEntry->GetName(), infile);
					fputs("=", infile);
					fputs(optEntry->GetValue(), infile);
					fputs("\n", infile);
					writtenOptions.insert(optEntry);
				}

				free(optname);
				free(optvalue);
			}
		}
		else
		{
			fputs(buf, infile);
		}

		free(buf);
	}

	// write new options
	for (Options::OptEntries::iterator it = optEntries->begin(); it != optEntries->end(); it++)
	{
		Options::OptEntry* optEntry = *it;
		std::set<Options::OptEntry*>::iterator fit = writtenOptions.find(optEntry);
		if (fit == writtenOptions.end())
		{
			fputs(optEntry->GetName(), infile);
			fputs("=", infile);
			fputs(optEntry->GetValue(), infile);
			fputs("\n", infile);
		}
	}

	// close and truncate the file
	int pos = (int)ftell(infile);
	fclose(infile);

	Util::TruncateFile(g_Options->GetConfigFilename(), pos);

	return true;
}

bool ScriptConfig::LoadConfigTemplates(ConfigTemplates* configTemplates)
{
	char* buffer;
	int length;
	if (!Util::LoadFileIntoBuffer(g_Options->GetConfigTemplate(), &buffer, &length))
	{
		return false;
	}
	ConfigTemplate* configTemplate = new ConfigTemplate(NULL, buffer);
	configTemplates->push_back(configTemplate);
	free(buffer);

	if (!g_Options->GetScriptDir())
	{
		return true;
	}

	Scripts scriptList;
	LoadScripts(&scriptList);

	const int beginSignatureLen = strlen(BEGIN_SCRIPT_SIGNATURE);
	const int queueEventsSignatureLen = strlen(QUEUE_EVENTS_SIGNATURE);

	for (Scripts::iterator it = scriptList.begin(); it != scriptList.end(); it++)
	{
		Script* script = *it;

		FILE* infile = fopen(script->GetLocation(), FOPEN_RB);
		if (!infile)
		{
			ConfigTemplate* configTemplate = new ConfigTemplate(script, "");
			configTemplates->push_back(configTemplate);
			continue;
		}

		StringBuilder stringBuilder;
		char buf[1024];
		bool inConfig = false;

		while (fgets(buf, sizeof(buf) - 1, infile))
		{
			if (!strncmp(buf, BEGIN_SCRIPT_SIGNATURE, beginSignatureLen) &&
				strstr(buf, END_SCRIPT_SIGNATURE) &&
				(strstr(buf, POST_SCRIPT_SIGNATURE) ||
				 strstr(buf, SCAN_SCRIPT_SIGNATURE) ||
				 strstr(buf, QUEUE_SCRIPT_SIGNATURE) ||
				 strstr(buf, SCHEDULER_SCRIPT_SIGNATURE) ||
				 strstr(buf, FEED_SCRIPT_SIGNATURE)))
			{
				if (inConfig)
				{
					break;
				}
				inConfig = true;
				continue;
			}

			bool skip = !strncmp(buf, QUEUE_EVENTS_SIGNATURE, queueEventsSignatureLen);

			if (inConfig && !skip)
			{
				stringBuilder.Append(buf);
			}
		}

		fclose(infile);

		ConfigTemplate* configTemplate = new ConfigTemplate(script, stringBuilder.GetBuffer());
		configTemplates->push_back(configTemplate);
	}

	// clearing the list without deleting of objects, which are in pConfigTemplates now 
	scriptList.clear();

	return true;
}

void ScriptConfig::InitConfigTemplates()
{
	if (!LoadConfigTemplates(&m_configTemplates))
	{
		error("Could not read configuration templates");
	}
}

void ScriptConfig::InitScripts()
{
	LoadScripts(&m_scripts);
}

void ScriptConfig::LoadScripts(Scripts* scripts)
{
	if (strlen(g_Options->GetScriptDir()) == 0)
	{
		return;
	}

	Scripts tmpScripts;
	LoadScriptDir(&tmpScripts, g_Options->GetScriptDir(), false);
	tmpScripts.sort(CompareScripts);

	// first add all scripts from m_szScriptOrder
	Tokenizer tok(g_Options->GetScriptOrder(), ",;");
	while (const char* scriptName = tok.Next())
	{
		Script* script = tmpScripts.Find(scriptName);
		if (script)
		{
			tmpScripts.remove(script);
			scripts->push_back(script);
		}
	}

	// second add all other scripts from scripts directory
	for (Scripts::iterator it = tmpScripts.begin(); it != tmpScripts.end(); it++)
	{
		Script* script = *it;
		if (!scripts->Find(script->GetName()))
		{
			scripts->push_back(script);
		}
	}

	tmpScripts.clear();

	BuildScriptDisplayNames(scripts);
}

void ScriptConfig::LoadScriptDir(Scripts* scripts, const char* directory, bool isSubDir)
{
	int bufSize = 1024*10;
	char* buffer = (char*)malloc(bufSize+1);

	const int beginSignatureLen = strlen(BEGIN_SCRIPT_SIGNATURE);
	const int queueEventsSignatureLen = strlen(QUEUE_EVENTS_SIGNATURE);

	DirBrowser dir(directory);
	while (const char* filename = dir.Next())
	{
		if (filename[0] != '.' && filename[0] != '_')
		{
			char fullFilename[1024];
			snprintf(fullFilename, 1024, "%s%s", directory, filename);
			fullFilename[1024-1] = '\0';

			if (!Util::DirectoryExists(fullFilename))
			{
				// check if the file contains pp-script-signature
				FILE* infile = fopen(fullFilename, FOPEN_RB);
				if (infile)
				{
					// read first 10KB of the file and look for signature
					int readBytes = fread(buffer, 1, bufSize, infile);
					fclose(infile);
					buffer[readBytes] = 0;

					// split buffer into lines
					Tokenizer tok(buffer, "\n\r", true);
					while (char* line = tok.Next())
					{
						if (!strncmp(line, BEGIN_SCRIPT_SIGNATURE, beginSignatureLen) &&
							strstr(line, END_SCRIPT_SIGNATURE))
						{
							bool postScript = strstr(line, POST_SCRIPT_SIGNATURE);
							bool scanScript = strstr(line, SCAN_SCRIPT_SIGNATURE);
							bool queueScript = strstr(line, QUEUE_SCRIPT_SIGNATURE);
							bool schedulerScript = strstr(line, SCHEDULER_SCRIPT_SIGNATURE);
							bool feedScript = strstr(line, FEED_SCRIPT_SIGNATURE);
							if (postScript || scanScript || queueScript || schedulerScript || feedScript)
							{
								char scriptName[1024];
								if (isSubDir)
								{
									char directory2[1024];
									snprintf(directory2, 1024, "%s", directory);
									directory2[1024-1] = '\0';
									int len = strlen(directory2);
									if (directory2[len-1] == PATH_SEPARATOR || directory2[len-1] == ALT_PATH_SEPARATOR)
									{
										// trim last path-separator
										directory2[len-1] = '\0';
									}

									snprintf(scriptName, 1024, "%s%c%s", Util::BaseFileName(directory2), PATH_SEPARATOR, filename);
								}
								else
								{
									snprintf(scriptName, 1024, "%s", filename);
								}
								scriptName[1024-1] = '\0';

								char* queueEvents = NULL;
								if (queueScript)
								{
									while (char* line = tok.Next())
									{
										if (!strncmp(line, QUEUE_EVENTS_SIGNATURE, queueEventsSignatureLen))
										{
											queueEvents = line + queueEventsSignatureLen;
											break;
										}
									}
								}

								Script* script = new Script(scriptName, fullFilename);
								script->SetPostScript(postScript);
								script->SetScanScript(scanScript);
								script->SetQueueScript(queueScript);
								script->SetSchedulerScript(schedulerScript);
								script->SetFeedScript(feedScript);
								script->SetQueueEvents(queueEvents);
								scripts->push_back(script);
								break;
							}
						}
					}
				}
			}
			else if (!isSubDir)
			{
				snprintf(fullFilename, 1024, "%s%s%c", directory, filename, PATH_SEPARATOR);
				fullFilename[1024-1] = '\0';

				LoadScriptDir(scripts, fullFilename, true);
			}
		}
	}

	free(buffer);
}

bool ScriptConfig::CompareScripts(Script* script1, Script* script2)
{
	return strcmp(script1->GetName(), script2->GetName()) < 0;
}

void ScriptConfig::BuildScriptDisplayNames(Scripts* scripts)
{
	// trying to use short name without path and extension.
	// if there are other scripts with the same short name - using a longer name instead (with ot without extension)

	for (Scripts::iterator it = scripts->begin(); it != scripts->end(); it++)
	{
		Script* script = *it;

		char shortName[256];
		strncpy(shortName, script->GetName(), 256);
		shortName[256-1] = '\0';
		if (char* ext = strrchr(shortName, '.')) *ext = '\0'; // strip file extension

		const char* displayName = Util::BaseFileName(shortName);

		for (Scripts::iterator it2 = scripts->begin(); it2 != scripts->end(); it2++)
		{
			Script* script2 = *it2;

			char shortName2[256];
			strncpy(shortName2, script2->GetName(), 256);
			shortName2[256-1] = '\0';
			if (char* ext = strrchr(shortName2, '.')) *ext = '\0'; // strip file extension

			const char* displayName2 = Util::BaseFileName(shortName2);

			if (!strcmp(displayName, displayName2) && script->GetName() != script2->GetName())
			{
				if (!strcmp(shortName, shortName2))
				{
					displayName =	script->GetName();
				}
				else
				{
					displayName =	shortName;
				}
				break;
			}
		}

		script->SetDisplayName(displayName);
	}
}
