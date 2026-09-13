/*
 * Physical FullMAC AF_PACKET data plane (#328).
 * Raw L2 socket open/send/recv — driver execution, not core/net orchestration.
 */
#include "wifi_fullmac_afpacket.h"

#include <errno.h>
#include <string.h>

#if defined(__linux__)

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "wifi_host_wire.h"

fl_result_t wifi_fullmac_afpacket_open(unsigned int ifindex, int *fd_out)
{
	struct fl_wifi_sockaddr_ll bind_addr;
	int fd;
	int flags;

	if (!fd_out)
		return FL_RESULT_INVAL;
	fd = socket(FL_WIFI_WIRE_AF_PACKET, FL_WIFI_WIRE_SOCK_RAW,
		    (int)htons(FL_WIFI_WIRE_ETH_P_ALL));
	if (fd < 0)
		return FL_RESULT_NOSYS;
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sll_family = FL_WIFI_WIRE_AF_PACKET;
	bind_addr.sll_protocol = htons(FL_WIFI_WIRE_ETH_P_ALL);
	bind_addr.sll_ifindex = (int)ifindex;
	if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
		close(fd);
		return FL_RESULT_NOSYS;
	}
	flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		(void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	*fd_out = fd;
	return FL_RESULT_OK;
}

void wifi_fullmac_afpacket_close(int *fd)
{
	if (!fd || *fd < 0)
		return;
	close(*fd);
	*fd = -1;
}

fl_result_t wifi_fullmac_afpacket_send(int fd, uint32_t ifindex, const uint8_t *frame, size_t len)
{
	struct fl_wifi_sockaddr_ll dest;
	ssize_t n;

	if (fd < 0 || !frame || len < 14u)
		return FL_RESULT_INVAL;
	memset(&dest, 0, sizeof(dest));
	dest.sll_family = FL_WIFI_WIRE_AF_PACKET;
	dest.sll_ifindex = (int)ifindex;
	dest.sll_halen = FL_WIFI_WIRE_ETH_ALEN;
	memcpy(dest.sll_addr, frame, FL_WIFI_WIRE_ETH_ALEN);
	n = sendto(fd, frame, len, 0, (struct sockaddr *)&dest, sizeof(dest));
	if (n < 0)
		return FL_RESULT_ERR;
	if ((size_t)n != len)
		return FL_RESULT_ERR;
	return FL_RESULT_OK;
}

fl_result_t wifi_fullmac_afpacket_recv(int fd, uint8_t *buf, size_t cap, size_t *len_out)
{
	ssize_t n;

	if (fd < 0 || !buf || !len_out || cap == 0u)
		return FL_RESULT_INVAL;
	n = recv(fd, buf, cap, MSG_DONTWAIT);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return FL_RESULT_TIMEDOUT;
		return FL_RESULT_ERR;
	}
	if (n < 14)
		return FL_RESULT_TIMEDOUT;
	*len_out = (size_t)n;
	return FL_RESULT_OK;
}

#else /* !__linux__ */

fl_result_t wifi_fullmac_afpacket_open(unsigned int ifindex, int *fd_out)
{
	(void)ifindex;
	(void)fd_out;
	return FL_RESULT_NOSYS;
}

void wifi_fullmac_afpacket_close(int *fd)
{
	(void)fd;
}

fl_result_t wifi_fullmac_afpacket_send(int fd, uint32_t ifindex, const uint8_t *frame, size_t len)
{
	(void)fd;
	(void)ifindex;
	(void)frame;
	(void)len;
	return FL_RESULT_NOSYS;
}

fl_result_t wifi_fullmac_afpacket_recv(int fd, uint8_t *buf, size_t cap, size_t *len_out)
{
	(void)fd;
	(void)buf;
	(void)cap;
	(void)len_out;
	return FL_RESULT_NOSYS;
}

#endif /* __linux__ */
