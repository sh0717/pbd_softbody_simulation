// Fast lexical naming checks; formatting and semantic review are separate checks.
import assert from 'node:assert/strict';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));

async function collect(directory) {
  const files = [];

  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);

    if (entry.isDirectory()) files.push(...(await collect(file)));
    else if (/\.(?:h|cpp)$/.test(entry.name)) files.push(file);
  }

  return files.sort();
}

function maskNonCode(source) {
  // Keep offsets and line breaks for diagnostics; do not inspect literal/comment text.
  return source.replace(
    /R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\\n])+'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
    (text) => text.replace(/[^\r\n]/g, ' '),
  );
}

const files = (
  await Promise.all(['native', 'tests/native'].map((name) => collect(path.join(root, name))))
).flat();
const failures = [];

for (const file of files) {
  const relative = path.relative(root, file).replaceAll('\\', '/');
  const source = await readFile(file, 'utf8');
  const code = maskNonCode(source);
  const fail = (offset, message) => {
    const line = code.slice(0, offset).split('\n').length;

    failures.push(`${relative}:${line}: ${message}`);
  };

  for (const match of code.matchAll(/\b(?:try|catch|throw|__try|__except|__finally)\b/g)) {
    fail(match.index, 'exceptions: use explicit results and precondition checks.');
  }

  if (file.endsWith('.h') && !/^\s*#pragma once\b/.test(code)) {
    fail(0, 'Start headers with #pragma once.');
  }

  // Scope is approximated lexically; build/tests remain responsible for C++ semantics.
  let scopeDepth = 0;

  for (const match of code.matchAll(/[{}]|\busing\b/g)) {
    if (match[0] === '{') scopeDepth++;
    else if (match[0] === '}') scopeDepth--;
    else if (scopeDepth === 0 && !/^using\s+[A-Za-z_]\w*\s*=/.test(code.slice(match.index))) {
      fail(match.index, 'Keep using declarations/directives inside a namespace or function.');
    }
  }

  for (const match of code.matchAll(/\bauto\s*(?:&&?|\*)?\s*\[/g)) {
    fail(match.index, 'Use a named result type instead of a structured binding.');
  }

  // Template parameters (class ObjectType>) are not class declarations.
  const declarations =
    /\benum\s+(?:(?:class|struct)\s+)?([A-Za-z_]\w*)|\b(?:class|struct)\s+([A-Za-z_]\w*)(?=\s*(?:final\b|[:{;<]))/g;

  for (const match of code.matchAll(declarations)) {
    const name = match[1] || match[2];

    if (match[1]) {
      const isPublicAbi =
        relative === 'native/bindings/include/boids/boids.h' && ['Config', 'State'].includes(name);

      if (!isPublicAbi && !/^E[A-Z][A-Za-z0-9]*$/.test(name)) {
        fail(match.index, `enum ${name}: use the E prefix.`);
      }
    } else {
      const prefix = /\btemplate\s*<[^{};]*>\s*$/.test(code.slice(0, match.index)) ? 'T' : 'F';

      if (!new RegExp(`^${prefix}[A-Z][A-Za-z0-9]*$`).test(name)) {
        fail(match.index, `type ${name}: use F for ordinary types and T for templates.`);
      }
    }
  }

  // Scalar declarations only; function names and library-required signatures are reviewed separately.
  for (const match of code.matchAll(
    /\bbool\s+(?:const\s+)?(?!operator\b)([A-Za-z_]\w*)\s*(?=[=;,){}]|:(?!:))/g,
  )) {
    if (!/^b[A-Z][A-Za-z0-9]*$/.test(match[1])) {
      fail(match.index, `bool ${match[1]}: use the b prefix.`);
    }
  }
}

assert.equal(failures.length, 0, failures.join('\n'));

console.log(
  `C++ naming, headers, using scopes and no-exceptions rules passed (${files.length} files; lexical checks only).`,
);
