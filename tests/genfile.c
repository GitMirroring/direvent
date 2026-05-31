/* genfile.c - create files for direvent testsuite
   This file is part of GNU direvent testsuite.
   Copyright (C) 2021-2026 Sergey Poznyakoff

   GNU direvent is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3 of the License, or (at your
   option) any later version.

   GNU direvent is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with direvent. If not, see <http://www.gnu.org/licenses/>. */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

char *filename;
size_t size = 4096;
struct timespec ts;

void
usage(FILE *fp)
{
	fprintf(fp, "usage: genfile [OPTIONS...]\n");
	fprintf(fp, "creates a file for the direvent test suite\n\n");
	fprintf(fp, "OPTIONS are:\n\n");
	fprintf(fp, "  -f FILENAME   name of the file to create\n");	
	fprintf(fp, "  -s SIZE       write SIZE more bytes to the file\n");
	fprintf(fp, "  -t D          sleep for D seconds before processing next option set\n\n");
	fprintf(fp, "  -h            display this help text\n\n");
	fprintf(fp, "Any number of -s and -t options can be given, separated by\n");
	fprintf(fp, "double-dashes (--), to stretch the file creation over certain time.\n\n");
	fprintf(fp, "In the absence of -s option, SIZE defaults to %zu.\n", size);
}

#ifndef SIZE_T_MAX
# define SIZE_T_MAX ((size_t)-1)
#endif

static int
ts_cmp(struct timespec const *a, struct timespec const *b)
{
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_nsec < b->tv_nsec)
		return -1;
	if (a->tv_nsec > b->tv_nsec)
		return 1;
	return 0;
}

static struct timespec
ts_add(struct timespec const *a, struct timespec const *b)
{
	struct timespec sum;

	sum.tv_sec = a->tv_sec + b->tv_sec;
	sum.tv_nsec = a->tv_nsec + b->tv_nsec;
	if (sum.tv_nsec >= 1e9) {
		++sum.tv_sec;
		sum.tv_nsec -= 1e9;
	}
	return sum;
}

static struct timespec
ts_sub(struct timespec const *a, struct timespec const *b)
{
	struct timespec diff;

	diff.tv_sec = a->tv_sec - b->tv_sec;
	diff.tv_nsec = a->tv_nsec - b->tv_nsec;
	if (diff.tv_nsec < 0)
	{
		--diff.tv_sec;
		diff.tv_nsec += 1e9;
	}

	return diff;
}

static void
set_size(char const *arg)
{
	char *p;
	unsigned long n;

	errno = 0;
	n = strtoul(arg, &p, 10);
	if (errno) {
		fprintf(stderr, "invalid size: %s\n", optarg);
		exit(2);
	}
	size = n;
	if (*p) {
		if (p[1]) {
			fprintf(stderr, "invalid size suffix: %s\n", arg);
			exit(2);
		}

#               define XB()						\
		if (SIZE_T_MAX / size < 1024) {				\
			fprintf(stderr,					\
				"size out of range: %s\n",		\
				optarg);				\
			exit(2);					\
		}							\
		size <<= 10;

		switch (*p) {
		case 'g':
		case 'G':
			XB();
			/* fall through */
		case 'm':
		case 'M':
			XB();
			/* fall through */
		case 'k':
		case 'K':
			XB();
			break;

		default:
			fprintf(stderr, "invalid size suffix: %s\n", arg);
			exit(2);
		}
	}
}

static void
set_timeout(char const *arg)
{
	char *p;
	unsigned long n;

	if (*arg == '.') {
		ts.tv_sec = 0;
		p = optarg;
	} else {
		errno = 0;
		n = strtoul(arg, &p, 10);
		if (errno || (*p && *p != '.') || n > LONG_MAX) {
			fprintf(stderr,	"invalid time interval: %s\n", arg);
			exit(2);
		}
		ts.tv_sec = n;
	}
	if (*p == '.') {
		char *q;
		size_t len;

		p++;
		errno = 0;
		n = strtoul(p, &q, 10);
		if (errno || *q || n > LONG_MAX) {
			fprintf(stderr,	"invalid time interval: %s\n", arg);
			exit(2);
		}
		len = strlen(p);
		if (len > 9)
			ts.tv_nsec = n / 1e9;
		else {
			static unsigned long f[] = {
				1e9, 1e8, 1e7, 1e6, 1e5, 1e4,
				1e3, 1e2, 10, 1
			};
			if (1e9 / n < f[len]) {
				fprintf(stderr, "invalid time interval: %s\n",
					arg);
				exit(2);
			}
			ts.tv_nsec = n * f[len];
		}
	} else
		ts.tv_nsec = 0;
}

int
main(int argc, char **argv)
{
	int c;
	FILE *fp;
	size_t i, n;
	struct timespec end, stop, diff;

	while ((c = getopt(argc, argv, "f:hst")) != EOF) {
		switch (c) {
		case 'f':
			filename = optarg;
			break;

		case 'h':
			usage(stdout);
			return 0;

		case 's':
		case 't':
			optind--;
			goto endopt;

		default:
			usage(stderr);
			return 2;
		}
	}
endopt:

	if (filename) {
		if ((fp = fopen(filename, "w")) == NULL) {
			perror(filename);
			return 1;
		}
	} else
		fp = stdout;

	argc -= optind;
	argv += optind;

	n = 0;
	do {
		while (argc) {
			char *opt = *argv++;
			char *arg;
			argc--;
			if (opt[0] == '-') {
				if (opt[1] == '-' && opt[2] == 0)
					break;
				if (opt[1] == 's' || opt[1] == 't') {
					if (opt[2] == 0) {
						if (argc-- == 0) {
							fprintf(stderr,
								"%s requires argument\n",
								opt);
							return 2;
						}
						arg = *argv++;
					} else {
						arg = opt + 2;
					}
					(opt[1] == 's' ? set_size : set_timeout)(arg);
				} else {
					fprintf(stderr,	"%s: unknown option\n",	arg);
					return 2;
				}
			} else {
				fprintf(stderr,	"%s: extra argument\n",	arg);
				return 2;
			}
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
		end = ts_add(&end, &ts);
		for (i = 0; i < size; i++, n++) {
			c = n & 0xff;
			if (fputc(c, fp) == EOF) {
				perror(filename ? filename : "stdout");
				return 1;
			}
		}
		clock_gettime(CLOCK_MONOTONIC, &stop);
		if (ts_cmp(&end, &stop) > 0) {
			diff = ts_sub(&end, &stop);
			nanosleep(&diff, NULL);
		}

		size = 0;
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
	} while (argc);
	fclose(fp);
	return 0;
}
