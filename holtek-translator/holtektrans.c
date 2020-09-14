#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define INPUT_CNT	2
#define KBD_DEV_NAME	"/dev/input/by-id/usb-HOLTEK_e000-event-kbd"
#define MOUSE_DEV_NAME	"/dev/input/by-id/usb-HOLTEK_e000-if01-event-mouse"

#define FLAG_LSHIFT	1
#define FLAG_LCTRL	2
#define FLAG_LALT	4
#define FLAG_LMETA	8
#define FLAG_LMOUSE	16

/* HACK alert! Redefine keys with codes > 255, as X11 cannot deal with them and
 * just ignores them. Also, X11 maps PLAY and PLAYPAUSE to the same keysym, so
 * redefine PLAY to something different. */
#undef KEY_GREEN
#undef KEY_YELLOW
#undef KEY_PROGRAM
#undef KEY_INFO
#undef KEY_LIST
#undef KEY_TEXT
#undef KEY_PLAY
#define KEY_GREEN	KEY_HOMEPAGE
#define KEY_YELLOW	KEY_BOOKMARKS
#define KEY_PROGRAM	KEY_DOCUMENTS
#define KEY_INFO	KEY_CONFIG
#define KEY_LIST	KEY_OPEN
#define KEY_TEXT	KEY_WWW
#define KEY_PLAY	KEY_REWIND

struct modtable {
	unsigned short code;
	unsigned short flag;
};

struct modtable modtable[] = {
	{ KEY_LEFTSHIFT, FLAG_LSHIFT },
	{ KEY_LEFTCTRL, FLAG_LCTRL },
	{ KEY_LEFTALT, FLAG_LALT },
	{ KEY_LEFTMETA, FLAG_LMETA },
};

struct xtable {
	unsigned short flags;
	unsigned short code;
	unsigned short result;
};

struct xtable xtable[] = {
	{ FLAG_LCTRL, KEY_I, KEY_GREEN },
	{ FLAG_LALT, KEY_TAB, KEY_YELLOW },
	{ FLAG_LCTRL, KEY_G, KEY_PROGRAM },
	{ FLAG_LALT | FLAG_LMETA, KEY_ENTER, KEY_MENU },
	{ 0, BTN_RIGHT, KEY_INFO },
	{ 0, KEY_F8, KEY_MUTE },
	{ FLAG_LCTRL, KEY_O, KEY_LIST },
	{ FLAG_LCTRL, KEY_R, KEY_RECORD },
	{ FLAG_LCTRL | FLAG_LSHIFT, KEY_P, KEY_PLAY },
	{ FLAG_LCTRL | FLAG_LSHIFT, KEY_S, KEY_STOPCD },
	{ FLAG_LCTRL, KEY_P, KEY_PLAYPAUSE },
	{ FLAG_LCTRL | FLAG_LSHIFT, KEY_T, KEY_TEXT },
};

unsigned short codes[] = {
	KEY_GREEN, KEY_YELLOW, KEY_PROGRAM, KEY_MENU, KEY_INFO, KEY_VOLUMEUP,
	KEY_VOLUMEDOWN, KEY_ENTER, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	KEY_PAGEUP, KEY_PAGEDOWN, KEY_BACKSPACE, KEY_MUTE, KEY_LIST,
	KEY_RECORD, KEY_PLAY, KEY_STOPCD, KEY_PREVIOUSSONG, KEY_PLAYPAUSE,
	KEY_NEXTSONG, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8,
	KEY_9, KEY_0, KEY_TEXT,
};

struct input_dev {
	int fd;
	const char *dev;
	unsigned short mod_flags;
	unsigned short pushed;
};

#define ARRAY_SIZE(a)	((int)(sizeof(a) / sizeof(*a)))

static void error(const char *msg, ...)
{
	va_list ap;
	char *err;

	err = strerror(errno);
	va_start(ap, msg);
	printf("ERROR: ");
	vprintf(msg, ap);
	printf(": %s\n", err);
	va_end(ap);
	exit(1);
}

