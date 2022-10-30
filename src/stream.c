/*==========================================================*/
/*							STREAMS							*/
/*==========================================================*/
#include "stream.h"
#include "memory.h"
#include "fiber.h"
#include "buffers.h"
#include "channel.h"
#include "lock.h"
#include "core.h"
#include "log.h"
#include "os.h"

#include <uv.h>

#define DEFAULT_BACKLOG 128

enum stream_type_t {
	TC_TCP_SOCKET,
	TC_UDP_SOCKET,
	TC_PIPE
};

typedef struct server_s {
	tc_socket_i;
	lock_t lock;
	tc_allocator_i* a;
	tc_buffers_i* buffers;
	tc_channel_t* channel;
	uv_async_t async_read;
	uv_async_t async_listen;
	uv_async_t async_close;
	int64_t nbytes;
	uv_buf_t buf;
	uint32_t num_clients;
	enum stream_type type;
	void* handle;
} socket_t;

typedef struct pipe_s {
	socket_t;
	uv_pipe_t pipe;
	fd_t fd;
} pipe_t;

typedef struct tcp_s {
	socket_t;
	uv_tcp_t tcp;
	struct sockaddr_in addr;
} tcp_t;

typedef struct udp_s {
	socket_t;
	uv_udp_t udp;
	struct sockaddr_in addr;
} udp_t;

static void stream_read_async(uv_async_t* handle);
static void stream_listen_async(uv_async_t* handle);
static void stream_close_async(uv_async_t* handle);

static tc_fut_t* stream_read(tc_stream_i* stream, int64_t nread);
static void stream_listen(tc_stream_i* stream);
static void stream_accept(tc_stream_i* stream);
static void stream_close(tc_stream_i* stream);

static void server_init(socket_t* s, tc_allocator_i* alloc, tc_buffers_i* buf, enum stream_type type)
{
	s->a = alloc;
	s->instance = s;
	s->read = stream_read;
	s->listen = stream_listen;
	s->close = stream_close;
	s->buffers = buf;
	s->type = type;
	s->channel = tc_channel->create(alloc, 4);

	void* loop = tc_fiber->eventloop();
	uv_async_init(loop, &s->async_read, stream_read_async);
	s->async_read.data = s;
	uv_async_init(loop, &s->async_listen, stream_listen_async);
	s->async_listen.data = s;
	uv_async_init(loop, &s->async_close, stream_close_async);
	s->async_close.data = s;
}

static pipe_t* pipe_create(tc_allocator_i* a, tc_buffers_i* b)
{
	pipe_t* s = tc_malloc(a, sizeof(pipe_t));
	memset(s, 0, sizeof(pipe_t));
	server_init(s, a, b, TC_PIPE);
	uv_pipe_init(tc_fiber->eventloop(), &s->pipe, 0);
	s->handle = &s->pipe;
	s->pipe.data = s;
	return s;
}

static tcp_t* tcp_create(tc_allocator_i* a, tc_buffers_i* b)
{
	tcp_t* s = tc_malloc(a, sizeof(tcp_t));
	memset(s, 0, sizeof(tcp_t));
	server_init(s, a, b, TC_TCP_SOCKET);
	uv_tcp_init(tc_fiber->eventloop(), &s->tcp);
	s->handle = &s->tcp;
	s->tcp.data = s;
	return s;
}

static udp_t* udp_create(tc_allocator_i* a, tc_buffers_i* b)
{
	udp_t* s = tc_malloc(a, sizeof(udp_t));
	memset(s, 0, sizeof(udp_t));
	server_init(s, a, b, TC_UDP_SOCKET);
	uv_udp_init(tc_fiber->eventloop(), &s->udp);
	s->handle = &s->udp;
	s->udp.data = s;
	return s;
}

static void stream_connect_cb(uv_stream_t* handle, int status)
{
	socket_t* s = handle->data;

	socket_t* client = NULL;
	if (s->type == TC_TCP_SOCKET)
		client = tcp_create(s->a, s->buffers);
	else if (s->type == TC_PIPE)
		client = pipe_create(s->a, s->buffers);

	if (client && uv_accept(s->handle, client->handle) == 0) {
		while (!tc_channel->try_put(&(tc_put_t) { s->channel, client })) {
			TRACE(LOG_WARNING, "[Stream]: connection channel full");
		}
	}
}

