import { fileURLToPath } from 'node:url';
import { npmRun } from './npm-runner.mjs';

const result = npmRun(process.argv[2], { cwd: fileURLToPath(new URL('../', import.meta.url)) });
if (result.error) throw result.error;
process.exitCode = result.status ?? 1;
