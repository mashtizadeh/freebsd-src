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
#include <unordered_set>

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

/*
static int
pmc_system_handler(struct pmclog_parse_state *ps)
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

			pmcs[eventid].e_samples++;

			auto pidname = pidmap.find(ev.pl_u.pl_cc.pl_pid);
			if (pidname != pidmap.end()) {
				auto &p = eventmap[pidname->second];
				p.p_counts[eventid]++;
			}
		}
	}

	vector<proc_info> pi;

	pi.reserve(eventmap.size());
	for (const auto &kv : eventmap)
		pi.push_back(kv.second);
	std::sort(pi.begin(), pi.end(), [sort_event](auto &a, auto &b)
	    { return (a.p_counts[sort_event] > b.p_counts[sort_event]); });

	// Print header
	table t = table();
	t.addcolumn("Program", true);
	for (auto &p : pmcs)
		t.addcolumn(p.second.e_name);

	for (auto &p : pi) {
		bool nonzero;
		vector<string> r;

		nonzero = false;
		r.push_back(p.p_name);
		for (auto &e : pmcs) {
			string samp = format_sample(
			    p.p_counts[e.second.e_event] * e.second.e_rate,
			    e.second.e_samples * e.second.e_rate);
			r.push_back(samp);
			if (p.p_counts[e.second.e_event] != 0)
				nonzero = true;
		}
		if (nonzero)
			t.addrow(r);
	}
	t.print();

	return (0);
}*/

struct func_info
{
	std::string image;
	std::string func;
	uint64_t samples;
	uint64_t ksamples;
};

static uint64_t total = 0;
static unordered_map<string, func_info> eventmap = unordered_map<string, func_info>();

static void
addsample(const std::string &image, const std::string &func, bool user)
{
	string key = image + "!!" + func;
	string fimage;

	if (user)
		fimage = image;
	else
		fimage = "[" + image + "]";

	auto e = eventmap.find(key);
	if (e == eventmap.end())
		eventmap[key] = func_info({ fimage, func, 0, 0 });

	if (user)
		eventmap[key].samples++;
	else
		eventmap[key].ksamples++;

	total++;
}

static void
print()
{
	vector<func_info> pi;

	pi.reserve(eventmap.size());
	for (const auto &kv : eventmap)
		pi.push_back(kv.second);
	std::sort(pi.begin(), pi.end(), [](auto &a, auto &b)
	    { return ((a.samples + a.ksamples) > (b.samples + b.ksamples)); });

	// Print header
	table t = table();
	t.addcolumn("Image", true);
	t.addcolumn("Function", true);
	t.addcolumn("Samples");

	for (auto &p : pi) {
		vector<string> r;

		r.push_back(p.image);
		r.push_back(p.func);
		string samp = format_sample(p.samples, total);
		string ksamp = format_sample(p.ksamples, total);
		r.push_back(samp + " / " + ksamp);

		t.addrow(r);
	}
	t.print();
}


static unordered_set<int> pidset = unordered_set<int>();
static string filter_exec = "";
static bool filter_kernel = false;
static bool filter_user = false;
static struct pmcstat_process *kproc;

static void
process(struct pmcstat_process *pp, __unused struct pmcstat_pmcrecord *rec,
	uint32_t nsamples, uintfptr_t *cc,
	int usermode, __unused uint32_t cpu)
{
	bool user;
	int msb;
	uint32_t i;
	uintfptr_t pc, loadaddress;
	struct pmcstat_pcmap *ppm;
	struct pmcstat_image *pi;
	struct pmcstat_symbol *sym;
	const char *name, *fname;

	// Filter by exec or pid
	if (!filter_kernel && (pidset.find(pp->pp_pid) == pidset.end())) {
		ppm = TAILQ_FIRST(&pp->pp_map);
		if (ppm != NULL) {
			pi = ppm->ppm_image;
			name = pmcstat_string_unintern(pi->pi_name);
			if (filter_exec == name)
				pidset.insert(pp->pp_pid);
		}
	}

	for (i = 0; i < nsamples; i++) {
		msb = 8 * sizeof(void *) - 1; 
		pc = cc[i];

		if (usermode != 0)
			user = true;
		else
			user = ((pc >> msb) == 0);

		if (filter_kernel && user)
			return;
		if (filter_user && !user)
			return;

		ppm = pmcstat_process_find_map(user ? pp : kproc, pc);
		if (ppm == NULL)
			return;

		pi = ppm->ppm_image;
		loadaddress = ppm->ppm_lowpc + pi->pi_vaddr - pi->pi_start;
		pc -= loadaddress;

		sym = pmcstat_symbol_search(pi, pc);
		if (sym != NULL) {
			name = pmcstat_string_unintern(pi->pi_name);
			fname = pmcstat_string_unintern(sym->ps_name);
			addsample(name, fname, user);
		}
	}
}

