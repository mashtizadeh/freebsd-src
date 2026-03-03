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

#include <sys/ioctl.h>

#include <curses.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <term.h>
#include <math.h>

#include <unistd.h>

#include <iomanip>
#include <ios>
#include <iostream>
#include <string>
#include <vector>

#include "display.h"

using namespace std;

enum termmode {
    TERMMODE_BASIC,
    TERMMODE_VT,
    TERMMODE_XTERM
};

static termmode tmode;
static int disp_height;
static int disp_width;
static short cf;
static short cb;

#define BORDER_HBAR		0
#define BORDER_VBAR		1
#define BORDER_TOPRIGHT		2
#define BORDER_TOPLEFT		3
#define BORDER_BOTTOMLEFT	4
#define BORDER_BOTTOMRIGHT	5
#define BORDER_TOPSPLIT		6
#define BORDER_BOTTOMSPLIT	7
#define BORDER_RIGHTSPLIT	8
#define BORDER_LEFTSPLIT	9
#define BORDER_CROSS		10

static const char **border_char;

static const char *border_vt[] = {
    "\xe2\x94\x80", "\xe2\x94\x82",
    "\xe2\x94\x8c", "\xe2\x94\x90", "\xe2\x94\x94", "\xe2\x94\x98",
    "\xe2\x94\xac", "\xe2\x94\xb4", "\xe2\x94\xa4", "\xe2\x94\x9c",
    "\xe2\x94\xbc"
};
static const char *border_basic[] = { "-", "|", "+", "+", "+", "+", "+", "+", "+", "+" };

static const char *graph_fancy[5][5] = {
    {            " ", "\xe2\xa2\x80", "\xe2\xa2\xa0", "\xe2\xa2\xb0", "\xe2\xa2\xb8" },
    { "\xe2\xa1\x80", "\xe2\xa3\x80", "\xe2\xa3\xa0", "\xe2\xa2\xb0", "\xe2\xa2\xb8" },
    { "\xe2\xa1\x84", "\xe2\xa3\x84", "\xe2\xa3\xa4", "\xe2\xa3\xb4", "\xe2\xa3\xbc" },
    { "\xe2\xa1\x86", "\xe2\xa3\x86", "\xe2\xa3\xa6", "\xe2\xa3\xb6", "\xe2\xa3\xbe" },
    { "\xe2\xa1\x87", "\xe2\xa3\x87", "\xe2\xa3\xa7", "\xe2\xa3\xb7", "\xe2\xa3\xbf" },
};
static const char *graph_vt[5] = {
    " ", "\xe2\x96\x91", "\xe2\x96\x92", "\xe2\x96\x93", "\xe2\x96\x88"
};
static const char *graph_basic[5] = { " ", " ", "*", "*", "*" };

static char tc_boldbuf[8];
static char tc_sgr0buf[8];
static char tc_afbuf[8];

static const char *TC_BOLD;
static const char *TC_SGR0;
static const char *TC_SETAF;

void
tcemit(const char *c)
{
	if (c)
		tputs(c, 1, putchar);
}

void
setup_pager()
{
	int status;
	int in[2];
	char *pager;

	pager = getenv("PAGER");
	if (pager == NULL)
		return;

	status = pipe(in);
	if (status != 0) {
		perror("pipe");
		return;
	}

	status = fork();
	if (status < 0) {
		close(in[0]);
		close(in[1]);
		perror("fork");
		return;
	}
	if (status == 0) {
		dup2(in[0], STDIN_FILENO);

		while(tcgetpgrp(STDOUT_FILENO) != getpid())
			sleep(0);

		execl(pager, pager, "-F", NULL);
	} else {
		setpgid(status, 0);
		tcsetpgrp(STDOUT_FILENO, status);

		dup2(in[1], STDOUT_FILENO);


	}
}

static void
setup_ncurses()
{
	initscr();
	if (has_colors() == TRUE) {
		start_color();
		use_default_colors();
		pair_content(0, &cf, &cb);
		init_pair(1, COLOR_RED, cb);
		init_pair(2, COLOR_YELLOW, cb);
		init_pair(3, COLOR_GREEN, cb);
	}
	cbreak();
	noecho();
	nonl();
	nodelay(stdscr, 1);
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	clear();
	getmaxyx(stdscr, disp_height, disp_width);
}

