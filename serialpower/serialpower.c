#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define TTYS	"/dev/ttyS0"
#define PIPE	"/var/run/serialpower.sock"
#define GROUP	"audio"
#define TIMEOUT	60

enum status {
	st_off,
	st_on,
	st_delayed,
};

int do_chown(int fd)
{
	struct group *grp;

	errno = 0;
	grp = getgrnam(GROUP);
	if (!grp) {
		if (!errno)
			fprintf(stderr, "Error: group " GROUP " not found.\n");
		else
			perror("Error resolving group " GROUP);
		return -1;
	}

	if (fchown(fd, 0, grp->gr_gid) < 0) {
		perror("Error chowning " PIPE);
		return -1;
	}
	return 0;
}

void sighandler(int signum __attribute__((unused)))
{
	exit(0);
}

void cleanup(void)
{
	unlink(PIPE);
}

int set_dtr(int fd, int set)
{
	int val = 0;

	if (set)
		val |= TIOCM_DTR;
	if (ioctl(fd, TIOCMSET, &val) < 0) {
		perror("WARNING: Error setting DTR");
		return -1;
	}
	return 0;
}

int main(void)
{
	int fd, pfd;
	mode_t oldmask;
	fd_set fds;
	char action;
	struct timeval timeout;
	enum status status = st_off;

	unlink(PIPE);
	oldmask = umask(0000);
	if (mkfifo(PIPE, 0620)) {
		perror("Error creating " PIPE);
		return 1;
	}
	umask(oldmask);
	atexit(cleanup);
	signal(SIGTERM, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGQUIT, sighandler);

	pfd = open(PIPE, O_RDWR | O_NONBLOCK);
	if (pfd < 0) {
		perror("Error opening " PIPE);
		return 1;
	}
	if (do_chown(pfd) < 0)
		return 1;

	fd = open(TTYS, O_RDWR);
	if (fd < 0) {
		perror("Error opening " TTYS);
		return 1;
	}
	set_dtr(fd, 0);

	FD_ZERO(&fds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	while (1) {
		FD_SET(pfd, &fds);
		if (timeout.tv_sec || timeout.tv_usec) {
			select(pfd + 1, &fds, NULL, NULL, &timeout);
			if (status == st_delayed && !FD_ISSET(pfd, &fds) &&
			    !timeout.tv_sec && !timeout.tv_usec) {
				set_dtr(fd, 0);
				status = st_off;
				continue;
			}
		} else {
			select(pfd + 1, &fds, NULL, NULL, NULL);
		}
		if (read(pfd, &action, 1) == 1) {
			switch (status) {
			case st_off:
				if (action == '1') {
					set_dtr(fd, 1);
					status = st_on;
				}
				break;
			case st_on:
				if (action == '0') {
					timeout.tv_sec = TIMEOUT;
					status = st_delayed;
				}
				break;
			case st_delayed:
				if (action == '1') {
					timeout.tv_sec = 0;
					timeout.tv_usec = 0;
					status = st_on;
				}
				break;
			}
		}
	}

	return 0;
}