static void open_input(const char *dev, struct input_dev *input)
{
	memset(input, 0, sizeof(*input));
	input->dev = dev;
	input->fd = open(dev, O_RDONLY | O_NONBLOCK);
	if (input->fd < 0)
		error("Cannot open %s", dev);
	if (ioctl(input->fd, EVIOCGRAB, (void *)1) < 0)
		error("Cannot grab %s", dev);
}

static void check(int res)
{
	if (res < 0)
		error("ioctl failed");
}

static int open_output(void)
{
	int fd, ret, i;
	struct uinput_user_dev ui;

	fd = open("/dev/uinput", O_WRONLY);
	if (fd < 0)
		error("Cannot open uinput device");
	check(ioctl(fd, UI_SET_EVBIT, EV_KEY));
	check(ioctl(fd, UI_SET_EVBIT, EV_SYN));
	for (i = 0; i < ARRAY_SIZE(codes); i++)
		check(ioctl(fd, UI_SET_KEYBIT, codes[i]));
	memset(&ui, 0, sizeof(ui));
	strncpy(ui.name, "translated-remote", UINPUT_MAX_NAME_SIZE);
	ret = write(fd, &ui, sizeof(ui));
	if (ret < 0)
		error("Cannot write to uinput device");
	check(ioctl(fd, UI_DEV_CREATE));
	return fd;
}

static int process_one_event(struct input_dev *input, int output)
{
	struct input_event ev;
	int len, i;

	len = read(input->fd, &ev, sizeof(ev));
	if (!len || (len < 0 && errno == EAGAIN))
		return 0;
	if (len < (int)sizeof(ev))
		error("Error reading from %s", input->dev);
	if (ev.type != EV_KEY)
		/* don't propagate mouse movements */
		return 1;
	if (ev.value == 2)
		/* ignore key autorepeats */
		return 1;
	/* was the key a modifier? */
	for (i = 0; i < ARRAY_SIZE(modtable); i++) {
		if (ev.code == modtable[i].code) {
			if (ev.value == 1)
				/* keypress */
				input->mod_flags |= modtable[i].flag;
			else
				/* release */
				input->mod_flags &= ~modtable[i].flag;
			return 1;
		}
	}
	/* try to translate the key */
	for (i = 0; i < ARRAY_SIZE(xtable); i++) {
		if (input->mod_flags != xtable[i].flags ||
		    ev.code != xtable[i].code)
			continue;
		if (ev.value == 1)
			/* keypress */
			/* Save the pushed button for correct release
			 * translation (as the remote first releases
			 * the modifier keys and only after that the
			 * main key). */
			input->pushed = xtable[i].result;
		else
			/* release */
			/* If there's no modifier key for the source
			 * event, we match here and have to reset the
			 * stored pushed key. */
			input->pushed = 0;
		ev.code = xtable[i].result;
		goto out;
	}
	if (ev.value == 0 && input->pushed) {
		ev.code = input->pushed;
		input->pushed = 0;
	}
out:
	if (write(output, &ev, sizeof(ev)) < (int)sizeof(ev))
		error("Error writing the event");
	ev.type = EV_SYN;
	ev.code = SYN_REPORT;
	ev.value = 0;
	if (write(output, &ev, sizeof(ev)) < (int)sizeof(ev))
		error("Error writing the event");
	return 1;
}

static void process_events(struct input_dev *input, int output)
{
	while (process_one_event(input, output))
		;
}

int main(void)
{
	struct input_dev input[INPUT_CNT];
	fd_set fds;
	int output;
	int i, n;

	open_input(KBD_DEV_NAME, input + 0);
	open_input(MOUSE_DEV_NAME, input + 1);
	output = open_output();

	FD_ZERO(&fds);
	while (1) {
		n = 0;
		for (i = 0; i < INPUT_CNT; i++) {
			FD_SET(input[i].fd, &fds);
			if (n < input[i].fd)
				n = input[i].fd;
		}
		if (select(n + 1, &fds, NULL, NULL, NULL) < 0)
			error("Select failed");
		for (i = 0; i < INPUT_CNT; i++)
			if (FD_ISSET(input[i].fd, &fds))
				process_events(input + i, output);
	}
}
