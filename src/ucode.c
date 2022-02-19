/* SPDX-License-Identifier: ISC */

/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

#include <ucode/module.h>
#include "device.h"

static uc_resource_type_t *urender_type;
struct urender_context *ctx = NULL;

/**
 * websockect has connected, call the ucode handler
 */
void
uc_handle_event(struct urender_context *ctx, enum uc_event event, char *data)
{
	uc_value_t *retval = NULL, *callback;
	struct json_object *o = NULL;

	/* get the correct callback handler for the event */
	switch (event) {
	case EVENT_CONNECT:
		callback = ctx->connect;
		break;

	case EVENT_DISCONNECT:
		callback = ctx->disconnect;
		break;

	case EVENT_RECEIVE:
		callback = ctx->receive;
		break;

	default:
		return;
	}

	/* push the object as "this" and the callback function value onto the stack */
	uc_vm_stack_push(ctx->vm, ucv_get(ctx->obj));
	uc_vm_stack_push(ctx->vm, ucv_get(callback));

	/* push the data to the stack */
	if (data)
		o = json_tokener_parse(data);

	if (data && !o)
		ULOG_ERR("failed to parse received message\n");

	uc_vm_stack_push(ctx->vm, ucv_from_json(ctx->vm, o));
	json_object_put(o);

	/* execute the callback */
	if (!uc_vm_call(ctx->vm, true, 1))
		retval = uc_vm_stack_pop(ctx->vm);

	ucv_put(retval);
}

/**
 * validate that the ucode has all the callbacks and setup the connection context
 */
static int
uc_context(struct urender_context *ctx)
{
	/* check if the global object exists */
	uc_value_t *obj = ucv_object_get(uc_vm_scope_get(ctx->vm), "urender", NULL);
	if (!obj) {
		ULOG_ERR("failed to load global.urender\n");
		return -1;
	}

	/* check if all required callbacks are present */
	ctx->connect = ucv_object_get(obj, "connect", NULL);
	if (!ucv_is_callable(ctx->connect))
		return -1;

	ctx->disconnect = ucv_object_get(obj, "disconnect", NULL);
	if (!ucv_is_callable(ctx->disconnect))
		return -1;

	ctx->receive = ucv_object_get(obj, "receive", NULL);
	if (!ucv_is_callable(ctx->receive))
		return -1;

	/* prevent the GC from removing this object */
	ctx->obj = ucv_get(obj);

	return 0;
}

/**
 * ucode requested a new connection to a server. allocate the conext,
 * create the resource object and start the connection process
 */
static uc_value_t *
uc_connect(uc_vm_t *vm, size_t nargs)
{
	uc_value_t *_server = uc_fn_arg(0);
	uc_value_t *_port = uc_fn_arg(1);
	int ret;

	/* make sure that a valid server and port were provided */
	if (ctx || !_server || !_port)
		return ucv_boolean_new(false);

	/* allocate the context */
	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return ucv_boolean_new(false);

	ctx->vm = vm;
	if (uc_context(ctx)) {
		free(ctx);
		return ucv_boolean_new(false);
	}

	ctx->server = strdup(ucv_to_string(vm, _server));
	ctx->port = ucv_int64_get(_port);
	ctx->path = strdup("/urender/");

	/* start the connection */
	ret = ws_connect(ctx);
	if (ret) {
		free(ctx->server);
		free(ctx);
		return ucv_boolean_new(false);
	}

	/* create and return the new connection resource */
	return uc_resource_new(urender_type, ctx);
}

/**
 * ucode requested the transmission of data
 */
static uc_value_t *
uc_send(uc_vm_t *vm, size_t nargs)
{
	struct urender_context **ctx = uc_fn_this("urender.context");
	uc_value_t *data = uc_fn_arg(0);
	char *msg;
	int ret;

	/* make sure that the connection is esatblished */
	if (!ctx || !*ctx || !(*ctx)->connected) {
		ULOG_ERR("trying to send data while not connected\n");
		return ucv_boolean_new(false);
	}

	/* convert the data to a json string */
	msg = ucv_to_jsonstring((*ctx)->vm, data);
	ret = ws_send(*ctx, msg);
	free(msg);

	return ucv_boolean_new(!ret);
}

/**
 * ucode requested to reconnect to the server
 */
static uc_value_t *
uc_reconnect(uc_vm_t *vm, size_t nargs)
{
	struct urender_context **ctx = uc_fn_this("urender.context");
	uc_value_t *arg = uc_fn_arg(0);
	int timeout = 10;

	if (!ctx || !(*ctx) || (*ctx)->connected)
		return ucv_boolean_new(false);

	/* was the function called with a timeout value ? */
	if (arg)
		timeout = ucv_int64_get(arg);

	/* start the reconnect logic */
	ULOG_INFO("reconnect in %d seconds\n", timeout);
	ws_reconnect(*ctx, timeout);

	return ucv_boolean_new(true);
}

/**
 * ucode requested the close the connection to the servera
 */
static uc_value_t *
uc_close(uc_vm_t *vm, size_t nargs)
{
	struct urender_context **ctx = uc_fn_this("urender.context");

	if (ctx && *ctx && (*ctx)->connected)
		lws_wsi_close((*ctx)->websocket, LWS_TO_KILL_SYNC);

	return NULL;
}

static const uc_function_list_t global_fns[] = {
	{ "connect",	uc_connect },
};

static const uc_function_list_t urender_fns[] = {
	{ "send",	uc_send },
	{ "reconnect",	uc_reconnect },
	{ "close",	uc_close },
};

/**
 * free the memory allocated by this resource
 */
static void
uc_context_close(void *ud)
{
	struct urender_context *ctx = (struct urender_context *) ud;

	free(ctx->server);
	free(ctx->path);
	free(ctx);
}

void
uc_module_init(uc_vm_t *vm, uc_value_t *scope)
{
	urender_type = uc_type_declare(vm, "urender.context", urender_fns, uc_context_close);
	uc_function_list_register(scope, global_fns);
}
