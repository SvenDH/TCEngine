/*==========================================================*/
/*							STREAM							*/
/*==========================================================*/
#pragma once
#include "types.h"

typedef struct tc_fut_s tc_fut_t;
typedef struct tc_buffers_i tc_buffers_i;
typedef struct tc_allocator_i tc_allocator_i;


typedef struct tc_stream_i {

    void* instance;
    /*
     * Reads at most <size> number of bytes from the stream. Can be waited on.
     * Waiting on the counter returns the number of bytes read.
     */
    tc_fut_t* (*read)(struct tc_stream_i* stream, int64_t nread);
    /*
     * Writes content of the buffer to the stream. Can be waited on.
     * Waiting on the counter returns the number of bytes written.
     */
    tc_fut_t* (*write)(struct tc_stream_i* stream, int64_t nread);
    /*
     * Closes the stream.
     */
    void (*close)(struct tc_stream_i* stream);

} tc_stream_i;


typedef struct tc_socket_i {
    tc_stream_i;

    void (*listen)(struct tc_socket_i* server);

    tc_stream_i* (*accept)(struct tc_socket_i* server);

} tc_socket_i;


typedef struct tc_streammanager_i {

    tc_socket_i* (*open_tcp)(tc_allocator_i* a, tc_buffers_i* b, const char* host, int port);

    tc_socket_i* (*open_pipe)(tc_allocator_i* a, tc_buffers_i* b, fd_t fd);

} tc_streammanager_i;

extern tc_streammanager_i* tc_stream;