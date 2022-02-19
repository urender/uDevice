/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

'use strict';

/* load the settings from uci */
export let settings = uci.get_all('uDevice', 'config');

if (!settings?.server || !settings?.port || !settings?.serial)
	die('invalid UCI environment\n');

settings.port = +settings.port;
