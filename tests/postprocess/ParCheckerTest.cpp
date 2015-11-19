/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
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

#include "catch.h"

#include "Options.h"
#include "ParChecker.h"
#include "TestUtil.h"

class ParCheckerMock: public ParChecker
{
private:
	unsigned long	CalcFileCrc(const char* filename);
protected:
	virtual bool	RequestMorePars(int blockNeeded, int* blockFound) { return false; }
	virtual EFileStatus	FindFileCrc(const char* filename, unsigned long* crc, SegmentList* segments);
public:
					ParCheckerMock();
	void			Execute();
	void			CorruptFile(const char* filename, int offset);
};

ParCheckerMock::ParCheckerMock()
{
	TestUtil::PrepareWorkingDir("parchecker");
	SetDestDir(TestUtil::WorkingDir().c_str());
}

void ParCheckerMock::Execute()
{
	TestUtil::DisableCout();
	Start();
	while (IsRunning())
	{
		usleep(10*1000);
	}
	TestUtil::EnableCout();
}

void ParCheckerMock::CorruptFile(const char* filename, int offset)
{
	std::string fullfilename(TestUtil::WorkingDir() + "/" + filename);

	FILE* file = fopen(fullfilename.c_str(), FOPEN_RBP);
	REQUIRE(file != NULL);

	fseek(file, offset, SEEK_SET);
	char b = 0;
	int written = fwrite(&b, 1, 1, file);
	REQUIRE(written == 1);

	fclose(file);
}

ParCheckerMock::EFileStatus ParCheckerMock::FindFileCrc(const char* filename, unsigned long* crc, SegmentList* segments)
{
	std::ifstream sm((TestUtil::WorkingDir() + "/crc.txt").c_str());
	std::string smfilename, smcrc;
	while (!sm.eof())
	{
		sm >> smfilename >> smcrc;
		if (smfilename == filename)
		{
			*crc = strtoul(smcrc.c_str(), NULL, 16);
			unsigned long realCrc = CalcFileCrc((TestUtil::WorkingDir() + "/" + filename).c_str());
			return *crc == realCrc ? ParChecker::fsSuccess : ParChecker::fsUnknown;
		}
	}
	return ParChecker::fsUnknown;
}

unsigned long ParCheckerMock::CalcFileCrc(const char* filename)
{
	FILE* infile = fopen(filename, FOPEN_RB);
	REQUIRE(infile);

	static const int BUFFER_SIZE = 1024 * 64;
	unsigned char* buffer = (unsigned char*)malloc(BUFFER_SIZE);
	unsigned long downloadCrc = 0xFFFFFFFF;

	int cnt = BUFFER_SIZE;
	while (cnt == BUFFER_SIZE)
	{
		cnt = (int)fread(buffer, 1, BUFFER_SIZE, infile);
		downloadCrc = Util::Crc32m(downloadCrc, buffer, cnt);
	}

	free(buffer);
	fclose(infile);

	downloadCrc ^= 0xFFFFFFFF;
	return downloadCrc;
}

TEST_CASE("Par-checker: repair not needed", "[Par][ParChecker][Slow][TestData]")
{
	Options::CmdOptList cmdOpts;
	cmdOpts.push_back("ParRepair=no");
	Options options(&cmdOpts, NULL);

	ParCheckerMock parChecker;
	parChecker.Execute();

	REQUIRE(parChecker.GetStatus() == ParChecker::psRepairNotNeeded);
	REQUIRE(parChecker.GetParFull() == true);
}

TEST_CASE("Par-checker: repair possible", "[Par][ParChecker][Slow][TestData]")
{
	Options::CmdOptList cmdOpts;
	cmdOpts.push_back("ParRepair=no");
	cmdOpts.push_back("BrokenLog=no");
	Options options(&cmdOpts, NULL);

	ParCheckerMock parChecker;
	parChecker.CorruptFile("testfile.dat", 20000);
	parChecker.Execute();

	REQUIRE(parChecker.GetStatus() == ParChecker::psRepairPossible);
	REQUIRE(parChecker.GetParFull() == true);
}

