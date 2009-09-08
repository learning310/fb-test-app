#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <linux/fb.h>
#include <linux/omapfb.h>

#define ERROR(x) printf("fbtest error in line %s:%d: %s\n", __FUNCTION__, __LINE__, strerror(errno));

#define FBCTL(cmd, arg)			\
	if(ioctl(fd, cmd, arg) == -1) {	\
		ERROR("ioctl failed");	\
		exit(1); }

#define FBCTL0(cmd)			\
	if(ioctl(fd, cmd) == -1) {	\
		ERROR("ioctl failed");	\
		exit(1); }

struct fb_var_screeninfo var;
struct fb_fix_screeninfo fix;

int open_fb(const char* dev)
{
	int fd = -1;
	fd = open(dev, O_RDWR);
	if(fd == -1)
	{
		printf("Error opening device %s : %s\n", dev, strerror(errno));
		exit(-1);
	}

	return fd;
}

static int fb_update_window(int fd, short x, short y, short w, short h)
{
	struct omapfb_update_window uw;

	uw.x = x;
	uw.y = y;
	uw.width = w;
	uw.height = h;

	//printf("update %d,%d,%d,%d\n", x, y, w, h);
	FBCTL(OMAPFB_UPDATE_WINDOW, &uw);
	FBCTL0(OMAPFB_SYNC_GFX);

	return 0;
}

struct rect {
	short x;
	short y;
	short w;
	short h;
};

static struct rect *get_rand_rect(struct rect *r,
		short max_x, short max_y,
		short min_w, short min_h,
		short max_w, short max_h)
{
#define MIN(x,y) ( ((x) < (y)) ? (x) : (y) )

	const short max_width = MIN(max_w, max_x)+1;
	const short max_height = MIN(max_h, max_y)+1;

	if(min_w == max_x)
		r->x = 0;
	else
		r->x = rand() % (max_x - min_w);

	if(min_h == max_y)
		r->y = 0;
	else
		r->y = rand() % (max_y - min_h);

	r->w = min_w + rand() % (MIN(max_width, 1+max_x - r->x - min_w));
	r->h = min_h + rand() % (MIN(max_height, 1+max_y - r->y -  min_h));

	//	printf("Returning x = %d, y = %d, w = %d, h = %d\n", r->x, r->y, r->w, r->h);
	return r;
}

static void draw_pixel(void *fbmem, int x, int y, unsigned int color)
{
	if (var.bits_per_pixel == 16) {
		unsigned short c;
		int r = (color >> 16) & 0xff;
		int g = (color >> 8) & 0xff;
		int b = (color >> 0) & 0xff;
		unsigned short *p;

		r = r * 8 / 5;
		g = g * 8 / 6;
		b = b * 8 / 5;

		c = (r << 11) | (g << 6) | (b << 0);

		fbmem += fix.line_length * y;

		p = fbmem;

		p += x;

		*p = c;
	} else {
		unsigned int *p;

		fbmem += fix.line_length * y;

		p = fbmem;

		p += x;

		*p = color;
	}
}

static void fill_rect(int *fb, const struct rect *r)
{
	short x,y;

	const int color = rand() % (0xffffff+1);
	const int max_w = r->x + r->w;
	const int max_h = r->y + r->h;

	//printf("fill_rect %d,%d %dx%d\n", r->x, r->y, r->w, r->h);

	for(y = r->y; y < max_h; y++)
	{
		for(x = r->x; x < max_w; x++)
		{
			if(y - r->y == x - r->x)
				draw_pixel(fb, x, y, ~color & 0xffffff);
			else
				draw_pixel(fb, x, y, color);
		}
	}
}

void fill_screen(void *fbmem)
{
	int x, y;
	int color;

	color = rand() % 0xffffff;

	for (y = 0; y < var.yres_virtual; y++) {
		for (x = 0; x < var.xres_virtual; x++) {
			int c;
			if (y == 0 || x == 0 ||
					y == var.yres_virtual - 1 ||
					x == var.xres_virtual - 1)
				c = 0xff0000;
			else
				c = color;

			draw_pixel(fbmem, x, y, c);
		}
	}
}

int main(int argc, char** argv)
{
	int fd;
	struct rect r;
	int i;

	fd = open_fb("/dev/fb0");

	FBCTL(FBIOGET_VSCREENINFO, &var);
	FBCTL(FBIOGET_FSCREENINFO, &fix);

	void* ptr = mmap(0, var.yres_virtual * fix.line_length,
			PROT_WRITE | PROT_READ,
			MAP_SHARED, fd, 0);

	if(ptr == MAP_FAILED) {
		perror("mmap failed");
		exit(1);
	}

	srand((unsigned int)time(NULL) + getpid());

	fill_screen(ptr);

	for (i = 0; 1 || i < 10000; i++) {
		get_rand_rect(&r,
				var.xres_virtual, var.yres_virtual,
				2, 0,
				var.xres_virtual, var.yres_virtual);

		fill_rect(ptr, &r);
		fb_update_window(fd, r.x, r.y, r.w, r.h);
		//fb_update_window(fd, 302, 116, 445, 190);
		//usleep(10000);
	}

	return 0;
}