#ifndef _sockutil_h_
#define _sockutil_h_

#include "sys/sock.h"
#include <assert.h>

#if defined(OS_WINDOWS)
#define SOCKET_TIMEDOUT -WSAETIMEDOUT
#define iov_base buf  
#define iov_len  len 
#else
#define SOCKET_TIMEDOUT -ETIMEDOUT
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 6031) // warning C6031: Return value ignored: 'snprintf'
#endif

/// @return >=0-socket, <0-socket_error(by socket_geterror())
static inline socket_t socket_tcp_listen(IN const char* ipv4_or_ipv6_or_dns, IN int port, IN int backlog);
static inline socket_t socket_udp_bind(IN const char* ipv4_or_ipv6_or_dns, IN int port);

/// @Notice: need restore block status
/// @param[in] timeout: ms, <0-forever
/// @return >=0-ok, <0-error(by socket_geterror())
static inline int socket_connect_by_time(IN socket_t sock, IN const struct sockaddr* addr, IN socklen_t addrlen, IN int timeout);
/// @return >=0-socket, <0-socket_invalid(by socket_geterror())
static inline socket_t socket_connect_host(IN const char* ipv4_or_ipv6_or_dns, IN u_short port, IN int timeout); // timeout: ms, <0-forever
static inline socket_t socket_accept_by_time(IN socket_t socket, OUT struct sockaddr_storage* addr, OUT socklen_t* addrlen, IN int timeout); // timeout: <0-forever

/// @return 0-ok, <0-socket_error(by socket_geterror())
static inline int socket_bind_any(IN socket_t sock, IN u_short port);

/// @return >0-sent/received bytes, SOCKET_TIMEDOUT-timeout, <0-error(by socket_geterror()), 0-connection closed(recv only)
static inline int socket_send_by_time(IN socket_t sock, IN const void* buf, IN size_t len, IN int flags, IN int timeout); // timeout: ms, <0-forever
static inline int socket_send_all_by_time(IN socket_t sock, IN const void* buf, IN size_t len, IN int flags, IN int timeout); // timeout: ms, <0-forever
static inline int socket_recv_by_time(IN socket_t sock, OUT void* buf, IN size_t len, IN int flags, IN int timeout); // timeout: ms, <0-forever
static inline int socket_recv_all_by_time(IN socket_t sock, OUT void* buf, IN size_t len, IN int flags, IN int timeout);  // timeout: ms, <0-forever
static inline int socket_send_v_all_by_time(IN socket_t sock, IN socket_bufvec_t* vec, IN size_t n, IN int flags, IN int timeout); // timeout: ms, <0-forever

//////////////////////////////////////////////////////////////////////////
/// socket connect
//////////////////////////////////////////////////////////////////////////

/// @Notice: need restore block status
/// @param[in] timeout: <0-forever
/// @return >=0-ok, <0-error(by socket_geterror())
static inline int socket_connect_by_time(IN socket_t sock, IN const struct sockaddr* addr, IN socklen_t addrlen, IN int timeout)
{
	int r;
#if !defined(OS_WINDOWS)
	int errcode = 0;
	int errlen = sizeof(errcode);
#endif
	socket_setnonblock(sock, 1);
	r = socket_connect(sock, addr, addrlen);
	assert(r <= 0);
#if defined(OS_WINDOWS)
	if (0 != r && WSAEWOULDBLOCK == WSAGetLastError())
#else
	if (0 != r && EINPROGRESS == errno)
#endif
	{
		// check timeout
		r = socket_select_write(sock, timeout);
#if defined(OS_WINDOWS)
		// r = socket_setnonblock(sock, 0);
		return 1 == r ? 0 : SOCKET_TIMEDOUT;
#else
		if (1 == r)
		{
			r = getsockopt(sock, SOL_SOCKET, SO_ERROR, (void*)&errcode, (socklen_t*)&errlen);
			if (0 == r)
				r = -errcode;
		}
		else
		{
			r = SOCKET_TIMEDOUT;
		}
#endif
	}

	// r = socket_setnonblock(sock, 0);
	return r;
}