void
setup_screen(bool interactive)
{
	int status;
	const char *term;
	struct winsize wsz;

	/* Sane defaults */
	tmode = TERMMODE_BASIC;
	border_char = border_basic;
	disp_height = 80;
	disp_width = 25;

	if (isatty(STDIN_FILENO) == 0) {
		tmode = TERMMODE_XTERM;
		border_char = border_vt;
		return;
	}

	status = ioctl(STDIN_FILENO, TIOCGWINSZ, &wsz);
	if (status < 0) {
		perror("ioctl(TIOCGWINSZ)");
		return;
	}

	disp_height = wsz.ws_row;
	disp_width = wsz.ws_col;

	term = getenv("TERM");
	if (term == NULL) {
		tmode = TERMMODE_BASIC;
		border_char = border_basic;
		return;
	}

	if (strncmp(term, "xterm", 5) == 0) {
		tmode = TERMMODE_XTERM;
		border_char = border_vt;
	} else if (strncmp(term, "vt", 2) == 0) {
		tmode = TERMMODE_VT;
		border_char = border_vt;
	}

	if (interactive) {
		setup_ncurses();
	} else {
		char *buf = tc_boldbuf;
		TC_BOLD = tgetstr("md", &buf);
		buf = tc_sgr0buf;
		TC_SGR0 = tgetstr("me", &buf);
		buf = tc_afbuf;
		TC_SETAF = tgetstr("AF", &buf);
	}
}

std::string
format_siunit(uint64_t count)
{
	unsigned long index = 0;
	char prefix[] = " kMGTP";
	char buf[10];

	while (count > 10000 && index < sizeof(prefix)) {
		index++;
		count /= 1000;
	}

	if (index == 0) {
		snprintf(buf, sizeof(buf), "%lu ", count);
	} else {
		snprintf(buf, sizeof(buf), "%lu%c", count, prefix[index]);
	}

	return buf;
}

std::string
format_sample(uint64_t count, uint64_t total)
{
	char buf[20];
	string fmt = format_siunit(count);

	snprintf(buf, sizeof(buf), "%5s (%4.1f%%)", fmt.c_str(),
	    100.0 * (float)count / (float)total);

	return buf;
}

void
table::addcolumn(const std::string &c, bool alignleft) {
	cols.push_back(c);
	align.push_back(alignleft);
}

void
table::addrow(std::vector<std::string> &r) {
	rows.push_back(r);
}

void
table::print() {
	unsigned long c;
	std::vector<unsigned long> width;

	for (auto h : cols)
		width.push_back(h.length());
	for (auto r : rows) {
		for (c = 0; c < r.size(); c++) {
			if (width[c] < r[c].length())
				width[c] = r[c].length();
		}
	}

	for (c = 0; c < width.size(); c++) {
		if (align[c])
			cout << left;
		cout << setw(width[c]);
		tcemit(TC_BOLD);
		cout << cols[c];
		tcemit(TC_SGR0);
		if (c != (width.size() - 1))
			cout << " " << border_char[BORDER_VBAR] << " ";
		if (align[c])
			cout << right;
	}
	cout << endl;

	for (c = 0; c < width.size(); c++) {
		//cout << setw(width[c]) << setfill(border_char[BORDER_HBAR]) << 
		//"";
		for (unsigned long i = 0; i < width[c]; i++)
		    cout << border_char[BORDER_HBAR];
		if (c != (width.size() - 1))
			cout << border_char[BORDER_HBAR] <<
			    border_char[BORDER_CROSS] << border_char[BORDER_HBAR];
	}
	cout << setfill(' ') << endl;

	for (auto r : rows) {
		for (c = 0; c < width.size(); c++) {
			if (align[c])
				cout << left;
			cout << setw(width[c]) << r[c];
			if (c != (width.size() - 1))
				cout << " " << border_char[BORDER_VBAR] << " ";
			if (align[c])
				cout << right;
		}
		cout << endl;
	}
}

void
histogram::setxlabel(const std::string &lbl)
{
    xlabel = lbl;
}

void
histogram::setylabel(const std::string &lbl)
{
    ylabel = lbl;
}

void
histogram::addsample(int s)
{
	auto e = samples.find(s);
	if (e == samples.end())
		samples[s] = 1;
	else
		e->second++;
}

int
histogram::min()
{
	int lowest = INT_MAX;
	for (auto s : samples) {
		if (s.first < lowest)
			lowest = s.first;
	}

	return lowest;
}

