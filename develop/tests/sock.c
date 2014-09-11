/*#ifdef __sun || 1 *//* XXX */

#if 1

int
main() {
	puts("-lnsl!");
	puts("-lsocket!!");
	puts("-lresolv!!!");
	puts("-lol!!!!!!!!!!!!!!!!!!!!!!!!!!!");
}

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>

static void
client() {
	struct sockaddr_in	sa;
	char			buf[1024];
	int			tries = 0;
	int			s;
	int			rc;
	int			bytes_read = 0;
	
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		return;
	}	
	sa.sin_family = AF_INET;
	sa.sin_port = htons(9991);
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");

	do {
		if (connect(s, (struct sockaddr *)&sa, sizeof sa) == -1) {
			sched_yield();
		} else {
			break;
		}	
	} while (++tries < 10);	
	
	while ((rc = recv(s, buf, sizeof buf, 0)) > 0) {
		bytes_read += rc;
	}	
	printf("%d\n", bytes_read);
}

static void
server() {
	int			s, s2;
	struct sockaddr_in	sa;
	int			salen;
	int			i;
	char			buf[1024];

	memset(buf, 'x', sizeof buf);
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		return;
	}

	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_family = AF_INET;
	sa.sin_port = htons(9991);
	salen = 1;
	(void) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &salen, sizeof(int));
	salen = sizeof sa;
	if (bind(s, (struct sockaddr *)&sa, salen) == -1) {
		perror("bind");
		return;
	}
	if (listen(s, 1) == -1) {
		perror("listen");
		return;
	}
	s2 = accept(s, (struct sockaddr *)&sa, &salen);
	if (s2 == -1) {
		perror("accept");
		return;
	}

	(void) signal(SIGPIPE, SIG_IGN);
	for (i = 0; i < 128; ++i) {
		(void) send(s2, buf, sizeof buf, 0);
	}
}

int
main() {
	struct sockaddr_in	sa;


	switch (fork()) {
	case -1:
		perror("fork");
		break;
	case 0:
		server();
		_exit(0);
		break;
	default:
		client();
		(void) wait(NULL);
	}	
}

#endif

