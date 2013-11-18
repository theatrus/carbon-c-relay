/*
 *  This file is part of carbon-c-relay.
 *
 *  carbon-c-relay is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  carbon-c-relay is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with carbon-c-relay.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "relay.h"
#include "carbon-hash.h"
#include "server.h"
#include "router.h"
#include "receptor.h"
#include "dispatcher.h"
#include "collector.h"

int keep_running = 1;
char relay_hostname[128];

static void
exit_handler(int sig)
{
	char *signal = "unknown signal";

	switch (sig) {
		case SIGTERM:
			signal = "SIGTERM";
			break;
		case SIGINT:
			signal = "SIGINT";
			break;
		case SIGQUIT:
			signal = "SIGQUIT";
			break;
	}
	fprintf(stdout, "caught %s, terminating...\n", signal);
	keep_running = 0;
}

int main() {
	int sock;
	char id;
	server **servers;
	dispatcher **workers;
	char workercnt = 16;
	char *routes = "testconf";
	unsigned short listenport = 2003;

	if (gethostname(relay_hostname, sizeof(relay_hostname)) < 0)
		snprintf(relay_hostname, sizeof(relay_hostname), "127.0.0.1");

	fprintf(stdout, "Starting carbon-c-relay %s (%s)\n",
		VERSION, GIT_VERSION);
	fprintf(stdout, "configuration:\n");
	fprintf(stdout, "    relay hostname = %s\n", relay_hostname);
	fprintf(stdout, "    listen port = %u\n", listenport);
	fprintf(stdout, "    workers = %d\n", workercnt);
	fprintf(stdout, "    routes configuration = %s\n", routes);
	fprintf(stdout, "\n");
	if (router_readconfig(routes) == 0)
		return 1;
	fprintf(stdout, "parsed configuration follows:\n");
	router_printconfig(stdout);
	fprintf(stdout, "\n");

	if (signal(SIGINT, exit_handler) == SIG_ERR) {
		fprintf(stderr, "failed to create SIGINT handler: %s\n",
				strerror(errno));
		return 1;
	}
	if (signal(SIGTERM, exit_handler) == SIG_ERR) {
		fprintf(stderr, "failed to create SIGTERM handler: %s\n",
				strerror(errno));
		return 1;
	}
	if (signal(SIGQUIT, exit_handler) == SIG_ERR) {
		fprintf(stderr, "failed to create SIGQUIT handler: %s\n",
				strerror(errno));
		return 1;
	}
	workers = malloc(sizeof(dispatcher *) * (workercnt + 1));
	if (workers == NULL) {
		fprintf(stderr, "failed to allocate memory for workers\n");
		return 1;
	}

	sock = bindlisten(listenport);
	if (sock < 0) {
		fprintf(stderr, "failed to bind on port %d: %s\n",
				2003, strerror(errno));
		return -1;
	}
	if (dispatch_addlistener(sock) != 0) {
		close(sock);
		fprintf(stderr, "failed to add listener\n");
		return -1;
	}
	fprintf(stdout, "listening on port %u\n", listenport);

	fprintf(stderr, "starting %d workers\n", workercnt);
	for (id = 1; id <= workercnt; id++) {
		workers[id - 1] = dispatch_new(id);
		if (workers[id - 1] == NULL) {
			fprintf(stderr, "failed to add worker %d\n", id);
			break;
		}
	}
	workers[id - 1] = NULL;
	if (id <= workercnt) {
		fprintf(stderr, "shutting down due to errors\n");
		keep_running = 0;
	}

	servers = router_getservers();
	collector_start((void **)workers, (void **)servers);

	/* workers do the work, just wait */
	while (keep_running)
		sleep(1);

	fprintf(stdout, "shutting down...\n");
	router_shutdown();
	/* since workers will be freed, stop querying the structures */
	collector_stop();
	for (id = 0; id < workercnt; id++)
		dispatch_shutdown(workers[id + 0]);
	fprintf(stdout, "%d workers stopped\n", workercnt);

	free(workers);
	return 0;
}
