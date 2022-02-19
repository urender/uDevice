/* Copyright (C) 2022 John Crispin <john@phrozen.org> */

'use strict';

import * as fs from 'fs';

/* load the devices capabilities */
export let capabilities = {};
let file = fs.open('/etc/urender/capabilities.json', 'r');
if (!file)
	die('failed to load capabilities');
capabilities = json(file.read('all'));
file.close();
