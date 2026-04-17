/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "debug_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include "log.h"
#include "str.h"

//For funky macro stuff
#define QUOTED_(x) #x
#define QUOTED(x) QUOTED_(x)


int launchTerminalDebugger(int targetPid, DebugLibsInfo* libs) {
    int childPid = fork();
    if (childPid == 0) {
        std::string targetPidStr = Str(targetPid);

        // Build multiple add-symbol-file -ex entries (one per library)
        std::string exArgs;
        for (int i = 0; i < libs->count; i++) {
            const LibSymbolInfo& s = libs->libs[i];
            if (!s.present) continue;
            char one[2048];
            snprintf(one, sizeof(one),
                     " -ex \"add-symbol-file %s %p -s .data %p -s .bss %p\"",
                     s.path, s.textAddr, s.dataAddr, s.bssAddr);
            exArgs += one;
        }


         // warn: make sure you have the following packages and the path is correct otherwise comment this
        const char* pretty =
            " -ex 'set auto-load safe-path /'"
            " -ex \"python import sys; sys.path[:0]=['/usr/share/gcc/python']\""
            " -ex \"python from libstdcxx.v6.printers import register_libstdcxx_printers; register_libstdcxx_printers(None)\""
            " -ex 'set print pretty on'"
            " -ex 'set print elements 0'"
            " -ex 'set print object on'"
            " -ex 'set pagination off'";

        std::string gdbCmd = std::string("gdb -q -p ") + targetPidStr +
            " -ex 'set confirm off'" + exArgs + pretty + 
            " -ex 'handle SIGTRAP nostop noprint'"
            " -ex 'set breakpoint pending on'"
            " -ex 'set confirm on'";

        std::string gfCmd = std::string("/home/halfblood/third-parties/gf/gf2 -q -p ") + targetPidStr +
            " -ex 'set confirm off'" + exArgs + pretty +
            " -ex 'handle SIGTRAP nostop noprint'"
            " -ex 'set breakpoint pending on'"
            " -ex 'set confirm on'";

        // Preferred: GNOME Terminal (clean UX). Use bash -lc so quotes in -ex survive.
        {
            const char* const args[] = { "gnome-terminal", "--", "bash", "-lc", gfCmd.c_str(), nullptr };
            execvp(args[0], (char* const*)args);
        }

        // Fallbacks if GNOME Terminal isn't available:
        {
            // KDE Konsole
            const char* const args1[] = { "konsole", "-e", "bash", "-lc", gdbCmd.c_str(), nullptr };
            execvp(args1[0], (char* const*)args1);
        }
        {
            // kitty
            const char* const args2[] = { "kitty", "bash", "-lc", gdbCmd.c_str(), nullptr };
            execvp(args2[0], (char* const*)args2);
        }
        {
            // Alacritty
            const char* const args3[] = { "alacritty", "-e", "bash", "-lc", gdbCmd.c_str(), nullptr };
            execvp(args3[0], (char* const*)args3);
        }
        {
            // Debian/Ubuntu generic terminal alternative
            const char* const args4[] = { "x-terminal-emulator", "-e", "bash", "-lc", gdbCmd.c_str(), nullptr };
            execvp(args4[0], (char* const*)args4);
        }

        {
            const char* const args5[] = {"xterm", "-e", gdbCmd.c_str(), nullptr};
            execvp(args5[0], (char* const*)args5);
        }

        // If we got here, no terminal was found.
        panic("No supported terminal (gnome-terminal/konsole/kitty/alacritty/x-terminal-emulator/xterm) found on PATH");
    } else {
        return childPid; // parent returns child's PID (the terminal process)
    }
}
