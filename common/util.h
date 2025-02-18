/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Ericsson AB
 */

#ifndef UTIL_H
#define UTIL_H

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void ut_mutex_init(pthread_mutex_t *m);
void ut_mutex_lock(pthread_mutex_t *m);
void ut_mutex_unlock(pthread_mutex_t *m);

pid_t ut_gettid(void);

void *ut_malloc(size_t size);
void *ut_realloc(void *ptr, size_t size);
void *ut_calloc(size_t size);
char *ut_strdup(const char *str);
void *ut_memdup(const char *ptr, size_t size);
void ut_free(void *ptr);

int ut_send_all(int fd, void* buf, size_t count, int flags);

int ut_snprintf(char *buf, size_t capacity, const char *format, ...);
void ut_vaprintf(char *buf, size_t capacity, const char *format, va_list ap);
void ut_aprintf(char *buf, size_t capacity, const char *format, ...);

int ut_set_blocking(int fd, bool should_block);
bool ut_is_blocking(int fd);

int ut_established(int fd);

/* 'name' buffer needs to be NAME_MAX in size */
int ut_self_net_ns(char *name);

int ut_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

void ut_die(const char *msg) __attribute__ ((__noreturn__));

extern bool ut_dprint_enabled;

#ifdef NDEBUG

#define ut_assert(expr) do { (void)(expr); } while (0)

#else

#ifdef UT_STD_ASSERT

#include <assert.h>
#define ut_assert assert

#else

#include "log.h"
#include "log_tp.h"
#define ut_assert(expr)							\
    do {								\
	if (!(expr)) {							\
	    log_console_conf(true);					\
	    log_error("Assertion \"%s\" failed.\n", __STRING(expr));	\
	    abort();							\
	}								\
    } while (0)
#endif

#endif

/* for use via macro only */
void _ut_lassert_failed(const char* expr, const char* file, int line);

#define UT_SAVE_ERRNO				\
    int _oerrno = errno

#define UT_RESTORE_ERRNO(saved_name)	\
    int saved_name = errno;		\
    errno = _oerrno

#define UT_RESTORE_ERRNO_DC			\
    errno = _oerrno

#define UT_PROTECT_ERRNO(stmt)			\
    do {					\
	int _errno = errno;			\
	stmt;					\
	errno = _errno;				\
    } while (0)

#define UT_MAX(a, b)				\
    ({						\
	typeof(a) _a = a;			\
	typeof(b) _b = b;			\
	_a > _b ? _a : _b;			\
    })

#define UT_MIN(a, b)				\
    ({						\
	typeof(a) _a = a;			\
	typeof(b) _b = b;			\
	_a < _b ? _a : _b;			\
    })

#define UT_ARRAY_LEN(ary) (sizeof(ary) / sizeof(ary[0]))

#endif
