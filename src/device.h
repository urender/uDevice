/* SPDX-License-Identifier: ISC */

/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

#include <string.h>

#include <libwebsockets.h>

#include <ucode/lib.h>
#include <libubox/ulog.h>
#include <libubox/uloop.h>

struct urender_context {
	char *server;
	int port;
	char *path;
	int selfsigned;
	int debug;

	struct lws *websocket;
	struct ws_private_data *priv;
	int connected;

	uc_vm_t *vm;

	uc_value_t *obj;
	uc_value_t *connect;
	uc_value_t *disconnect;
	uc_value_t *receive;
};

enum uc_event {
	EVENT_CONNECT,
	EVENT_DISCONNECT,
	EVENT_RECEIVE,
};

extern struct urender_context *ctx;

extern void
uc_handle_event(struct urender_context *ctx, enum uc_event event, char *data);

extern int
ws_connect(struct urender_context *ctx);

extern void
ws_reconnect(struct urender_context *ctx, int timeout);

extern int
ws_send(struct urender_context *ctx, const char *msg);
