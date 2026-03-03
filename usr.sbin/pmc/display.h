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

void setup_screen(bool interactive);

/* Formatting */
std::string format_siunit(uint64_t count);
std::string format_sample(uint64_t count, uint64_t total);

/* Printing */
void display_header(const std::string &msg);

class table
{
public:
	table() { }
	~table() { }
	void addcolumn(const std::string &c, bool alignleft = false);
	void addrow(std::vector<std::string> &r);
	void print();
private:
	std::vector<std::string> cols;
	std::vector<bool> align;
	std::vector<std::vector<std::string>> rows;
};

class histogram
{
public:
	histogram() { }
	~histogram() { }
	void setxlabel(const std::string &lbl);
	void setylabel(const std::string &lbl);
	void addsample(int s);
	int min();
	int max();
	int average();
	void print();
private:
	std::string xlabel;
	std::string ylabel;
	std::unordered_map<int, uint64_t> samples;
};

