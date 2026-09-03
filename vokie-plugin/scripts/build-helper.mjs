import { spawnSync } from 'node:child_process';
import { chmodSync, existsSync, mkdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

if (process.platform !== 'darwin') {
  console.error('AI Passport helper must be built on macOS.');
  process.exit(1);
}
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = path.join(root, 'helper', 'AiPassportHelper.swift');
const infoPlist = path.join(root, 'helper', 'Info.plist');
const output = path.join(root, 'assets', 'bin', 'ai-passport-helper');
if (!existsSync(source)) throw new Error(`Missing Swift source: ${source}`);
if (!existsSync(infoPlist))
  throw new Error(`Missing helper Info.plist: ${infoPlist}`);
mkdirSync(path.dirname(output), { recursive: true });
const result = spawnSync(
  'xcrun',
  [
    'swiftc',
    source,
    '-target',
    'arm64-apple-macosx12.0',
    '-framework',
    'CoreBluetooth',
    // TCC evaluates the helper process bundle, so embed its Bluetooth usage
    // description instead of relying only on Vokie's parent app plist.
    '-Xlinker',
    '-sectcreate',
    '-Xlinker',
    '__TEXT',
    '-Xlinker',
    '__info_plist',
    '-Xlinker',
    infoPlist,
    '-o',
    output
  ],
  { stdio: 'inherit' }
);
if (result.error || result.status !== 0) process.exit(result.status || 1);
// Preserve an executable mode when the package is copied through a filesystem
// or archive that does not retain the compiler's default mode bits.
chmodSync(output, 0o755);
console.log(`Built ${output}`);
