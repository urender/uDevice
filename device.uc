#!/usr/bin/ucrun

/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

'use strict';

/* include required libraries */
let uc = require('uDevice');

import * as libuci from 'uci';
global.uci = libuci.cursor();

import * as libubus from 'ubus';
global.ubus = libubus.connect();

import * as reader from 'uReader.device_method';
import * as protocol from 'uDevice.protocol';
import { settings } from 'uDevice.settings';
import * as context from 'uDevice.context';
import * as fs from 'fs';

global.ulog = {
	identity: 'uDevice',
	channels: [ 'stdio', 'syslog' ],
};

global.publish = {
	object: 'uDevice',

	/**
	 * this function gets called whenever a connection to ubus was established
	 */
	connect: function() {
		ulog_info('connected to ubus\n');
	},

	methods: {
		/**
		 * the callback handler for the status method
		 */
		status: {
			cb: function(msg) {
				/* return the internal state of the device */
				return context.status();
			}
		}
	}
};

global.urender = {
	/**
	 * this function gets called whenever a connection to the controller was established
	 */
	connect: function() {
		ulog_info('connected to uControl\n');

		/* store the connection time */
		context.connected();

		/* send the connect message */
		protocol.connect();

		/* check if there is a crashlog */
		if (fs.stat('/sys/fs/pstore/dmesg-ramoops-0'))
			protocol.crashlog();
	},

	/**
	 * this function gets called whenever the connection to the controller was lost
	 */
	disconnect: function() {
		ulog_info('disconnected from uControl\n');

		/* reset the connection time */
		context.disconnected();

		/* dont reconnect when a reboot, fatory reset or sysupgrade is in progess */
		if (context.reconnect)
			uControl.reconnect(context.reconnect);
	},

	/**
	 * this function gets called whenever data was received from the controller
	 */
	receive: function(msg) {
		let error = [];
		let cmd = reader.validate(msg, error);

		if (!cmd || length(error)) {
			ulog_err('failed to parse incoming frame\n');
			return;
		}

		printf('RX: %.J\n', cmd);

		return protocol.handle(msg);
	},
};

/**
 * the main entry point that ucrun calls
 */
global.start = function() {
	ulog_info('starting urender-device\n');

	/* connect to the controller */
	ulog_info('connecting to %s:%d\n', settings.server, settings.port);
	global.uControl = uc.connect(settings.server, settings.port);
};

/**
 * ucrun is shutting down
 */
global.stop = function() {
	ulog_info('stopping\n');
	uControl.close();
};
