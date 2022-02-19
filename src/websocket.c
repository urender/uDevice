/* SPDX-License-Identifier: ISC */

/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

#include "device.h"

struct ws_private_data {
	struct lws_context *context;
	struct lws_vhost *vhost;
	const struct lws_protocols *protocol;
	struct lws_client_connect_info server;
	struct lws *client_wsi;

	struct uloop_timeout connect;
};

/**
 * attempt to connect to the server
 */
static void
ws_connect_attempt(struct uloop_timeout *t)
{
	struct ws_private_data *priv = container_of(t, struct ws_private_data, connect);

	/* setup the private data */
	priv->server.context = priv->context;
	priv->server.port = ctx->port;
	priv->server.address = ctx->server;
	priv->server.path = ctx->path;
	priv->server.host = priv->server.address;
	priv->server.origin = priv->server.address;
//	priv->server.ssl_connection = LCCSCF_USE_SSL | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LWS_SERVER_OPTION_DISABLE_OS_CA_CERTS;
	priv->server.protocol = "urender";
	priv->server.pwsi = &priv->client_wsi;
//	priv->server.ssl_connection |= LCCSCF_ALLOW_SELFSIGNED;

	lws_client_connect_via_info(&priv->server);
}

/**
 * helper function for defered reconnects
 */
void
ws_reconnect(struct urender_context *ctx, int timeout)
{
	if (!ctx->priv) {
		ULOG_ERR("trying to reconnect a none existing context\n");
		return;
	}

	/* trigger the uloop timer that will call ws_connect_attempt() */
	uloop_timeout_set(&ctx->priv->connect, timeout * 1000);
}

/**
 * send a payload to the server
 */
int
ws_send(struct urender_context *ctx, const char *msg)
{
	char *payload;
        int len, ret = 0;

	/* allocate memory with enough LWS headroom and copy the message into it */
	len = strlen(msg) + 1;
	payload = malloc(LWS_PRE + len);
	memcpy(&payload[LWS_PRE], msg, len);
	memset(payload, 0, LWS_PRE);

	/* send out the payload on its way */
	ret = lws_write(ctx->websocket, (unsigned char *)&payload[LWS_PRE], len - 1, LWS_WRITE_TEXT);
	if (ret < 0)
		ULOG_ERR("failed to send message\n");

	free(payload);

	return ret;
}

/**
 * the websocket state machine handler function
 */
static int
ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
	    void *user, void *in, size_t len)
{
	struct ws_private_data *priv = (struct ws_private_data *)
		lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
	union lws_tls_cert_info_results ci;

	switch (reason) {
	/* create the connections private data before anything else happens */
	case LWS_CALLBACK_PROTOCOL_INIT:
		priv = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi),
				lws_get_protocol(wsi),
				sizeof(struct ws_private_data));
		priv->context = lws_get_context(wsi);
		priv->protocol = lws_get_protocol(wsi);
		priv->vhost = lws_get_vhost(wsi);
		ctx = ctx;
		ctx->priv = priv;

		/* trigger the actual connect attempt */
		priv->connect.cb = ws_connect_attempt;
		ws_connect_attempt(&priv->connect);
		break;

	/* if the initial connect failed we need to trigger a reconnect attempt */
	case LWS_CALLBACK_WSI_DESTROY:
		if (!ctx->connected)
			uc_handle_event(ctx, EVENT_DISCONNECT, NULL);
		break;

	/* cleanup after shutdown */
	case LWS_CALLBACK_PROTOCOL_DESTROY:
		uloop_timeout_cancel(&priv->connect);
		return 0;

	/* a connection to the server was estiablished */
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
                if (!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_COMMON_NAME, &ci, sizeof(ci.ns.name)))
                        ULOG_INFO(" Peer Cert CN        : %s\n", ci.ns.name);

                if (!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_VALIDITY_TO, &ci, 0))
                        ULOG_INFO(" Peer Cert Valid to  : %s", ctime(&ci.time));

                if (!lws_tls_peer_cert_info(wsi, LWS_TLS_CERT_INFO_ISSUER_NAME,
                                            &ci, sizeof(ci.ns.name)))
                        ULOG_INFO(" Peer Cert issuer    : %s\n", ci.ns.name);

		ctx->websocket = wsi;
		ctx->connected = 1;
		uc_handle_event(ctx, EVENT_CONNECT, NULL);
		break;

	/* new data was received */
	case LWS_CALLBACK_CLIENT_RECEIVE:
		uc_handle_event(ctx, EVENT_RECEIVE, (char *)in);
		break;

	/* the connection unexpectedly shutdown */
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		ULOG_ERR("connection error: %s\n",
			 in ? (char *)in : "(null)");

#ifdef __clang_analyzer__
		__attribute__ ((fallthrough));
#endif

	/* an orderly connection shutdown happened */
	case LWS_CALLBACK_CLIENT_CLOSED:
		ctx->connected = 0;
		priv->client_wsi = NULL;
		uc_handle_event(ctx, EVENT_DISCONNECT, NULL);
		break;

	default:
		break;
	}

	return 0;
}

static const struct
lws_protocols ws_protocols[] = {
	{ "urender", ws_callback, 0, 32 * 1024, 0, NULL, 0},
	{ }
};

int
ws_connect(struct urender_context *_ctx)
{
	int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_CLIENT | LLL_DEBUG;
	struct lws_context_creation_info info;
	struct lws_context *context;

	lws_set_log_level(logs, NULL);

	memset(&info, 0, sizeof info);
	info.port = CONTEXT_PORT_NO_LISTEN;
//	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
//	info.client_ssl_cert_filepath = "/etc/urender/cert.pem";
//	info.client_ssl_private_key_filepath = "/etc/urender/key.pem";
//	info.client_ssl_ca_filepath = "/etc/urender/ca.pem";
	info.protocols = ws_protocols;
	info.fd_limit_per_thread = 1 + 1 + 1;
        info.timeout_secs = 60;
        info.connect_timeout_secs = 30;
	info.options |= LWS_SERVER_OPTION_ULOOP;

	context = lws_create_context(&info);
	if (!context) {
		ULOG_INFO("failed to start LWS context\n");
		return -1;
	}

	return 0;
}