int
histogram::max()
{
	int highest = INT_MIN;
	for (auto s : samples) {
		if (s.first > highest)
			highest = s.first;
	}

	return highest;
}

int
histogram::average()
{
	int64_t sum = 0;
	int64_t count = 0;

	for (auto s : samples) {
		sum += ((int64_t)s.first) * ((int64_t)s.second);
		count += (int64_t)s.second;
	}

	return sum / count;
}

void
histogram::print()
{
	int i, j;
	int min, max;
	int height, width, bwidth;
	uint64_t ymax;
	unordered_map<int, uint64_t> bins = unordered_map<int, uint64_t>();

	height = (disp_height / 2);
	width = disp_width - 6;

	/*
	 * Ensure that graphs are at least 20 high and have even width to make
	 * the rendering routine below simple.
	 */
	if (height < 20)
		height = 20;
	if (width % 2)
		width -= 1;
	bwidth = (tmode == TERMMODE_XTERM) ? (2 * width) : width;

	if (samples.begin() == samples.end()) {
		printf("Histogram does not contain samples\n");
		return;
	}

	min = samples.begin()->first;
	max = samples.begin()->first;
	for (auto s : samples) {
		if (s.first < min)
			min = s.first;
		if (s.first > max) // && s.second > 1)
			max = s.first;
	}

	if (min == max) {
		printf("Histogram does not contain enough samples\n");
		return;
	}

	min = 0;
	max = 2000;

	for (i = 0; i < bwidth; i++)
		bins[i] = 0;

	for (auto s : samples) {
		int bin = bwidth * (s.first - min) / (max - min);
		bins[bin] += s.second;
	}

	for (i = 0; i < bwidth; i++) {
		if (ymax < bins[i])
			ymax = bins[i];
	}
	for (i = 0; i < bwidth; i++) {
		float val = 4 * height * log10(bins[i]) / log10(ymax);
		bins[i] = (val < 0) ? 0 : val;
	}

	int vstart = (height - ylabel.length()) / 2;
	//printf("  %lu Samples\n", ymax);
	printf("   %s", border_char[BORDER_TOPRIGHT]);
	for (i = 0; i < width; i++) {
		printf("%s", border_char[BORDER_HBAR]);
	}
	printf("%s\n", border_char[BORDER_TOPLEFT]);
	for (i = height; i > 0; i--) {
		char vc;
		std::string str = "";

		for (j = 0; j < bwidth; j += 2) {
			uint64_t row = 4 * (i - 1);
			uint64_t val[2] = { 0, 0 };

			if (bins[j] > row) {
				val[0] = bins[j] - row;
				val[0] = (val[0] > 4) ? 4 : val[0];
			}
			if (bins[j + 1] > row) {
				val[1] = bins[j + 1] - row;
				val[1] = (val[1] > 4) ? 4 : val[1];
			}

			if (tmode == TERMMODE_XTERM) {
				str.append(graph_fancy[val[0]][val[1]]);
			} else if (tmode == TERMMODE_VT) {
				str.append(graph_vt[val[0]]);
				str.append(graph_vt[val[1]]);
			} else {
				str.append(graph_basic[val[0]]);
				str.append(graph_basic[val[1]]);
			}
		}

		if ((height - i >= vstart) && (height - i - vstart < (int)ylabel.length()))
		    vc = ylabel[height - i - vstart];
		else
		    vc = ' ';
		printf(" %c %s%s%s\n", vc, border_char[BORDER_VBAR],
		    str.c_str(), border_char[BORDER_VBAR]);
	}
	printf(" 0 %s", border_char[BORDER_BOTTOMLEFT]);
	for (i = 0; i < width; i++) {
		printf("%s", border_char[BORDER_HBAR]);
	}
	printf("%s\n", border_char[BORDER_BOTTOMRIGHT]);

	int rwidth = (width - xlabel.length() - 26) / 2;
	printf("   %-10d%*c%s%*c%10d\n", min, rwidth, ' ', xlabel.c_str(),
	    rwidth, ' ', max);
}

void
display_header(const string &msg)
{
	if (tmode == TERMMODE_BASIC) {
		printf("%s\n", msg.c_str());
	} else {
		tcemit(TC_BOLD);
		printf("%s\n", msg.c_str());
		tcemit(TC_SGR0);
	}
}

