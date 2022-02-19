/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

'use strict';

import * as config from 'uDevice.config';

/* when did the device last connect to the controller */
let seen = 0;

/* default reconnect time if 10s */
export let reconnect = 10;

/* gets called when the device established a connection */
export function connected() {
	seen = time();
};

/* gets called when the device got disconnection */
export function disconnected() {
	seen = 0;
};

/* dump the current status of the device */
export function status() {
	return {
		connected: seen ? time() - seen : 0,
		latest: config.uuid,
	};
};
