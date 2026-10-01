'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const appDirectory = path.resolve(__dirname, '..');
const cacheDirectory = process.argv[2];
const prepareOnly = process.argv.includes('--prepare-only');
const cliVersion = '4.5.3';
let logFile;
let logDescriptor;

function run(script, args, { cwd = appDirectory, interactive = false, label } = {}) {
  fs.writeSync(logDescriptor, `\n${label}\n`);
  const result = spawnSync(process.execPath, [script, ...args], {
    cwd,
    stdio: interactive ? 'inherit' : ['ignore', logDescriptor, logDescriptor],
    env: { ...process.env, NO_UPDATE_NOTIFIER: '1', npm_config_update_notifier: 'false' },
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    throw new Error(`${label} failed. Please try again.`);
  }
}

try {
  if (!cacheDirectory || !path.isAbsolute(cacheDirectory)) throw new Error('Start the Mac or Windows installer from the homey folder.');
  if (!fs.existsSync(path.join(appDirectory, 'app.json'))) throw new Error('Extract the complete AirBee ZIP before starting the installer.');
  fs.mkdirSync(cacheDirectory, { recursive: true });
  logFile = path.join(cacheDirectory, `install-${Date.now()}-${process.pid}.log`);
  logDescriptor = fs.openSync(logFile, 'a', 0o600);

  const nodeDirectory = path.dirname(process.execPath);
  const npm = process.platform === 'win32'
    ? path.join(nodeDirectory, 'node_modules/npm/bin/npm-cli.js')
    : path.resolve(nodeDirectory, '../lib/node_modules/npm/bin/npm-cli.js');
  const cliDirectory = path.join(cacheDirectory, `homey-cli-${cliVersion}`);
  const homey = path.join(cliDirectory, 'node_modules/homey/bin/homey.mjs');
  const ready = path.join(cliDirectory, '.ready');
  const npmCache = path.join(cacheDirectory, 'npm-cache');

  console.log('\n2/4 - Preparing installation. The first run can take a few minutes.');
  if (!fs.existsSync(homey) || !fs.existsSync(ready)) {
    fs.mkdirSync(cliDirectory, { recursive: true });
    run(npm, ['install', '--prefix', cliDirectory, '--cache', npmCache,
      '--ignore-scripts', '--no-audit', '--no-fund', '--save-exact', `homey@${cliVersion}`],
    { cwd: cliDirectory, label: 'Downloading Homey tools' });
    fs.writeFileSync(ready, `${cliVersion}\n`);
  }
  run(npm, ['ci', '--cache', npmCache, '--ignore-scripts', '--no-audit', '--no-fund'],
    { label: 'Preparing app files' });

  if (prepareOnly) {
    run(homey, ['app', 'build'], { label: 'Building app' });
    run(homey, ['app', 'validate', '--level', 'publish'], { label: 'Validating app' });
    console.log('Installer preparation and app validation passed. No Homey was changed.');
  } else {
    console.log('\n3/4 - Sign in if a browser opens. Then return here and choose your Homey with the arrow keys and Enter.');
    run(homey, ['select'], { interactive: true, label: 'Selecting Homey' });
    console.log('\n4/4 - Installing SENSY-ONE on your selected Homey...');
    run(homey, ['app', 'install'], { label: 'Installing app on Homey' });
    console.log('\nDone! SENSY-ONE is installed on Homey. You can close this window.');
  }
} catch (error) {
  console.error(`\n${error.message}`);
  if (logDescriptor !== undefined) console.error(`Details for support: ${logFile}`);
  process.exitCode = 1;
} finally {
  if (logDescriptor !== undefined) fs.closeSync(logDescriptor);
}
