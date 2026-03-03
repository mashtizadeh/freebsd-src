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

struct ibs_op_stats
{
	uint64_t total;

	uint64_t brret;
	uint64_t brmisp;
	uint64_t brtaken;
	uint64_t rets;

	uint64_t loads;
	uint64_t stores;
	uint64_t locked;

	uint64_t dctlbmiss;
	uint64_t dctlbhit2m;
	uint64_t dctlbhit1g;

	histogram ldlat;
};

static ibs_op_stats opstats;

static void
ibs_fetchstats(__unused uint64_t *msr, __unused uint64_t len)
{
}

static void
ibs_opstats(uint64_t *msr, __unused uint64_t len)
{
	uint64_t data = msr[PMC_MPIDX_OP_DATA];
	uint64_t data3 = msr[PMC_MPIDX_OP_DATA3];

	opstats.total += 1;

	// Branch stats
	if (data & IBS_OP_DATA_BRANCHRETIRED)
		opstats.brret += 1;
	if (data & IBS_OP_DATA_BRANCHMISPREDICTED)
		opstats.brmisp += 1;
	if (data & IBS_OP_DATA_BRANCHTAKEN)
		opstats.brtaken += 1;
	if (data & IBS_OP_DATA_RETURN)
		opstats.rets += 1;

	// Load/store stats
	if (data3 & IBS_OP_DATA3_LOAD) {
		opstats.loads += 1;
		opstats.ldlat.addsample(IBS_OP_DATA3_TO_DCLAT(data3));
	}
	if (data3 & IBS_OP_DATA3_STORE)
		opstats.stores += 1;
	if (data3 & IBS_OP_DATA3_LOCKEDOP)
		opstats.locked += 1;

	// DC TLB
	if (data3 & IBS_OP_DATA3_DCL1TLBMISS)
		opstats.dctlbmiss += 1;
	if (data3 & IBS_OP_DATA3_DCL1TLBHIT2M)
		opstats.dctlbhit2m += 1;
	if (data3 & IBS_OP_DATA3_DCL1TLBHIT1G)
		opstats.dctlbhit1g += 1;
}

static int
ibs_multipart(struct pmclog_ev_callchain *cc)
{
	int i;
	uint8_t *hdr = (uint8_t *)&cc->pl_pc[0];
	int offset = PMC_MULTIPART_HEADER_LENGTH / sizeof(uintptr_t);

	for (i = 0; i < PMC_MULTIPART_HEADER_ENTRIES; i++) {
		uint8_t type = hdr[2 * i];
		uint8_t len = hdr[2 * i + 1];

		if (type == PMC_CC_MULTIPART_NONE) {
			break;
		} else if (type == PMC_CC_MULTIPART_CALLCHAIN) {
			return (offset);
		} else if (type == PMC_CC_MULTIPART_IBS_FETCH) {
			ibs_fetchstats((uint64_t *)&cc->pl_pc[offset], len);
		} else if (type == PMC_CC_MULTIPART_IBS_OP) {
			ibs_opstats((uint64_t *)&cc->pl_pc[offset], len);
		} else {
			printf("Unsupported multipart type!\n");
		}
	}

	return (offset);
}

static int
pmc_ibs_handler(struct pmclog_parse_state *ps)
{
	struct pmclog_ev ev;

	while (pmclog_read(ps, &ev) == 0) {
		if (ev.pl_type == PMCLOG_TYPE_CALLCHAIN) {
			if (ev.pl_u.pl_cc.pl_cpuflags & PMC_CC_F_MULTIPART) {
				ibs_multipart(&ev.pl_u.pl_cc);
			}
		}
	}

	if (opstats.total) {
		printf("IBS Op Report\n\n");
		printf("Branches: %4.2f%%\n", 100.0 * opstats.brret / opstats.total);
		printf("Branch Mispredictions: %4.2f%%\n", 100.0 * opstats.brmisp / opstats.brret);
		printf("Branch Taken: %4.2f%%\n", 100.0 * opstats.brtaken / opstats.brret);
		printf("Returns: %4.2f%%\n", 100.0 * opstats.rets / opstats.total);
		printf("\n");
		printf("Loads: %4.2f%%\n", 100.0 * opstats.loads / opstats.total);
		printf("Stores: %4.2f%%\n", 100.0 * opstats.stores / opstats.total);
		printf("Locked Operations: %4.2f%%\n", 100.0 * opstats.locked / opstats.total);
		printf("\n");

		printf("Load Latency Distribution\n");
		printf("Min: %d Cycles\n", opstats.ldlat.min());
		printf("Max: %d Cycles\n", opstats.ldlat.max());
		printf("Average: %d Cycles\n", opstats.ldlat.average());
		opstats.ldlat.setxlabel("Latency (Cycles)");
		opstats.ldlat.setylabel("Frequency Log");
		opstats.ldlat.print();
	}

	return (0);
}

static struct option longopts[] = {
	{ "pid",	required_argument,	NULL,	'P' },
	{ NULL,		0,			NULL,	0 }
};

static void
usage(void)
{
	printf("Usage: pmc ibs [options] [pmclog]\n\n");
	printf("Display a summary of an IBS trace\n");
}

int
cmd_pmc_ibs(int argc, char **argv)
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

	setup_screen(false);

	status = pmc_ibs_handler(ps);

	pmclog_close(ps);

	return (status);
}
