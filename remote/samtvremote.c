#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "samtvremotelib.h"

static int resolve(const char *addr)
{
	struct addrinfo hints = { .ai_socktype = SOCK_STREAM,
				  .ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG };
	struct addrinfo *res, *p;
	int err, fd;

	err = getaddrinfo(addr, "55000", &hints, &res);
	if (err) {
		fprintf(stderr, "Error resolving '%s': %s\n", addr, gai_strerror(err));
		exit(1);
	}
	for (p = res; p; p = p->ai_next) {
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0)
			continue;
		if (!connect(fd, p->ai_addr, p->ai_addrlen))
			break;
		close(fd);
	}
	if (!p) {
		fprintf(stderr, "Could not connect to '%s'\n", addr);
		exit(1);
	}
	return fd;
}

static char *get_local_ip(int fd)
{
	struct sockaddr_in6 sa6;
	socklen_t len = sizeof(sa6);
	struct sockaddr *sa = (struct sockaddr *)&sa6;
	void *s_addr;
	char *res;

	if (getsockname(fd, sa, &len))
		goto err;

	res = malloc(INET6_ADDRSTRLEN);
	if (sa->sa_family == AF_INET)
		s_addr = &((struct sockaddr_in *)sa)->sin_addr;
	else
		s_addr = &sa6.sin6_addr;
	if (!inet_ntop(sa->sa_family, s_addr, res, INET6_ADDRSTRLEN))
		goto err;
	return res;
err:
	fprintf(stderr, "Cannot get local IP address");
	exit(1);
}

static void comm_send(int fd, void *packet, unsigned len)
{
	if (!packet || write(fd, packet, len) < len) {
		fprintf(stderr, "Failed to send a packet\n");
		exit(1);
	}
}

static unsigned comm_read(int fd, void *packet, unsigned len)
{
	ssize_t res;

	res = read(fd, packet, len);
	if (res <= 0) {
		fprintf(stderr, "Failed to receive a reply\n");
		exit(1);
	}
	return res;
}

static unsigned communicate(int fd, void *packet, unsigned len)
{
	comm_send(fd, packet, len);
	return comm_read(fd, packet, len);
}

int main(int argc, char **argv)
{
	int fd;
	char *ip_addr;
	void *packet;
	unsigned len, recv_len;
	int res, i;

	if (argc < 3) {
		printf("Usage: %s REMOTE_ADDR COMMAND [COMMAND...]\n", argv[0]);
		printf("\nCOMMAND may be a key name or a delay in ms.\n");
		exit(1);
	}
	fd = resolve(argv[1]);
	ip_addr = get_local_ip(fd);
	samtv_set_appname("samtvremote");

	packet = samtv_auth_packet(ip_addr, "abcd", &len);
	comm_send(fd, packet, len);

	recv_len = comm_read(fd, packet, len);
	res = samtv_check_auth_response(packet, recv_len);
	if (res < 0) {
		fprintf(stderr, "Received unknown repsonse\n");
		exit(1);
	}
	if (res == 0) {
		fprintf(stderr, "Access denied\n");
		exit(1);
	}
	if (res == 2) {
		fprintf(stderr, "Waiting for authorization\n");
		exit(2);
	}

	free(packet);

	for (i = 2; i < argc; i++) {
		char buf[64];

		if (isdigit(argv[i][0])) {
			usleep(atol(argv[i]) * 1000);
			continue;
		}
		if (!strcmp(argv[i], "KEY_")) {
			strncpy(buf, argv[i], 64);
			buf[63] = '\0';
		} else {
			snprintf(buf, 64, "KEY_%s", argv[i]);
		}
		packet = samtv_key_packet(buf, &len);
		communicate(fd, packet, len);
		free(packet);
	}

	close(fd);
	return 0;
}