static struct option longopts[] = {
	{ "exec",	required_argument,	NULL,	'e' },
	{ "pid",	required_argument,	NULL,	'p' },
	{ "kernel",	no_argument,		NULL,	'k' },
	{ NULL,		0,			NULL,	0 }
};

static void
usage(void)
{
	printf("Usage: pmc program [options] [pmclog]\n\n");
	printf("Display a summary of the per-process samples collected\n");
	printf("Options:\n");
	printf("\t--exec	Filter by executable\n");
	printf("\t--pid		Filter by PID\n");
	printf("\t--kernel	Only include kernel events\n");
	printf("\t--user	Only include user events\n");
}

static struct pmc_plugins plugins[] = {
	{
		.pl_name = "none",
		.pl_configure = NULL,
		.pl_init = NULL,
		.pl_shutdown = NULL,
		.pl_process = NULL,
		.pl_initimage = NULL,
		.pl_shutdownimage = NULL,
		.pl_newpmc = NULL,
		.pl_topdisplay = NULL,
		.pl_topkeypress = NULL
	},
	{
		.pl_name = "program",
		.pl_configure = NULL,
		.pl_init = NULL,
		.pl_shutdown = NULL,
		.pl_process = process,
		.pl_initimage = NULL,
		.pl_shutdownimage = NULL,
		.pl_newpmc = NULL,
		.pl_topdisplay = NULL,
		.pl_topkeypress = NULL
	},
	{
		.pl_name = NULL,
		.pl_configure = NULL,
		.pl_init = NULL,
		.pl_shutdown = NULL,
		.pl_process = NULL,
		.pl_initimage = NULL,
		.pl_shutdownimage = NULL,
		.pl_newpmc = NULL,
		.pl_topdisplay = NULL,
		.pl_topkeypress = NULL
	}
};

int
cmd_pmc_program(int argc, char **argv)
{
	int option, logfd;
	struct pmclog_parse_state *ps;

	while ((option = getopt_long(argc, argv, "p:e:ku", longopts, NULL)) != -1) {
		switch (option) {
		case 'p':
			pidset.insert(atoi(optarg));
			break;
		case 'e':
			filter_exec = optarg;
			break;
		case 'k':
			filter_kernel = true;
			break;
		case 'u':
			filter_user = true;
			break;
		case '?':
		default:
			usage();
			exit(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usage();
		exit(EX_USAGE);
	}

	if (filter_kernel && filter_user) {
		printf("Options '-k' and '-u' are exclusive!\n");
		usage();
		exit(EX_USAGE);
	}

	setup_screen(false);

	if ((logfd = open(argv[0], O_RDONLY)) < 0) {
		errx(EX_OSERR, "ERROR: Cannot open \"%s\" for reading: %s.", argv[0],
		    strerror(errno));
	}

	ps = static_cast<struct pmclog_parse_state*>(pmclog_open(logfd));
	if (ps == NULL) {
		errx(EX_OSERR, "ERROR: Cannot allocate pmclog parse state: %s\n",
			 strerror(errno));
	}

	struct pmcstat_args args;
	struct pmcstat_stats stats;
	int mergepmc = 0;
	int npmcs;
	int speriod;

	memset(&args, 0, sizeof(args));
	args.pa_flags = FLAG_DO_ANALYSIS;
	args.pa_logparser = ps;
	args.pa_fsroot = "";
	args.pa_pplugin = 0;
	args.pa_plugin = 1;
	cpuset_t cpumask;
	CPU_FILL(&cpumask);
	args.pa_cpumask = cpumask;

	memset(&stats, 0, sizeof(stats));

	pmcstat_initialize_logging(&kproc, &args, plugins, &npmcs, &mergepmc);

	pmcstat_analyze_log(&args, plugins, &stats, kproc,
	    mergepmc, &npmcs, &speriod);

	print();

	pmclog_close(ps);

	return (0);
}
