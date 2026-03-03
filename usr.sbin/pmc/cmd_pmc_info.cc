/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Ali Mashtizadeh
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/event.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/ttycom.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <assert.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <kvm.h>
#include <libgen.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pmc.h>
#include <pmclog.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <libpmcstat.h>
#include "cmd_pmc.h"

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

#include "display.h"

using namespace std;

struct event_info {
	uint64_t		e_event;	/* Event id */
	uint64_t		e_rate;		/* Sample rate */
	string			e_name;		/* Event name */
	uint64_t		e_samples;	/* Total samples */
};

struct proc_info {
	int					p_pid;
	string					p_name;
	unordered_map<uint64_t, uint64_t>	p_counts;	/* Event counts */
};

static int
pmc_info_handler(struct pmclog_parse_state *ps)
{
	struct pmclog_ev ev;
	uint64_t sort_event;
	unordered_map<uint32_t, uint64_t> pmcid;
	unordered_map<uint64_t, event_info> pmcs;
	unordered_map<int, string> pidmap;
	unordered_map<string, proc_info> eventmap;

	sort_event = UINT64_MAX;

	while (pmclog_read(ps, &ev) == 0) {
		if (ev.pl_type == PMCLOG_TYPE_PMCALLOCATE) {
			if (sort_event == UINT64_MAX)
				sort_event = ev.pl_u.pl_a.pl_event;
			pmcid[ev.pl_u.pl_a.pl_pmcid] = ev.pl_u.pl_a.pl_event;
			pmcs[ev.pl_u.pl_a.pl_event] = event_info({ ev.pl_u.pl_a.pl_event,
				ev.pl_u.pl_a.pl_rate, ev.pl_u.pl_a.pl_evname, 0 });
		}
		if (ev.pl_type == PMCLOG_TYPE_PROC_CREATE) {
			string image = ev.pl_u.pl_pc.pl_pcomm;
			if ((ev.pl_u.pl_pc.pl_flags & P_KPROC) != 0) {
				image = "[" + image + "]";
			}

			pidmap[ev.pl_u.pl_pc.pl_pid] = image;
			if (eventmap.find(image) == eventmap.end()) {
				eventmap[image] = proc_info({ ev.pl_u.pl_pc.pl_pid,
				    image, unordered_map<uint64_t, uint64_t>() });
			}
		}
		if (ev.pl_type == PMCLOG_TYPE_CALLCHAIN) {
			uint64_t eventid = pmcid[ev.pl_u.pl_cc.pl_pmcid];

			/*if (eventid == 0)
				continue;*/

			pmcs[eventid].e_samples++;

			auto pidname = pidmap.find(ev.pl_u.pl_cc.pl_pid);
			if (pidname != pidmap.end()) {
				auto &p = eventmap[pidname->second];
				p.p_counts[eventid]++;
			}
		}
	}

	// Format and print table
	table t = table();

	t.addcolumn("Counter", true);
	t.addcolumn("Rate");
	t.addcolumn("Samples");

	for (auto &kv : pmcs) {
		vector<string> r;

		r.push_back(kv.second.e_name);
		r.push_back(format_siunit(kv.second.e_rate));
		r.push_back(format_siunit(kv.second.e_samples));

		t.addrow(r);
	}

	t.print();

	return (0);
}

static struct option longopts[] = {
	{ "byexec",	no_argument,		NULL,	'E' },
	{ "bypid",	no_argument,		NULL,	'P' },
	{ "sort",	required_argument,	NULL,	's' },
	{ NULL,		0,			NULL,	0 }
};

static void
usage(void)
{
	printf("Usage: pmc info [options] [pmclog]\n\n");
	printf("Display a summary of the trace\n");
}

int
cmd_pmc_info(int argc, char **argv)
{
	int status;
	int option, logfd;
	struct pmclog_parse_state *ps;

	while ((option = getopt_long(argc, argv, "k:f", longopts, NULL)) != -1) {
		switch (option) {
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usage();
		exit(EX_USAGE);
	}

	if ((logfd = open(argv[0], O_RDONLY)) < 0) {
		errx(EX_OSERR, "ERROR: Cannot open \"%s\" for reading: %s.", argv[0],
		    strerror(errno));
	}

	ps = static_cast<struct pmclog_parse_state*>(pmclog_open(logfd));
	if (ps == NULL) {
		errx(EX_OSERR, "ERROR: Cannot allocate pmclog parse state: %s\n",
			 strerror(errno));
	}

	status = pmc_info_handler(ps);

	pmclog_close(ps);

	return (status);
}
