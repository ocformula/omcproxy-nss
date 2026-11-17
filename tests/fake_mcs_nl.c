/*
 * Fake qca-mcs transport for host tests.
 *
 * The event socket is backed by a pipe so that the code under test has to
 * register it with the real event loop to see events.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <libubox/uloop.h>

#include "fake_mcs_nl.h"

struct fake_req fake_reqs[256];
int fake_req_cnt;
int fake_open_fail;
struct mcs_mdb_entry fake_mdb[32];
int fake_mdb_cnt;
int fake_overflow_once;
uint32_t fake_overflow_needed;
int fake_read_event_calls;

static int open_cnt;
static int event_pipe[2] = { -1, -1 };
static char events[8][IFNAMSIZ];
static int event_cnt, event_pos;

void fake_reset(void)
{
	if (event_pipe[0] >= 0) {
		close(event_pipe[0]);
		close(event_pipe[1]);
	}

	event_pipe[0] = event_pipe[1] = -1;
	memset(fake_reqs, 0, sizeof(fake_reqs));
	memset(fake_mdb, 0, sizeof(fake_mdb));
	fake_req_cnt = fake_open_fail = fake_mdb_cnt = 0;
	fake_overflow_once = fake_read_event_calls = 0;
	fake_overflow_needed = 0;
	open_cnt = event_cnt = event_pos = 0;
}

void fake_queue_event(const char *bridge)
{
	strncpy(events[event_cnt++], bridge, IFNAMSIZ - 1);
	if (write(event_pipe[1], "e", 1) != 1)
		perror("fake event pipe");
}

int mcs_nl_open(uint32_t *portid)
{
	if (fake_open_fail)
		return -1;

	/* The first socket carries requests, the second one events */
	if (open_cnt++ == 0) {
		*portid = FAKE_REQ_PORT;
		return open("/dev/null", O_RDONLY);
	}

	if (pipe(event_pipe))
		return -1;

	fcntl(event_pipe[0], F_SETFL, O_NONBLOCK);
	*portid = FAKE_EVENT_PORT;
	return event_pipe[0];
}

void mcs_nl_close(int fd)
{
	if (fd >= 0 && fd != event_pipe[0])
		close(fd);
}

int mcs_nl_request(int fd, uint32_t portid, int type, const char *bridge,
		void *data, uint32_t len, uint32_t *written, uint32_t *needed)
{
	struct fake_req *req = &fake_reqs[fake_req_cnt++];

	req->fd = fd;
	req->portid = portid;
	req->type = type;
	strncpy(req->bridge, bridge, IFNAMSIZ - 1);
	req->len = len;
	memcpy(req->data, data, len < sizeof(req->data) ? len : sizeof(req->data));

	if (type == MCS_MSG_GET_MDB) {
		uint32_t bytes = fake_mdb_cnt * sizeof(fake_mdb[0]);

		if (fake_overflow_once) {
			fake_overflow_once = 0;
			*written = 0;
			*needed = fake_overflow_needed;
			return MCS_STATUS_BUFFER_OVERFLOW;
		}

		if (bytes > len)
			bytes = len;

		memcpy(data, fake_mdb, bytes);
		*written = bytes;
		*needed = bytes;
	}

	return 0;
}

int mcs_nl_read_event(int fd, char bridge[IFNAMSIZ])
{
	char c;

	fake_read_event_calls++;
	if (fd != event_pipe[0])
		return -1;

	if (read(fd, &c, 1) != 1 || event_pos >= event_cnt) {
		/* Queue drained: let the test continue */
		uloop_end();
		return 0;
	}

	strcpy(bridge, events[event_pos++]);
	return 1;
}

bool mcs_nl_bridge_name(int ifindex, char name[IFNAMSIZ])
{
	if (ifindex < 100)
		return false;

	snprintf(name, IFNAMSIZ, "br%d", ifindex);
	return true;
}