static void stream_alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	socket_t* s = handle->data;
	TC_LOCK(&s->lock);
	suggested_size = min(suggested_size, s->nbytes);
	void* ptr = s->buffers->alloc(s->buffers, suggested_size, NULL);
	s->buf = uv_buf_init(ptr, suggested_size);
	*buf = s->buf;
	TC_UNLOCK(&s->lock);
}

static void stream_read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
	bool success = false;
	socket_t* s = handle->data;
	TC_LOCK(&s->lock);
	if (nread <= 0) {
		if (nread == UV_EOF) {
			uv_read_stop(s->handle);
			tc_channel->close(s->read);
			uv_close(s->handle, NULL);
		}
	}
	else {
		s->nbytes -= nread;
		uint32_t id = s->buffers->add(s->buffers, buf->base, buf->len, 0);
		success = tc_channel->try_put(&(tc_put_t) { s->channel, id });
	}
	TC_UNLOCK(&s->lock);
	TC_ASSERT(success);
}

static void stream_udp_read_cb(uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags)
{
	bool success = false;
	socket_t* s = handle->data;
	TC_LOCK(&s->lock);
	if (nread <= 0) {
		if (nread == UV_EOF) {
			uv_udp_read_stop(s->handle);
			tc_channel->close(s->channel);
			uv_close(s->handle, NULL);
		}
	}
	else {
		s->nbytes -= nread;
		uint32_t id = s->buffers->add(s->buffers, buf->base, buf->len, 0);
		success = tc_channel->try_put(&(tc_put_t) { s->channel, id });
	}
	TC_UNLOCK(&s->lock);
	TC_ASSERT(success);
}

static void stream_read_async(uv_async_t* handle)
{
	socket_t* s = handle->data;
	if (s->type == TC_UDP_SOCKET)
		uv_udp_recv_start(s->handle, stream_alloc_cb, stream_udp_read_cb);
	else
		uv_read_start(s->handle, stream_alloc_cb, stream_read_cb);
}

static void stream_listen_async(uv_async_t* handle)
{
	socket_t* s = handle->data;
	uv_listen(s->handle, DEFAULT_BACKLOG, stream_connect_cb);
}

static void stream_close_async(uv_async_t* handle)
{
	socket_t* s = handle->data;
	uv_close(s->handle, NULL);
}

tc_fut_t* stream_read(tc_stream_i* stream, int64_t nread)
{
	socket_t* s = stream->instance;
	TC_LOCK(&s->lock);
	s->nbytes = nread;
	uv_async_send(&s->async_read);
	TC_UNLOCK(&s->lock);
	return tc_channel->get(s->channel);
}

void stream_listen(tc_stream_i* stream)
{
	socket_t* s = stream->instance;
	uv_async_send(&s->async_listen);
}

void stream_accept(tc_stream_i* stream)
{
	socket_t* s = stream->instance;

}

void stream_close(tc_stream_i* stream)
{
	socket_t* s = stream->instance;
	tc_channel->close(s->channel);
	uv_async_send(&s->async_close);
}

tc_stream_i* stream_open_pipe(tc_allocator_i* a, tc_buffers_i* b, fd_t fd)
{
	TC_ASSERT(fd.handle != TC_INVALID_FILE);
	pipe_t* s = pipe_create(a, b);

	s->fd = fd;
	int err = uv_pipe_open(&s->pipe, fd.handle);
	TC_ASSERT(err == 0);

	return s;
}

tc_socket_i* stream_open_tcp(tc_allocator_i* a, tc_buffers_i* b, const char* host, int port)
{
	TC_ASSERT(host != NULL && port);
	tcp_t* s = tcp_create(a, b);

	uv_ip4_addr(host, port, &s->addr);
	int err = uv_tcp_bind(&s->tcp, &s->addr, 0);
	TC_ASSERT(err == 0);

	return s;
}

tc_streammanager_i* tc_stream = &(tc_streammanager_i) {
	.open_tcp = stream_open_tcp,
	.open_pipe = stream_open_pipe,
};