/// @param[in] timeout ms, -1==infinite
static inline socket_t socket_connect_host(IN const char* ipv4_or_ipv6_or_dns, IN u_short port, IN int timeout)
{
	int r;
	socket_t sock;
	char portstr[16];
	struct addrinfo hints, *addr, *ptr;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	//	hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
	snprintf(portstr, sizeof(portstr), "%hu", port);
	r = getaddrinfo(ipv4_or_ipv6_or_dns, portstr, &hints, &addr);
	if (0 != r)
		return socket_invalid;

	r = -1; // not found
	for (ptr = addr; 0 != r && ptr != NULL; ptr = ptr->ai_next)
	{
		sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (socket_invalid == sock)
			continue;

		socket_addr_setport(ptr->ai_addr, ptr->ai_addrlen, port); // fixed ios getaddrinfo don't set port if nodename is ipv4 address

		if (timeout < 0)
			r = socket_connect(sock, ptr->ai_addr, ptr->ai_addrlen);
		else
			r = socket_connect_by_time(sock, ptr->ai_addr, ptr->ai_addrlen, timeout);

		if (0 != r)
			socket_close(sock);
	}

	freeaddrinfo(addr);
	return 0 == r ? sock : socket_invalid;
}


//////////////////////////////////////////////////////////////////////////
/// socket bind
//////////////////////////////////////////////////////////////////////////
static inline int socket_bind_any(IN socket_t sock, IN u_short port)
{
	int r;
	int domain;
	r = socket_getdomain(sock, &domain);
	if (0 != r)
		return r;

	if (AF_INET == domain)
	{
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = INADDR_ANY;
		return socket_bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	}
	else if (AF_INET6 == domain)
	{
		struct sockaddr_in6 addr6;
		memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons(port);
		addr6.sin6_addr = in6addr_any;
		return socket_bind(sock, (struct sockaddr*)&addr6, sizeof(addr6));
	}
	else
	{
		assert(0);
		return socket_error;
	}
}

//////////////////////////////////////////////////////////////////////////
/// TCP/UDP server socket
//////////////////////////////////////////////////////////////////////////

/// create a new TCP socket, bind, and listen
/// @param[in] ip socket bind local address, NULL-bind any address
/// @param[in] port bind local port
/// @param[in] backlog the maximum length to which the queue of pending connections for socket may grow
/// @return socket_invalid-error, use socket_geterror() to get error code, other-ok 
static inline socket_t socket_tcp_listen(IN const char* ipv4_or_ipv6_or_dns, IN int port, IN int backlog)
{
	int r;
	socket_t sock;
	char portstr[22];
	struct addrinfo hints, *addr, *ptr;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
	snprintf(portstr, sizeof(portstr), "%d", port);
	r = getaddrinfo(ipv4_or_ipv6_or_dns, portstr, &hints, &addr);
	if (0 != r)
		return socket_invalid;

	r = -1; // not found
	for (ptr = addr; 0 != r && ptr != NULL; ptr = ptr->ai_next)
	{
#if !defined(IPV6)
		if (AF_INET6 == ptr->ai_family)
			continue;
#endif
		sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (socket_invalid == sock)
			continue;

		// reuse addr
		socket_setreuseaddr(sock, 1);

		// restrict IPv6 only
#if defined(OS_LINUX)
		if (AF_INET6 == ptr->ai_addr->sa_family)
			socket_setipv6only(sock, 1);
#endif

		r = socket_bind(sock, ptr->ai_addr, ptr->ai_addrlen);
		if (0 == r)
			r = socket_listen(sock, backlog);

		if (0 != r)
			socket_close(sock);
	}

	freeaddrinfo(addr);
	return 0 == r ? sock : socket_invalid;
}

static inline socket_t socket_udp_bind(IN const char* ipv4_or_ipv6_or_dns, IN int port)
{
	int r;
	socket_t sock;
	char portstr[16];
	struct addrinfo hints, *addr, *ptr;

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
	snprintf(portstr, sizeof(portstr), "%d", port);
	r = getaddrinfo(ipv4_or_ipv6_or_dns, portstr, &hints, &addr);
	if (0 != r)
		return socket_invalid;

	r = -1; // not found
	for (ptr = addr; 0 != r && ptr != NULL; ptr = ptr->ai_next)
	{
#if !defined(IPV6)
		if (AF_INET6 == ptr->ai_family)
			continue;
#endif
		sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (socket_invalid == sock)
			continue;

		// reuse addr
		//		socket_setreuseaddr(sock, 1);

		// restrict IPv6 only
#if defined(OS_LINUX)
		if (AF_INET6 == ptr->ai_addr->sa_family)
			socket_setipv6only(sock, 1);
#endif

		r = socket_bind(sock, ptr->ai_addr, ptr->ai_addrlen);
		if (0 != r)
			socket_close(sock);
	}

	freeaddrinfo(addr);
	return 0 == r ? sock : socket_invalid;
}

