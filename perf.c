/*
 * perf.c
 *
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 * Copyright (C) 2009-2012 Tomi Valkeinen

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int fd;
static void* fb;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static unsigned bytespp;
FILE* logfile;

struct timespec time_start = { 0, 0 }, time_end = { 0, 0 };

static void start_timing()
{
	clock_gettime(CLOCK_MONOTONIC, &time_start);
}

static unsigned long long stop_timing()
{
	clock_gettime(CLOCK_MONOTONIC, &time_end);
	return (time_end.tv_nsec - time_start.tv_nsec) + (time_end.tv_sec - time_start.tv_sec) * 1e9;
}

typedef void (*test_func)(unsigned, unsigned long long*, unsigned long long*);

static void run(const char* name, test_func func)
{
	unsigned long long usecs;
	unsigned long long pixels;
	unsigned long long pix_per_sec;
	const unsigned calib_loops = 5;
	const unsigned runtime_secs = 5;
	int loops;

	/* dunno if these work as I suppose, but I try to prevent any
	 * disk activity during test */
	fflush(stdout);
	fflush(logfile);
	sync();

	/* calibrate */
	func(calib_loops, &usecs, &pixels);
	loops = runtime_secs * 1e9 * calib_loops / usecs;

	func(loops, &usecs, &pixels);

	pix_per_sec = pixels * 1e9 / usecs;

	printf("%18llu pix, %18llu ns, %18llu pix/s, %s\n", pixels,
		usecs, pix_per_sec, name);
	fprintf(logfile, "%18llu pix, %18llu ns, %18llu pix/s, %s\n", pixels,
		usecs, pix_per_sec, name);
}

#define RUN(t) run(#t, t)

static void sequential_horiz_singlepixel_read(unsigned loops,
	unsigned long long* usecs, unsigned long long* pixels)
{
	const unsigned xres = var.xres_virtual;
	const unsigned yres = var.yres_virtual;

	unsigned l = loops;
	unsigned x, y;
	unsigned sum = 0;

	start_timing();

	while (l--) {
		__u32* p32 = fb;
		for (y = 0; y < var.yres_virtual; ++y) {
			for (x = 0; x < var.xres_virtual; ++x)
				sum += p32[x];
			p32 += (fix.line_length / sizeof(*p32));
		}
	}

	*usecs = stop_timing();
	*pixels = xres * yres * loops;

	if (sum == 0xffffffff) {
		printf("bad luck\n");
		int* p = 0;
		*p = 0;
	}
}

static void sequential_horiz_singlepixel_write(unsigned loops,
	unsigned long long* usecs, unsigned long long* pixels)
{
	const unsigned xres = var.xres_virtual;
	const unsigned yres = var.yres_virtual;

	unsigned l = loops;
	unsigned x, y;

	start_timing();

	while (l--) {
		__u32* p32 = fb;
		for (y = 0; y < var.yres_virtual; ++y) {
			for (x = 0; x < var.xres_virtual; ++x)
				p32[x] = x * y * (loops - l);
			p32 += (fix.line_length / sizeof(*p32));
		}
	}

	*usecs = stop_timing();
	*pixels = xres * yres * loops;
}

static void sequential_vert_singlepixel_read(unsigned loops,
	unsigned long long* usecs, unsigned long long* pixels)
{
	const unsigned xres = var.xres_virtual;
	const unsigned yres = var.yres_virtual;

	unsigned l = loops;
	unsigned x, y;
	unsigned sum = 0;

	start_timing();

	while (l--) {
		for (x = 0; x < var.xres_virtual; ++x) {
			__u32* p32 = ((__u32*)fb) + x;
			for (y = 0; y < var.yres_virtual; ++y) {
				sum += *p32;
				p32 += (fix.line_length / sizeof(*p32));
			}
		}
	}

	*usecs = stop_timing();
	*pixels = xres * yres * loops;

	if (sum == 0xffffffff) {
		printf("bad luck\n");
		int* p = 0;
		*p = 0;
	}
}

static void sequential_vert_singlepixel_write(unsigned loops,
	unsigned long long* usecs, unsigned long long* pixels)
{
	const unsigned xres = var.xres_virtual;
	const unsigned yres = var.yres_virtual;

	unsigned l = loops;
	unsigned x, y;

	start_timing();

	while (l--) {
		for (x = 0; x < var.xres_virtual; ++x) {
			__u32* p32 = ((__u32*)fb) + x;
			for (y = 0; y < var.yres_virtual; ++y) {
				*p32 = x * y * (loops - l);
				p32 += (fix.line_length / sizeof(*p32));
			}
		}
	}

	*usecs = stop_timing();
	*pixels = xres * yres * loops;
}

