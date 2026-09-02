/*
plist.cpp

Список процессов (Ctrl-W)
*/
/*
Copyright (c) 1996 Eugene Roshal
Copyright (c) 2000 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "headers.hpp"

#include "plist.hpp"
#include "keys.hpp"
#include "help.hpp"
#include "lang.hpp"
#include "vmenu.hpp"
#include "message.hpp"
#include "config.hpp"
#include "interf.hpp"
#include "execute.hpp"
#include "dirmix.hpp"
#include "manager.hpp"

#ifdef __linux__

#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <vector>
#include <string>

static int is_pid_dir(const char *name) {
    for (const char *p = name; *p; p++)
        if (!isdigit(*p)) return 0;
    return 1;
}

static void enumerateProcesses(std::vector<std::wstring>& v, std::vector<int>& pidv) 
{
	v.clear();
	pidv.clear();
    struct dirent *entry;

    DIR *d = opendir("/proc");
    if (!d) return;

    while ((entry = readdir(d)) != NULL) {
        if (!is_pid_dir(entry->d_name))
            continue;

        int pid = atoi(entry->d_name);

        // ---- Read process name from /proc/<pid>/comm ----
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        char name[256];
        if (!fgets(name, sizeof(name), f)) {
            fclose(f);
            continue;
        }
        fclose(f);

        // ---- Read CPU and memory from /proc/<pid>/stat ----
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        f = fopen(path, "r");
        if (!f) continue;

        long utime, stime, rss;
        char commbuf[256], state;

        /*
         * /proc/<pid>/stat format:
         * pid (comm) state ... utime stime ... rss ...
         */
        fscanf(f, "%*d (%[^)]) %c", commbuf, &state);

        // Skip fields until utime (14) and stime (15)
        for (int i = 0; i < 11; i++)
            fscanf(f, "%*s");

        fscanf(f, "%ld %ld", &utime, &stime);

        // Skip fields until rss (24)
        for (int i = 0; i < 7; i++)
            fscanf(f, "%*s");

        fscanf(f, "%ld", &rss);
        fclose(f);

        long page_kb = sysconf(_SC_PAGESIZE) / 1024;
        long rss_kb = rss * page_kb;

        char* q = name + strlen(name) - 1;
        while(q > name && isspace(*q)) --q;
        q[1] = 0;

        /*
        printf("PID: %d\n", pid);
        printf("Name: %s", name);
        printf("CPU ticks: %ld (user) + %ld (system)\n", utime, stime);
        printf("RSS: %ld KB\n", rss_kb);
        printf("----\n");
        */
        long total_time = utime + stime;
        const char* cpuLoad = "?";

        if (total_time < 100)        cpuLoad = "idle";
		else if (total_time < 1000)  cpuLoad = "low";
		else if (total_time < 10000) cpuLoad = "medium";
		else                         cpuLoad = "high";

		FARString strStr;
		strStr.Format(L"%6.6d %lc %-25.25s %lc %6s %lc %6ld Kb", pid, BoxSymbols[BS_V1], name, BoxSymbols[BS_V1], cpuLoad, BoxSymbols[BS_V1], rss_kb);
		v.push_back(strStr.GetWide());
		pidv.push_back(pid);
    }

    closedir(d);
}

void ShowProcessList()
{
	std::vector<std::wstring> v;
	std::vector<int> pidv;
	enumerateProcesses(v, pidv);

	int GroupsLen = (int)v.size();
	if (GroupsLen < 1) return; // nothing to display

	MenuDataEx Groups[GroupsLen];
	for (int j = 0; j < GroupsLen; ++j) {
		Groups[j] = { v[j].c_str(), 0, 0 };
	}

	VMenu ProcList(Msg::ProcessListTitle, Groups, GroupsLen, ScrY - 4);

	ProcList.SetPosition(-1, -1, 0, 0);
	ProcList.SetFlags(VMENU_WRAPMODE | VMENU_NOTCHANGE);
	ProcList.ClearDone();

	ProcList.AssignHighlights(FALSE);
	ProcList.SetBottomTitle(Msg::ProcessListBottom);

	ProcList.Show();

	while (!ProcList.Done()) {
		FarKey Key = ProcList.ReadInput();

		switch (Key) {
		case KEY_F1:
			Help::Present(L"TaskList");
			break;
		case KEY_CTRLR:	
			ProcList.Hide();
			ProcList.DeleteItems();

			ProcList.SetPosition(-1,-1,0,0);

			enumerateProcesses(v, pidv);

			GroupsLen = (int)v.size();
			for (int j = 0; j < GroupsLen; ++j) {
				MenuItemEx item;
				item.Clear();
				item.strName = v[j].c_str();
                item.AccelKey = 0;
				item.Flags = 0;

				ProcList.AddItem(&item);
			}

			ProcList.Show();
			break;
		case KEY_NUMDEL:
		case KEY_DEL: 
			kill(pidv[ProcList.GetSelectPos()], SIGTERM);
			break;
		default:
			ProcList.ProcessInput();
			break;
		}
	}
}

#else

void ShowProcessList()
{
	farExecuteA(GetMyScriptQuoted("ps.sh").c_str(), 0);
	if (FrameManager) {
		auto *current_frame = FrameManager->GetCurrentFrame();
		if (current_frame) {
			FrameManager->RefreshFrame(current_frame);
		}
	}
/*
	for (int i = FrameManager->GetFrameCount(); i > 0; --i) {
		FrameManager->RefreshFrame(i - 1);
	}
*/
}

#endif

