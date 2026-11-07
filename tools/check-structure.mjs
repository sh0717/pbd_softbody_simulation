import './check-native-layout.mjs';
import './check-layout.mjs';
// Fast architecture checks. Numerical and browser behavior have separate test suites.
import './check-cpp-standard.mjs';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { readdir, readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const required = [
  'native/src',
  'native/bindings',
  'native/sources.txt',
  'native/CMakeLists.txt',
  'src/app.js',
  'src/simulation',
  'src/rendering',
  'src/ui',
  'tools',
  'tests/unit',
  'tests/native',
  'tests/browser',
  'tests/fixtures',
  'CMakeLists.txt',
  'package.json',
  '.clang-format',
  '.prettierrc.json',
  '.editorconfig',
  'docs/architecture.md',
  'docs/project-conventions.md',
];
for (const file of required) assert.ok(existsSync(path.join(root, file)), `Missing: ${file}`);
for (const old of [
  'cpp',
  'scripts',
  'tests/reference',
  'app.js',
  'physics.js',
  'soft-contacts.js',
  'bunny-data.js',
]) {
  assert.ok(!existsSync(path.join(root, old)), `Obsolete active path: ${old}`);
}
const manifest = (await readFile(path.join(root, 'native/sources.txt'), 'utf8'))
  .split(/\r?\n/)
  .map((line) => line.trim())
  .filter(Boolean);
assert.equal(new Set(manifest).size, manifest.length, 'Duplicate source manifest entries');
for (const file of manifest) {
  assert.ok(
    file.startsWith('native/src/') || file.startsWith('native/bindings/'),
    `Invalid core source: ${file}`,
  );
  assert.ok(existsSync(path.join(root, file)), `Missing core source: ${file}`);
}
const pkg = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
for (const name of [
  'start',
  'build',
  'build:core',
  'test',
  'test:native',
  'test:browser',
  'check:format',
  'format',
  'check:structure',
  'check',
]) {
  assert.ok(pkg.scripts[name], `Missing npm command: ${name}`);
}

async function walk(directory) {
  if (!existsSync(directory)) return [];
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    if (['generated', 'fixtures', 'data', '__pycache__'].includes(entry.name)) continue;
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) files.push(...(await walk(file)));
    else if (/\.(?:[cm]?js|cpp|h)$/.test(entry.name)) files.push(file);
  }
  return files;
}
const files = (
  await Promise.all(
    ['src', 'native', 'tools', 'tests/unit', 'tests/browser'].map((dir) =>
      walk(path.join(root, dir)),
    ),
  )
).flat();
const implementations = files
  .map((file) => path.relative(root, file).replaceAll('\\', '/'))
  .filter((file) => /^(?:native\/src|native\/bindings)\/.*\.cpp$/.test(file));
for (const file of implementations)
  assert.ok(manifest.includes(file), `Core source omitted from manifest: ${file}`);
for (const file of manifest) {
  assert.ok(
    implementations.includes(file),
    `Manifest source is outside the domain implementation set: ${file}`,
  );
}
for (const name of ['index.html', 'pbd_model.html']) {
  const entry = path.join(root, name);
  if (!existsSync(entry)) continue;
  const html = await readFile(entry, 'utf8');
  for (const [, reference] of html.matchAll(/(?:src|href)\s*=\s*['"]([^'"]+)['"]/g)) {
    if (/^(?:[a-z]+:|\/\/|#)/i.test(reference)) continue;
    const local = reference.split(/[?#]/)[0];
    if (local)
      assert.ok(existsSync(path.resolve(root, local)), `${name}: missing asset ${reference}`);
  }
}
for (const file of files) {
  const source = await readFile(file, 'utf8');
  const relative = path.relative(root, file).replaceAll('\\', '/');
  if (/\.[cm]?js$/.test(file)) {
    const imports = [
      ...source.matchAll(
        /(?:\bfrom\s*|\bimport\s*(?:\(\s*)?|\brequire\s*\(\s*)['"](\.[^'"]+)['"]/g,
      ),
    ];
    // A URL may name a future output (artifacts/, build/, etc.). Only module
    // loading expressions are dependencies that must already exist in a clone.
    for (const [, specifier] of imports) {
      assert.ok(
        existsSync(path.resolve(path.dirname(file), specifier)),
        `${relative}: missing import ${specifier}`,
      );
      if (relative.startsWith('src/'))
        assert.ok(!specifier.includes('fixtures'), `${relative}: runtime fixture dependency`);
      if (relative.startsWith('src/simulation/'))
        assert.ok(
          !/\/(?:rendering|ui)\//.test(specifier),
          `${relative}: simulation depends on presentation`,
        );
      if (relative.startsWith('src/rendering/'))
        assert.ok(!/\/ui\//.test(specifier), `${relative}: rendering depends on UI`);
    }
    if (relative.startsWith('src/simulation/')) {
      assert.ok(
        !/\b(?:document|window)\s*[.[]/.test(source),
        `${relative}: simulation depends on DOM`,
      );
    }
  }
  if (relative.startsWith('native/src/') && /\.(?:cpp|h)$/.test(file)) {
    assert.ok(
      !/^[ \t]*#include\s*[<"]emscripten\//m.test(source),
      `${relative}: Emscripten belongs in bindings`,
    );
    for (const [, include] of source.matchAll(/^[ \t]*#include\s*"([^"]+)"/gm)) {
      const resolved = [
        path.dirname(file),
        ...['native/src', 'native/bindings/include', 'native/bindings'].map((dir) =>
          path.join(root, dir),
        ),
      ]
        .map((dir) => path.resolve(dir, include))
        .find((candidate) => existsSync(candidate));
      if (resolved)
        assert.ok(
          !path.relative(root, resolved).replaceAll('\\', '/').startsWith('native/bindings/') ||
            path
              .relative(root, resolved)
              .replaceAll('\\', '/')
              .startsWith('native/bindings/include/'),
          `${relative}: domain depends on binding header ${include}`,
        );
    }
  }
}
console.log(
  `Structure and dependency boundaries passed (${manifest.length} native units, ${files.length} source files).`,
);