TEST_CASE("Par-checker: repair successful", "[Par][ParChecker][Slow][TestData]")
{
	Options::CmdOptList cmdOpts;
	cmdOpts.push_back("ParRepair=yes");
	cmdOpts.push_back("BrokenLog=no");
	Options options(&cmdOpts, NULL);

	ParCheckerMock parChecker;
	parChecker.CorruptFile("testfile.dat", 20000);
	parChecker.Execute();

	REQUIRE(parChecker.GetStatus() == ParChecker::psRepaired);
	REQUIRE(parChecker.GetParFull() == true);
}

TEST_CASE("Par-checker: repair failed", "[Par][ParChecker][Slow][TestData]")
{
	Options::CmdOptList cmdOpts;
	cmdOpts.push_back("ParRepair=no");
	cmdOpts.push_back("BrokenLog=no");
	Options options(&cmdOpts, NULL);

	ParCheckerMock parChecker;
	parChecker.CorruptFile("testfile.dat", 20000);
	parChecker.CorruptFile("testfile.dat", 30000);
	parChecker.CorruptFile("testfile.dat", 40000);
	parChecker.CorruptFile("testfile.dat", 50000);
	parChecker.CorruptFile("testfile.dat", 60000);
	parChecker.CorruptFile("testfile.dat", 70000);
	parChecker.CorruptFile("testfile.dat", 80000);
	parChecker.Execute();

	REQUIRE(parChecker.GetStatus() == ParChecker::psFailed);
	REQUIRE(parChecker.GetParFull() == true);
}

TEST_CASE("Par-checker: quick verification repair not needed", "[Par][ParChecker][Slow][TestData]")
{
	Options::CmdOptList cmdOpts;
	cmdOpts.push_back("ParRepair=no");
	Options options(&cmdOpts, NULL);

	ParCheckerMock parChecker;
	parChecker.SetParQuick(true);
	parChecker.Execute();

	REQUIRE(parChecker.GetStatus() == ParChecker::psRepairNotNeeded);
	REQUIRE(parChecker.GetParFull() == false);
}

TEST_CASE("Par-checker: quick verification repair successful", "[Par][ParChecker][Slow][TestData]")
{
	Options::CmdOptList cmdOpts;
	cmdOpts.push_back("ParRepair=yes");
	cmdOpts.push_back("BrokenLog=no");
	Options options(&cmdOpts, NULL);

	ParCheckerMock parChecker;
	parChecker.SetParQuick(true);
	parChecker.CorruptFile("testfile.dat", 20000);
	parChecker.Execute();

	REQUIRE(parChecker.GetStatus() == ParChecker::psRepaired);
	REQUIRE(parChecker.GetParFull() == false);
}

TEST_CASE("Par-checker: quick full verification repair successful", "[Par][ParChecker][Slow][TestData]")
{
	Options::CmdOptList cmdOpts;
	cmdOpts.push_back("ParRepair=yes");
	cmdOpts.push_back("BrokenLog=no");
	Options options(&cmdOpts, NULL);

	ParCheckerMock parChecker;
	parChecker.SetParQuick(true);
	parChecker.CorruptFile("testfile.dat", 20000);
	parChecker.CorruptFile("testfile.nfo", 100);
	parChecker.Execute();

	// All files were damaged, the full verification was performed

	REQUIRE(parChecker.GetStatus() == ParChecker::psRepaired);
	REQUIRE(parChecker.GetParFull() == true);
}

TEST_CASE("Par-checker: ignoring extensions", "[Par][ParChecker][Slow][TestData]")
{
	Options::CmdOptList cmdOpts;
	cmdOpts.push_back("ParRepair=yes");
	cmdOpts.push_back("BrokenLog=no");

	SECTION("ParIgnoreExt")
	{
		cmdOpts.push_back("ParIgnoreExt=.dat");
	}

	SECTION("ExtCleanupDisk")
	{
		cmdOpts.push_back("ExtCleanupDisk=.dat");
	}

	Options options(&cmdOpts, NULL);

	ParCheckerMock parChecker;
	parChecker.CorruptFile("testfile.dat", 20000);
	parChecker.CorruptFile("testfile.dat", 30000);
	parChecker.CorruptFile("testfile.dat", 40000);
	parChecker.CorruptFile("testfile.dat", 50000);
	parChecker.CorruptFile("testfile.dat", 60000);
	parChecker.CorruptFile("testfile.dat", 70000);
	parChecker.CorruptFile("testfile.dat", 80000);

	parChecker.Execute();

	REQUIRE(parChecker.GetStatus() == ParChecker::psRepairNotNeeded);
}