static void sequential_line_read(unsigned loops,
	unsigned long long* usecs, unsigned long long* pixels)
{
	const unsigned xres = var.xres_virtual;
	const unsigned yres = var.yres_virtual;

	unsigned l = loops;
	unsigned y;
	void* linebuf;

	linebuf = malloc(var.xres_virtual * bytespp);

	start_timing();

	while (l--) {
		void* p = fb;
		for (y = 0; y < var.yres_virtual; ++y) {
			memcpy(linebuf, p, var.xres_virtual * bytespp);
			p += fix.line_length;
		}
	}

	*usecs = stop_timing();
	*pixels = xres * yres * loops;

	free(linebuf);
}

static void sequential_line_write(unsigned loops,
	unsigned long long* usecs, unsigned long long* pixels)
{
	const unsigned xres = var.xres_virtual;
	const unsigned yres = var.yres_virtual;

	unsigned l = loops;
	unsigned y;
	void* linebuf;

	linebuf = malloc(var.xres_virtual * bytespp);
	for (y = 0; y < var.xres_virtual * bytespp; ++y)
		((unsigned char*)linebuf)[y] = y;

	start_timing();

	while (l--) {
		void* p = fb;
		for (y = 0; y < var.yres_virtual; ++y) {
			memcpy(p, linebuf, var.xres_virtual * bytespp);
			p += fix.line_length;
		}
	}

	*usecs = stop_timing();
	*pixels = xres * yres * loops;

	free(linebuf);
}

static void nonsequential_singlepixel_write(unsigned loops,
	unsigned long long* usecs, unsigned long long* pixels)
{
	const unsigned xres = var.xres_virtual;
	const unsigned yres = var.yres_virtual;

	unsigned l = loops;
	unsigned i;

	start_timing();

	while (l--) {
		for (i = 0; i < yres * xres; ++i) {
			const unsigned yparts = 4;
			const unsigned xparts = 4;
			unsigned x, y;

			y = (i % yparts) * (yres / yparts);
			y += (i / yparts) % (yres / yparts);

			x = ((i / yres) % xparts) * (xres / xparts);
			x += ((i / yres) / xparts) % (xres / xparts);

			__u32* p32 = fb + y * fix.line_length;

			p32[x] = x * y * (loops - l);
		}
	}

	*usecs = stop_timing();
	*pixels = xres * yres * loops;
}

static void nonsequential_singlepixel_read(unsigned loops,
	unsigned long long* usecs, unsigned long long* pixels)
{
	const unsigned xres = var.xres_virtual;
	const unsigned yres = var.yres_virtual;

	unsigned l = loops;
	unsigned i;
	unsigned sum = 0;

	start_timing();

	while (l--) {
		for (i = 0; i < yres * xres; ++i) {
			const unsigned yparts = 16;
			const unsigned xparts = 8;
			unsigned x, y;

			y = (i % yparts) * (yres / yparts);
			y += (i / yparts) % (yres / yparts);

			x = ((i / yres) % xparts) * (xres / xparts);
			x += ((i / yres) / xparts) % (xres / xparts);

			__u32* p32 = fb + y * fix.line_length;

			sum += p32[x];
		}
	}

	*usecs = stop_timing();
	*pixels = xres * yres * loops;

	if (sum == 0xffffffff) {
		printf("bad luck\n");
		int* p = 0;
		*p = 0;
	}
}

int main(int argc, char** argv)
{
	int fb_num = 0;
	char str[64];

	printf("perf %d.%d.%d (%s)\n", VERSION, PATCHLEVEL, SUBLEVEL,
		VERSION_NAME);

	if (argc != 3) {
		printf("usage: %s <fbnum> <logfile>\n", argv[0]);
		return 0;
	}

	fb_num = atoi(argv[1]);
	sprintf(str, "/dev/fb%d", fb_num);
	fd = open(str, O_RDWR);

	if (ioctl(fd, FBIOGET_VSCREENINFO, &var))
		return -1;

	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix))
		return -1;

	bytespp = var.bits_per_pixel / 8;

	fb = mmap(NULL, fix.line_length * var.yres_virtual,
		PROT_READ | PROT_WRITE, MAP_SHARED,
		fd, 0);
	if (fb == MAP_FAILED)
		return -1;

	logfile = fopen(argv[2], "a");
	if (logfile == NULL) {
		printf("Failed to open logfile\n");
		return -1;
	}

	fprintf(logfile, "Launch performance test\n");

	RUN(sequential_horiz_singlepixel_read);
	RUN(sequential_horiz_singlepixel_write);

	RUN(sequential_vert_singlepixel_read);
	RUN(sequential_vert_singlepixel_write);

	RUN(sequential_line_read);
	RUN(sequential_line_write);

	RUN(nonsequential_singlepixel_write);
	RUN(nonsequential_singlepixel_read);

	fprintf(logfile, "Finish performance test\n");

	close(fd);
	fclose(logfile);

	return 0;
}
