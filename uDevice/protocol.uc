/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

'use strict';

import * as reader from 'uReader.device_notification';
import { capabilities } from 'uDevice.capabilities';
import { settings } from 'uDevice.settings';
import * as context from 'uDevice.context';
import * as config from 'uDevice.config';
import { ubus } from 'uDevice.ubus';
import * as fs from 'fs';

let methods;

/**
 * generate a json-rpc2.0 method call
 */
function notification(method, params) {
	let msg = {
		jsonrpc: '2.0',
		method,
		params
	};

	/* pipe the outgoind data through the validation reader */
	let errors = [];
	let notification = reader.validate(msg, errors);

	if (!notification)
		return 1;

	uControl.send(notification);
}

/**
 * generate a json-rpc2.0 result
 */
function response(id, result) {
	let msg = {
		jsonrpc: '2.0',
		id
	};

	if (result.code)
		msg.error = result;
	else
		msg.result = result.message;

	uControl.send(msg);
}

/**
 * handle an incoming RPC
 */
export function handle(msg) {
	/* basic sanity checks */
	if (msg.jsonrpc != '2.0' || !msg.method)
		return false;

	/* check if a handler is available */
	let handler = methods[msg.method];
	if (!handler) {
		response(msg.id, { code: 2, message: 'Unknown method.' });
		return false;
	}

	let result;
	try {
		/* call the handler */
		result = handler(msg);
	} catch(e) {
		if (e?.code !== null)
			result = e;
		else
			result = { code: 2, message: e.message };
	}

	if (result?.code != null) {
		ulog_info('\'%s\' command was executed - \'%s\' (%d)\n',
			  msg.method, result.message, result.code);
		response(msg.id, result);
	}
};

/**
 * generate the connect message
 */
export function connect() {
	notification('connect',{
		serial: settings.serial,
		uuid: config.uuid,
		firmware: '1.1',
		capabilities: capabilities,
	});
};

/**
 * send a crashlog
 */
export function crashlog() {
	/* read the crashlog and convert it into an array of lines */
	let file = fs.open('/sys/fs/pstore/dmesg-ramoops-0', 'r');
	let line, crashlog = [];

	while (line = file.read('line'))
	        push(crashlog, trim(line));
	file.close();

	/* remove the crashlog */
	fs.unlink('/sys/fs/pstore/dmesg-ramoops-0');

	notification('crashlog', { crashlog });
};

/**
 * the handler for received RPC method calls
 */
methods = {
	/**
	 * controller sends this command when it wants the device to load a new configuration
	 */
	configure: function(msg) {
		/* basic sanity checks */
		let cfg = msg.params;

		if (!cfg.uuid)
			die({code: 2, message: 'config is missing its uuid'});

		ulog_info('received a new configuration\n');

		/* store the config on fs prior to applying it */
		if (!config.store(cfg))
			die({code: 2, message: 'failed to store new configuration'});

		/* attempt to apply the config */
		let response = config.apply(cfg);
		if (response.code)
			die(response);

		/* mark the new config as active */
		config.active(cfg.uuid);

		/* set a timer for 2 seconds to trigger the config reload
		 * this avoids that the device cannot confirm the configure request
		 * in case wan connectivity is reset during reload */
		uloop_timeout(function() {
				ulog_info('trigger config reload\n');
				system('reload_config');
			}, 2000);

		/* all went well */
		return response;
	},

	/**
	 * controller sends this command when it wants the device to reboot
	 */
	reboot: function(msg) {
		/* wait 2 seconds before closing the connection and rebooting the device */
		uloop_timeout(function() {
				global.uControl.close();
				global.ubus.call('system', 'reboot');
			}, 2000);

		/* all went well */
		return { code: 0, message: 'rebooting' };
	},

	/**
	 * controller sends this command when it wants the device to perform a factory-reset
	 */
	factory: function(msg) {
		/* wait 2 seconds before closing the connection and factory resetting the device */
		uloop_timeout(function() {
				global.uControl.close();

				/* TODO: a ubus call would be much nicer here */
				system('/sbin/jffs2reset -y -r');
			}, 2000);

		/* all went well */
		return { code: 0, message: 'factory resetting' };
	},

	/**
	 * controller sends this command when it wants the device to flash its LEDs or turn them on/off
	 */
	leds: function(msg) {
		/* trigger the correct pattern */
		switch (msg.params?.pattern) {
		case 'on':
			system('/etc/init.d/led turnon');
			break;

		case 'off':
			system('/etc/init.d/led turnoff');
			break;

		case 'blink':
			ulog_info('start blinking the LEDs\n');
			uloop_process(function(retcode, priv) {
					ulog_info('stop blinking the LEDs\n');
					response(priv.id, { code: 0, message: 'success' });
				},
				[ '/usr/libexec/uDevice/led_blink.sh', msg.params?.duration || 10 ], { id: msg.id });
			return;

		default:
			die({ code: 2, message: 'invalid LED pattern' });
		}

		return { code: 0, message: 'success' };
	},

	/**
	 * controller sends this command when it wants the device upgrade its firmware
	 */
	upgrade: function(msg) {
		/* download and validate the sysupgrade file */
		let image_path = '/tmp/urender.upgrade';
		let download_cmdline = [ 'wget', '-O', image_path, msg.params.url ];
		let rc = system(download_cmdline);
		let fw_validate = ubus.call('system', 'validate_firmware_image', { path: image_path });
		if (!fw_validate?.valid) {
			fs.unlink(image_path);
			return { code: 1, message: 'Firmware failed to be validated.' };
		}

		/* assemble the cmdline and /tmp/sysupgrade.tgz */
		let sysupgrade_cmdline = [ 'sysupgrade' ];
		if (msg.params.keep_controller) {
			let archive_cmdline = [
				'tar', 'czf', '/tmp/sysupgrade.tgz',
				'/etc/config/uDevice'
			];
		        let active_config = fs.readlink("/etc/urender/urender.active");
			if (active_config)
		                push(archive_cmdline, '/etc/urender/urender.active', active_config);
			let rc = system(archive_cmdline);
			if (rc)
				return { code: 1, message: 'Failed to create /tmp/sysupgrade.tgz.' };

			push(sysupgrade_cmdline, '-f');
			push(sysupgrade_cmdline, '/tmp/sysupgrade.tgz');
		} else
			push(sysupgrade_cmdline, '-n');
		push(sysupgrade_cmdline, image_path);

		/* perfofmt he sysupgrade */
		uloop_timeout(function() {
				global.uControl.close();

				system(sysupgrade_cmdline);
			}, 2000);

		return { code: 0, message: 'Performing sysupgrade' };
	},
};
