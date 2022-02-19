/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

'use strict';

let uRender = require('uRender.uRender');
import * as fs from 'fs';

/**
 * the uuid of the active config
 */
export let uuid = 1;

/**
 * try applying the config
 */
export function apply(config, reload) {
	let file = sprintf('/etc/urender/urender.cfg.%010d', config.uuid);
	let message = "failed to apply";
	let code = 1;

	try {
		ulog_info('applying config uuid: %s\n', config.uuid);
		message = { rejected: uRender.generate(file, false, reload) };
		code = 0;
	} catch (e) {
		warn('Fatal error while generating UCI: ', e, '\n');
		warn(e.stacktrace[0].context, '\n');
		message = e.message;
	}

	gc();

	return { code, message };
};

/**
 * try to load a config file
 */
export function load(path) {
	/* try opening the config file */
	let file = fs.open(sprintf('/etc/urender/%s', path));

	if (!file)
		return false;

	/* try reading the file */
	let config = json(file.read('all'));
	file.close();

	if (config)
		ulog_info('loaded %s\n', path);

	return config;
};

/**
 * store a config onto the fs
 */
export function store(config) {
	/* try opening the new file */
        let file = fs.open(sprintf('/etc/urender/urender.cfg.%10d', config.uuid), 'w');
	if (!file)
		return false;

	/* write the cfg to the file */
	file.write(config);
	file.close();

	return true;
};

/**
 * mark a config as the active one on the fs
 */
export function active(uuid) {
	/* start by deleting the old symlink */
	fs.unlink('/etc/urender/urender.active');

	/* create the new symlink */
	let file = sprintf('/etc/urender/urender.cfg.%10d', uuid);
	fs.symlink(file, '/etc/urender/urender.active');
};

/**
 * initialize the devices config
 */

/* load the latest active config */
let config = load('urender.active');

/* if this fails load the initial default config */
if (!config)
	config = load('urender.cfg.0000000001');

/* apply the config */
apply(config, true);