/// wait for client connection
/// @param[in] socket server socket(must be bound and listening)
/// @param[out] addr struct sockaddr_in for IPv4
/// @param[out] addrlen addr length in bytes
/// @param[in] timeout timeout in millisecond
/// @return socket_invalid-error, use socket_geterror() to get error code, other-ok  
static inline socket_t socket_accept_by_time(IN socket_t socket, OUT struct sockaddr_storage* addr, OUT socklen_t* addrlen, IN int timeout)
{
	int ret;
	
	assert(socket_invalid != socket);
	ret = socket_select_read(socket, timeout);
	if (socket_error == ret)
	{
		return socket_invalid;
	}
	else if (0 == ret)
	{
		return socket_invalid; //SOCKET_TIMEDOUT
	}

	return socket_accept(socket, addr, addrlen);
}


//////////////////////////////////////////////////////////////////////////
/// send/recv by time
//////////////////////////////////////////////////////////////////////////

/// @param[in] timeout ms, -1==infinite
/// @return >0-sent bytes, SOCKET_TIMEDOUT-timeout, <0-error(by socket_geterror())
static inline int socket_send_by_time(IN socket_t sock, IN const void* buf, IN size_t len, IN int flags, IN int timeout)
{
	int r;

	r = socket_select_write(sock, timeout);
	if (r <= 0)
		return 0 == r ? SOCKET_TIMEDOUT : r;
	assert(1 == r);

	r = socket_send(sock, buf, len, flags);
	return r;
}

/// @param[in] timeout ms, -1==infinite
/// @return >0-sent bytes, SOCKET_TIMEDOUT-timeout, <0-error(by socket_geterror())
static inline int socket_send_all_by_time(IN socket_t sock, IN const void* buf, IN size_t len, IN int flags, IN int timeout)
{
	int r;
	size_t bytes = 0;

	while (bytes < len)
	{
		r = socket_send_by_time(sock, (const char*)buf + bytes, len - bytes, flags, timeout);
		if (r <= 0)
			return r;	// <0-error

		bytes += r;
	}
	return bytes;
}

/// @param[in] timeout ms, -1==infinite
/// @return >0-received bytes, SOCKET_TIMEDOUT-timeout, <0-error(by socket_geterror()), 0-connection closed(recv only)
static inline int socket_recv_by_time(IN socket_t sock, OUT void* buf, IN size_t len, IN int flags, IN int timeout)
{
	int r;

	r = socket_select_read(sock, timeout);
	if (r <= 0)
		return 0 == r ? SOCKET_TIMEDOUT : r;
	assert(1 == r);

	r = socket_recv(sock, buf, len, flags);
	return r;
}

/// @param[in] timeout ms, -1==infinite
/// @return >0-received bytes, SOCKET_TIMEDOUT-timeout, <0-error(by socket_geterror()), 0-connection closed(recv only)
static inline int socket_recv_all_by_time(IN socket_t sock, OUT void* buf, IN size_t len, IN int flags, IN int timeout)
{
	int r;
	size_t bytes = 0;

	while (bytes < len)
	{
		r = socket_recv_by_time(sock, (char*)buf + bytes, len - bytes, flags, timeout);
		if (r <= 0)
			return r;	// <0-error / 0-connection closed

		bytes += r;
	}
	return bytes;
}

/// @param[in] timeout ms, -1==infinite
/// @return >0-sent bytes, SOCKET_TIMEDOUT-timeout, <0-error(by socket_geterror())
static inline int socket_send_v_all_by_time(IN socket_t sock, IN socket_bufvec_t* vec, IN size_t n, IN int flags, IN int timeout)
{
	int r;
	size_t i, count;
	size_t bytes = 0;

	while (n > 0)
	{
		r = socket_select_write(sock, timeout);
		if (r <= 0)
			return 0 == r ? SOCKET_TIMEDOUT : r;
		assert(1 == r);

		r = socket_send_v(sock, vec, n, flags);
		if (r <= 0)
			return r;	// <0-error

		bytes += r;

		for (i = 0, count = 0; i < n; i++)
		{
			if (count + vec[i].iov_len > (size_t)r)
				break;
			count += vec[i].iov_len;
		}

		n -= i;
		if (n > 0)
		{
			count = r - count;
			vec[i].iov_len -= count;
			vec[i].iov_base = (char*)vec[i].iov_base + count;
			vec += i;
		}
	}

	return bytes;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif /* !_sockutil_h_ */
