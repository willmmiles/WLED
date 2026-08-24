'use strict';

const assert = require('node:assert');
const { describe, it, before, after } = require('node:test');
const fs = require('fs');
const os = require('os');
const path = require('path');
const child_process = require('child_process');
const util = require('util');
const zlib = require('node:zlib');
const execPromise = util.promisify(child_process.exec);

// Importing the build script must be side-effect free: with NODE_ENV=test it
// defines its helpers but must not run a build.
process.env.NODE_ENV = 'test';
const { applyUiContributions, checkUiTargets } = require('./cdata.js');

// The CLI decides whether to build from NODE_ENV, so child processes must not
// inherit the test harness's NODE_ENV=test (which suppresses the build).
const buildEnv = { ...process.env, NODE_ENV: 'production' };
const runCdata = (args = '') => execPromise('node tools/cdata.js ' + args, { env: buildEnv });

describe('Script', () => {
  const folderPath = 'wled00';
  const dataPath = path.join(folderPath, 'data');

  before(() => {
    process.env.NODE_ENV = 'production';
    // Backup files
    fs.cpSync("wled00/data", "wled00Backup", { recursive: true });
    fs.cpSync("tools/cdata.js", "cdata.bak.js");
    fs.cpSync("package.json", "package.bak.json");
  });
  after(() => {
    // Restore backup
    fs.rmSync("wled00/data", { recursive: true });
    fs.renameSync("wled00Backup", "wled00/data");
    fs.rmSync("tools/cdata.js");
    fs.renameSync("cdata.bak.js", "tools/cdata.js");
    fs.rmSync("package.json");
    fs.renameSync("package.bak.json", "package.json");
  });

  // delete all html_*.h files
  async function deleteBuiltFiles() {
    const files = await fs.promises.readdir(folderPath);
    await Promise.all(files.map(file => {
      if (file.startsWith('html_') && path.extname(file) === '.h') {
        return fs.promises.unlink(path.join(folderPath, file));
      }
    }));
  }

  // check if html_*.h files were created
  async function checkIfBuiltFilesExist() {
    const files = await fs.promises.readdir(folderPath);
    const htmlFiles = files.filter(file => file.startsWith('html_') && path.extname(file) === '.h');
    assert(htmlFiles.length > 0, 'html_*.h files were not created');
  }

  async function runAndCheckIfBuiltFilesExist() {
    await execPromise('node tools/cdata.js');
    await checkIfBuiltFilesExist();
  }

  async function checkIfFileWasNewlyCreated(file) {
    const modifiedTime = fs.statSync(file).mtimeMs;
    assert(Date.now() - modifiedTime < 850, file + ' was not modified');
  }

  async function testFileModification(sourceFilePath, resultFile) {
    // run cdata.js to ensure html_*.h files are created
    await execPromise('node tools/cdata.js');

    // modify file
    fs.appendFileSync(sourceFilePath, ' ');
    // delay for 1 second to ensure the modified time is different
    await new Promise(resolve => setTimeout(resolve, 1400));

    // run script cdata.js again and wait for it to finish
    await execPromise('node tools/cdata.js');

    await checkIfFileWasNewlyCreated(path.join(folderPath, resultFile));
  }

  describe('should build if', () => {
    it('html_*.h files are missing', async () => {
      await deleteBuiltFiles();
      await runAndCheckIfBuiltFilesExist();
    });

    it('only one html_*.h file is missing', async () => {
      // run script cdata.js and wait for it to finish
      await execPromise('node tools/cdata.js');

      // delete a random html_*.h file
      let files = await fs.promises.readdir(folderPath);
      let htmlFiles = files.filter(file => file.startsWith('html_') && path.extname(file) === '.h');
      const randomFile = htmlFiles[Math.floor(Math.random() * htmlFiles.length)];
      await fs.promises.unlink(path.join(folderPath, randomFile));

      await runAndCheckIfBuiltFilesExist();
    });

    it('script was executed with -f or --force', async () => {
      await execPromise('node tools/cdata.js');
      await new Promise(resolve => setTimeout(resolve, 1000));
      await execPromise('node tools/cdata.js --force');
      await checkIfFileWasNewlyCreated(path.join(folderPath, 'html_ui.h'));
      await new Promise(resolve => setTimeout(resolve, 1000));
      await execPromise('node tools/cdata.js -f');
      await checkIfFileWasNewlyCreated(path.join(folderPath, 'html_ui.h'));
    });

    it('a file changes', async () => {
      await testFileModification(path.join(dataPath, 'index.htm'), 'html_ui.h');
    });

    it('a inlined file changes', async () => {
      await testFileModification(path.join(dataPath, 'index.js'), 'html_ui.h');
    });

    it('a settings file changes', async () => {
      await testFileModification(path.join(dataPath, 'settings_leds.htm'), 'html_settings.h');
    });

    it('common.js changes', async () => {
      await testFileModification(path.join(dataPath, 'common.js'), 'html_settings.h');
    });

    // this testcase currently fails - might be due to npm updates (maybe "faking" a favicon.ico change is harder now), or a real regression
    // see https://github.com/wled/WLED/issues/5581
    // it('the favicon changes', async () => {
    //   await testFileModification(path.join(dataPath, 'favicon.ico'), 'html_other.h');
    // });

    it('cdata.js changes', async () => {
      await testFileModification('tools/cdata.js', 'html_ui.h');
    });

    it('package.json changes', async () => {
      await testFileModification('package.json', 'html_ui.h');
    });
  });

  describe('should not build if', () => {
    it('the files are already built', async () => {
      await deleteBuiltFiles();

      // run script cdata.js and wait for it to finish
      let startTime = Date.now();
      await execPromise('node tools/cdata.js');
      const firstRunTime = Date.now() - startTime;

      // run script cdata.js and wait for it to finish
      startTime = Date.now();
      await execPromise('node tools/cdata.js');
      const secondRunTime = Date.now() - startTime;

      // check if second run was faster than the first (must be at least 2x faster)
      assert(secondRunTime < firstRunTime / 2, 'html_*.h files were rebuilt');
    });
  });
});

describe('Dependency graph (--emit-deps / --depfile)', () => {
  const depfile = path.join(os.tmpdir(), `cdata-deps-${process.pid}.d`);

  after(() => {
    try { fs.rmSync(depfile); } catch { /* ignore */ }
  });

  it('emits a depfile listing every generated header, without building', async () => {
    const { stdout } = await runCdata(`--emit-deps --depfile "${depfile}"`);
    assert.match(stdout, /Wrote dependency graph/);
    assert.doesNotMatch(stdout, /Minified/); // nothing was actually built

    const dep = fs.readFileSync(depfile, 'utf8');
    for (const header of ['wled00/html_ui.h', 'wled00/html_settings.h', 'wled00/js_iro.h']) {
      assert.match(dep, new RegExp('^' + header.replace(/\./g, '\\.') + ':', 'm'),
        `depfile is missing a rule for ${header}`);
    }
  });

  it('records cdata.js, package.json and package-lock.json as inputs of every header', async () => {
    await runCdata(`--emit-deps --depfile "${depfile}"`);
    const dep = fs.readFileSync(depfile, 'utf8');
    for (const line of dep.split('\n')) {
      if (!line || line.startsWith('#') || !line.includes(':')) continue;
      assert.match(line, /tools\/cdata\.js/, 'every rule should depend on cdata.js');
      assert.match(line, /(^|\s)package\.json(\s|$)/, 'every rule should depend on package.json');
      // The lock file pins minifier versions, so an npm install that changes them
      // (without touching package.json) must still invalidate every header.
      assert.match(line, /package-lock\.json/, 'every rule should depend on package-lock.json');
    }
  });

  it("records an html job's whole source folder, not a snapshot of its files", async () => {
    // An html job inlines arbitrary resources from its source folder, so the
    // folder itself is the dependency.  That is what lets a file added to or
    // removed from wled00/data (e.g. wled00/data/icons-ui/Read Me.txt) count as a
    // dependency change -- rather than listing today's files and missing the next
    // one that appears.
    await runCdata(`--emit-deps --depfile "${depfile}"`);
    const dep = fs.readFileSync(depfile, 'utf8');
    const uiRule = dep.split('\n').find(l => l.startsWith('wled00/html_ui.h:'));
    assert(uiRule, 'no dependency rule for wled00/html_ui.h');
    assert.match(uiRule, /(^|\s)wled00\/data(\s|$)/, 'html_ui.h should depend on its source folder');
    // The folder is listed as one token, so individual files inside it -- and any
    // spaces in their names -- are not enumerated here.
    assert.doesNotMatch(uiRule, /Read/);
  });
});

describe('Usermod manifests', () => {
  let modDir;
  const manifest = () => path.join(modDir, 'cdata.json');

  before(() => {
    modDir = fs.mkdtempSync(path.join(os.tmpdir(), 'cdata-mod-'));
    fs.mkdirSync(path.join(modDir, 'data'));
    fs.writeFileSync(path.join(modDir, 'data', 'page.htm'),
      '<!doctype html><html><body><h1>Hi ##VERSION##</h1></body></html>');
    fs.writeFileSync(manifest(), JSON.stringify({
      header: [{
        output: 'html_mod.h',
        srcDir: 'data',
        specs: [{ file: 'page.htm', name: 'PAGE_mod', method: 'gzip', filter: 'html-minify' }],
      }],
    }));
  });

  after(() => {
    fs.rmSync(modDir, { recursive: true, force: true });
  });

  it('legacy single-manifest mode builds only that header', async () => {
    const out = path.join(modDir, 'html_mod.h');
    const { stdout } = await runCdata(`"${manifest()}"`);
    assert(fs.existsSync(out), 'usermod header was not built');
    assert.match(fs.readFileSync(out, 'utf8'), /PAGE_mod/);
    assert.doesNotMatch(stdout, /index\.htm|html_ui\.h/); // main UI is not part of this run
  });

  it('--manifest adds the usermod header to the graph alongside the main UI', async () => {
    const depfile = path.join(modDir, 'deps.d');
    await runCdata(`--emit-deps --depfile "${depfile}" --manifest "${manifest()}"`);
    const dep = fs.readFileSync(depfile, 'utf8');
    assert.match(dep, /html_mod\.h:/);        // the usermod output
    assert.match(dep, /wled00\/html_ui\.h:/); // together with the main UI
  });

  it('rejects a manifest with an unknown top-level key', async () => {
    const bad = path.join(modDir, 'bad.json');
    fs.writeFileSync(bad, JSON.stringify({
      inject: {}, // reserved for the future, not accepted by current tooling
      header: [{
        output: 'html_bad.h',
        specs: [{ file: 'page.htm', name: 'PAGE_bad', method: 'gzip', filter: 'html-minify' }],
      }],
    }));
    await assert.rejects(runCdata(`"${bad}"`), /unknown top-level key/);
    fs.rmSync(bad);
  });

  it('builds one header per header op in a multi-header manifest', async () => {
    const multi = path.join(modDir, 'multi.json');
    fs.writeFileSync(multi, JSON.stringify({
      header: [
        { output: 'html_a.h', srcDir: 'data',
          specs: [{ file: 'page.htm', name: 'PAGE_a', method: 'gzip', filter: 'html-minify' }] },
        { output: 'html_b.h', srcDir: 'data',
          specs: [{ file: 'page.htm', name: 'PAGE_b', method: 'gzip', filter: 'html-minify' }] },
      ],
    }));
    await runCdata(`"${multi}"`);
    assert.match(fs.readFileSync(path.join(modDir, 'html_a.h'), 'utf8'), /PAGE_a/);
    assert.match(fs.readFileSync(path.join(modDir, 'html_b.h'), 'utf8'), /PAGE_b/);
    fs.rmSync(multi);
  });

  it('emits a valid raw string for a plaintext spec with no delimiters', async () => {
    const pt = path.join(modDir, 'plain.json');
    fs.writeFileSync(pt, JSON.stringify({
      header: [{ output: 'html_plain.h', srcDir: 'data',
        specs: [{ file: 'page.htm', name: 'PAGE_plain', method: 'plaintext' }] }],
    }));
    await runCdata(`"${pt}"`);
    const out = fs.readFileSync(path.join(modDir, 'html_plain.h'), 'utf8');
    // A safe matching delimiter pair is filled in, so the literal is R"=====(...)====="
    // rather than an invalid bare R"...".
    assert.match(out, /const char PAGE_plain\[\] PROGMEM = R"=====\(/);
    assert.match(out, /\)=====";/);
    assert.doesNotMatch(out, /R"[^=]/); // never a bare/undelimited raw string
    fs.rmSync(pt);
    fs.rmSync(path.join(modDir, 'html_plain.h'));
  });

  it('defaults a spec method to gzip when none is given', async () => {
    const g = path.join(modDir, 'gz.json');
    fs.writeFileSync(g, JSON.stringify({
      header: [{ output: 'html_gz.h', srcDir: 'data',
        specs: [{ file: 'page.htm', name: 'PAGE_gz' }] }],
    }));
    await runCdata(`"${g}"`);
    const out = fs.readFileSync(path.join(modDir, 'html_gz.h'), 'utf8');
    // gzip encodes as a PROGMEM byte array with a _length, not a raw string.
    assert.match(out, /const uint8_t PAGE_gz\[\] PROGMEM = \{/);
    assert.match(out, /const uint16_t PAGE_gz_length =/);
    fs.rmSync(g);
    fs.rmSync(path.join(modDir, 'html_gz.h'));
  });

  it("infers a spec's filter from its extension, and honours an explicit override", async () => {
    // A source with a strippable comment and collapsible whitespace, so the
    // difference between css-minify and plain is visible in the output.
    fs.writeFileSync(path.join(modDir, 'data', 'style.css'), '/* a comment */\n.pm {\n  color: red;\n}\n');
    const c = path.join(modDir, 'css.json');
    fs.writeFileSync(c, JSON.stringify({
      header: [{ output: 'css_mod.h', srcDir: 'data', specs: [
        // No filter: .css infers css-minify, so the comment is stripped.  (plaintext
        // keeps the result human-readable in the header so the test can inspect it.)
        { file: 'style.css', name: 'CSS_min', method: 'plaintext' },
        // An explicit 'plain' overrides the inference and emits the file verbatim.
        { file: 'style.css', name: 'CSS_raw', method: 'plaintext', filter: 'plain' },
      ] }],
    }));
    await runCdata(`"${c}"`);
    const out = fs.readFileSync(path.join(modDir, 'css_mod.h'), 'utf8');
    const minChunk = out.slice(out.indexOf('const char CSS_min'), out.indexOf('const char CSS_raw'));
    const rawChunk = out.slice(out.indexOf('const char CSS_raw'));
    assert.doesNotMatch(minChunk, /a comment/);      // css-minify stripped it
    assert.match(rawChunk, /\/\* a comment \*\//);   // plain kept it verbatim
    fs.rmSync(c);
    fs.rmSync(path.join(modDir, 'css_mod.h'));
    fs.rmSync(path.join(modDir, 'data', 'style.css'));
  });

  // The build hard-fails on any invalid manifest (cdata.js is the single
  // validator; the Python layer only forwards paths).  Each case asserts the
  // CLI exits non-zero with a message identifying the specific problem.
  const spec = { file: 'page.htm', name: 'PAGE_x', method: 'gzip', filter: 'html-minify' };
  const invalidManifests = {
    'malformed JSON': { raw: '{ "header": [ }', error: /Could not read manifest/ },
    'a header op missing output': {
      json: { header: [{ specs: [spec] }] }, error: /missing a string 'output'/ },
    'an empty header array': {
      json: { header: [] }, error: /missing or empty 'header' array/ },
    // A manifest may omit 'header' (a ui-only usermod), but it has to contribute
    // something -- an empty manifest is a mistake, not a valid no-op.
    'a manifest that contributes nothing': {
      json: { schemaVersion: 1 }, error: /must define at least one of 'header' or 'ui'/ },
    'an unsupported schemaVersion': {
      json: { schemaVersion: 2, header: [{ output: 'x.h', specs: [spec] }] },
      error: /unsupported schemaVersion 2/ },
    'two header ops writing the same output': {
      json: { header: [
        { output: 'dup.h', srcDir: 'data', specs: [{ ...spec, name: 'PAGE_a' }] },
        { output: 'dup.h', srcDir: 'data', specs: [{ ...spec, name: 'PAGE_b' }] },
      ] },
      error: /collides with an earlier header op/ },
    // Path traversal: no manifest-supplied path may escape the usermod folder.
    'an output escaping the usermod folder': {
      json: { header: [{ output: '../escape.h', specs: [spec] }] },
      error: /output '\.\.\/escape\.h' resolves outside the usermod folder/ },
    'an absolute output path': {
      json: { header: [{ output: '/tmp/evil.h', specs: [spec] }] },
      error: /resolves outside the usermod folder/ },
    'a srcDir escaping the usermod folder': {
      json: { header: [{ output: 'x.h', srcDir: '../../elsewhere', specs: [spec] }] },
      error: /srcDir '\.\.\/\.\.\/elsewhere' resolves outside the usermod folder/ },
    'a spec file escaping the usermod folder': {
      json: { header: [{ output: 'x.h', srcDir: 'data', specs: [{ ...spec, file: '../../../../etc/hosts' }] }] },
      error: /file '.*etc\/hosts' resolves outside the usermod folder/ },
    // Spec value validation: bad values must fail loudly, not reach codegen.
    'a spec name that is not a C identifier': {
      json: { header: [{ output: 'x.h', srcDir: 'data', specs: [{ ...spec, name: 'not a name!' }] }] },
      error: /needs a 'name' that is a valid C identifier/ },
    'an unknown spec method': {
      json: { header: [{ output: 'x.h', srcDir: 'data', specs: [{ ...spec, method: 'zip' }] }] },
      error: /invalid 'method' "zip"/ },
    'an unknown spec filter': {
      json: { header: [{ output: 'x.h', srcDir: 'data', specs: [{ ...spec, filter: 'htlm-minify' }] }] },
      error: /invalid 'filter' "htlm-minify"/ },
    'a plaintext spec with only one delimiter': {
      json: { header: [{ output: 'x.h', srcDir: 'data',
        specs: [{ file: 'page.htm', name: 'PAGE_x', method: 'plaintext', prepend: '=====(' }] }] },
      error: /must set both 'prepend' and 'append'/ },
  };

  for (const [desc, { raw, json, error }] of Object.entries(invalidManifests)) {
    it(`rejects ${desc}`, async () => {
      const bad = path.join(modDir, 'invalid.json');
      fs.writeFileSync(bad, raw !== undefined ? raw : JSON.stringify(json));
      await assert.rejects(runCdata(`"${bad}"`), error);
      fs.rmSync(bad);
    });
  }
});

// A manifest's `ui` section contributes source files to one of WLED's own web UI
// bundles instead of generating a header of its own: the files are spliced into
// the page and minified and compressed along with it.
describe('Usermod UI contributions', () => {
  // A stand-in for the inlined index.htm.  All the splice needs is the two
  // closing tags it anchors on.
  const page = '<!doctype html><html><head><style>a{}</style></head><body><div id="app"></div></body></html>';
  // The shape loadUiContributions() produces, built by hand so the splice itself
  // can be exercised as the pure string operation it is.
  const src = (file, type, contents) =>
    ({ file, type, contents, basename: path.basename(file, path.extname(file)) });
  const contribution = (module, ...sources) => ({ module, sources });

  describe('splicing into the page', () => {
    it('inserts css before the final </head>', () => {
      const out = applyUiContributions(page, [contribution('pm', src('pm.css', 'css', '.pm{color:red}'))]);
      assert.match(out, /<style>\.pm\{color:red\}<\/style><\/head>/);
      // A css-only contribution adds nothing to the body.
      assert.strictEqual(out.slice(out.indexOf('<body>')), page.slice(page.indexOf('<body>')));
    });

    it('inserts templates and scripts before the final </body>, templates first', () => {
      // The script is listed first on purpose: within one contribution the
      // templates are always emitted first, so the script can find them.
      const out = applyUiContributions(page, [
        contribution('pm', src('pm.js', 'script', 'go();'), src('panel.htm', 'template', '<p>hi</p>')),
      ]);
      const template = out.indexOf('<template id="um-pm-panel"><p>hi</p></template>');
      const script = out.indexOf('WLED.module("pm"');
      assert(template > 0, 'the template was not spliced in');
      assert(out.indexOf('<div id="app">') < template, 'contributions belong at the end of the body');
      assert(template < script, "a contribution's templates must precede its script");
      assert(script < out.indexOf('</body>'), 'contributions belong before </body>');
    });

    it('wraps a script so a trailing comment cannot swallow the closing brace', () => {
      const out = applyUiContributions(page, [contribution('pm', src('pm.js', 'script', 'go(); // trailing'))]);
      assert(out.includes('<script>WLED.module("pm",function(WLED){\ngo(); // trailing\n});</script>'),
        'the module wrapper text (including its newlines) is not what modules are compiled against');
    });

    it('keeps contributions in the order they were supplied, never sorted', () => {
      const out = applyUiContributions(page, [
        contribution('zeta', src('z.css', 'css', '.z{}'), src('z.js', 'script', 'Z();')),
        contribution('alpha', src('a.css', 'css', '.a{}'), src('a.js', 'script', 'A();')),
      ]);
      // Manifest order decides the order contributed code runs in; sorting would
      // make that depend on the module names instead.
      assert(out.indexOf('.z{}') < out.indexOf('.a{}'), 'css must follow manifest order');
      assert(out.indexOf('"zeta"') < out.indexOf('"alpha"'), 'scripts must follow manifest order');
    });

    it('emits the extension API ahead of any contributed code', () => {
      const out = applyUiContributions(page, [contribution('pm', src('pm.js', 'script', 'go();'))], 'API();');
      assert(out.includes('<script>API();</script>'), 'the extension API was not spliced in');
      assert(out.indexOf('API();') < out.indexOf('WLED.module'), 'the API must load before the modules using it');
    });

    it('leaves the page untouched when nothing is contributed', () => {
      // index.js carries an inert stub, so a build with no usermod UI must not
      // pay for the extension API.
      assert.strictEqual(applyUiContributions(page, [], 'API();'), page);
    });

    // Contributed files are third-party build inputs: text that closes the tag
    // they are wrapped in would spill the rest of the file into the document.
    for (const [type, file, contents] of [
      ['script', 'pm.js', 'var s = "</script>";'],
      ['css', 'pm.css', '.pm{content:"</style>"}'],
      ['template', 'pm.htm', '<p></template></p>'],
    ]) {
      it(`rejects a ${type} source that closes its own tag early`, () => {
        assert.throws(
          () => applyUiContributions(page, [contribution('pm', src(file, type, contents))]),
          new RegExp(file.replace('.', '\\.') + ': a contributed ' + type + ' file must not contain'));
      });
    }

    it('rejects a script that does not parse, naming the file', () => {
      // The page minifier keeps JavaScript it cannot parse rather than failing,
      // so nothing else would stop a broken contribution from being shipped.
      assert.throws(
        () => applyUiContributions(page, [contribution('pm', src('pm.js', 'script', 'function ( { oops'))]),
        /pm\.js: contributed script does not parse/);
    });

    it('accepts a script that only parses inside the module wrapper', () => {
      // The contents become a function body, so `return` at the top level of a
      // contributed file is legal and must not be rejected.
      assert.doesNotThrow(
        () => applyUiContributions(page, [contribution('pm', src('pm.js', 'script', 'if (!WLED) return;\ngo();'))]));
    });
  });

  describe('target wiring', () => {
    // A ui target names the mainJobs entry that builds the bundle by its `page`,
    // and nothing but convention links the two.  Importing cdata.js at all
    // asserts the real table (the check runs at load); these cover both outcomes.
    it('accepts a job list that builds every ui target', () => {
      assert.doesNotThrow(() => checkUiTargets([{ kind: 'html', page: 'index' }]));
    });

    it('rejects a job list that has drifted away from a ui target', () => {
      assert.throws(() => checkUiTargets([{ kind: 'html', page: 'renamed' }]),
        /ui target 'index' names no html job/);
      // A chunks job with a matching name is not a bundle contributions can go in.
      assert.throws(() => checkUiTargets([{ kind: 'chunks', page: 'index' }]),
        /ui target 'index' names no html job/);
    });
  });

  describe('manifest validation', () => {
    let modDir;
    const depfile = () => path.join(modDir, 'deps.d');
    const write = (name, json) => {
      const p = path.join(modDir, name);
      fs.writeFileSync(p, JSON.stringify(json));
      return p;
    };
    // A manifest's ui section is validated before anything is built, so these run
    // against --emit-deps: that proves it, and keeps them cheap.
    const emitDeps = (manifestPath) =>
      runCdata(`--emit-deps --depfile "${depfile()}" --manifest "${manifestPath}"`);
    // A well-formed op, with the parts a case is testing overridden.
    const uiOp = (op) => ({ ui: [{ target: 'index', srcDir: 'ui', ...op }] });

    before(() => {
      modDir = fs.mkdtempSync(path.join(os.tmpdir(), 'cdata-ui-'));
      fs.mkdirSync(path.join(modDir, 'ui'));
      fs.writeFileSync(path.join(modDir, 'ui', 'pm.js'), 'go();\n');
    });
    after(() => fs.rmSync(modDir, { recursive: true, force: true }));

    it('accepts a manifest that contributes ui and no header', async () => {
      const { stdout } = await emitDeps(write('uionly.json', uiOp({ sources: ['pm.js'] })));
      assert.match(stdout, /Wrote dependency graph/);
    });

    it('makes contributed sources and the manifest inputs of the bundle', async () => {
      await emitDeps(write('deps.json', uiOp({ sources: ['pm.js'] })));
      const rule = fs.readFileSync(depfile(), 'utf8').split('\n').find(l => l.startsWith('wled00/html_ui.h:'));
      assert(rule, 'no dependency rule for wled00/html_ui.h');
      // Without these, editing a usermod's UI would not rebuild the page it is
      // compiled into.
      assert.match(rule, /ui\/pm\.js(\s|$)/, 'a contributed source must be an input of the bundle');
      assert.match(rule, /deps\.json(\s|$)/, 'the contributing manifest must be an input of the bundle');
    });

    it('warns that legacy single-manifest mode skips ui contributions', async () => {
      // That mode does not build a core bundle, so there is nothing to splice
      // into -- but dropping the usermod's UI in silence would look like a bug.
      const { stderr } = await runCdata(`"${write('legacy.json', uiOp({ sources: ['pm.js'] }))}"`);
      assert.match(stderr, /skipped in legacy single-manifest mode/);
    });

    it('rejects two usermods claiming the same module name', async () => {
      // A module name namespaces template ids and WLED.module() registrations
      // across the whole bundle, and it defaults to the folder name -- so a
      // collision is something neither author necessarily chose, and the error
      // has to name both manifests for either of them to act on it.
      const mine = write('mine.json', uiOp({ module: 'shared', sources: ['pm.js'] }));
      const theirs = write('theirs.json', uiOp({ module: 'shared', sources: ['pm.js'] }));
      await assert.rejects(
        runCdata(`--emit-deps --depfile "${depfile()}" --manifest "${mine}" --manifest "${theirs}"`),
        (e) => {
          assert.match(e.message, /Conflicting UI module name 'shared' for target 'index'/);
          assert.match(e.message, /mine\.json/);
          assert.match(e.message, /theirs\.json/);
          assert.match(e.message, /Set an explicit 'module' in one of them/);
          return true;
        });
    });

    it('accepts two usermods contributing under different module names', async () => {
      const mine = write('a.json', uiOp({ module: 'alpha', sources: ['pm.js'] }));
      const theirs = write('b.json', uiOp({ module: 'beta', sources: ['pm.js'] }));
      const { stdout } = await runCdata(
        `--emit-deps --depfile "${depfile()}" --manifest "${mine}" --manifest "${theirs}"`);
      assert.match(stdout, /Wrote dependency graph/);
    });

    it("rejects a folder name that cannot be used as the default module name", async () => {
      // 'module' defaults to the usermod folder's name, which ends up in an HTML
      // id and a JS string literal; an unusable one must say so, not be mangled.
      const oddDir = path.join(modDir, 'power manager');
      fs.mkdirSync(oddDir);
      fs.writeFileSync(path.join(oddDir, 'cdata.json'), JSON.stringify(uiOp({ sources: ['pm.js'] })));
      await assert.rejects(emitDeps(path.join(oddDir, 'cdata.json')),
        /ui\[0\] cannot use the usermod folder name "power manager" as its 'module'.*set 'module' explicitly/);
    });

    const invalidUi = {
      'a ui op with no target': {
        json: { ui: [{ srcDir: 'ui', sources: ['pm.js'] }] },
        error: /ui\[0\] is missing a string 'target'/ },
      'an unknown target': {
        json: uiOp({ target: 'settings', sources: ['pm.js'] }),
        error: /ui\[0\] has an unknown 'target' "settings" \(expected one of: index\)/ },
      'an unsupported api version': {
        json: uiOp({ api: 2, sources: ['pm.js'] }),
        error: /ui\[0\] requests unsupported 'api' 2/ },
      'an empty sources array': {
        json: uiOp({ sources: [] }),
        error: /ui\[0\] is missing a non-empty 'sources' array/ },
      'a source whose extension implies no type': {
        json: uiOp({ sources: ['pm.txt'] }),
        error: /ui\[0\]\.sources\[0\] file 'pm\.txt' has an unrecognized extension/ },
      'a module name that is not id-safe': {
        json: uiOp({ module: 'power manager', sources: ['pm.js'] }),
        error: /ui\[0\] has an invalid 'module' "power manager"/ },
      'two ui ops for the same target': {
        json: { ui: [
          { target: 'index', srcDir: 'ui', sources: ['pm.js'] },
          { target: 'index', srcDir: 'ui', sources: ['pm.js'] },
        ] },
        error: /ui\[1\] target 'index' duplicates an earlier ui op/ },
      // Path traversal: a ui op's paths are constrained exactly as a header op's.
      'a srcDir escaping the usermod folder': {
        json: uiOp({ srcDir: '../../elsewhere', sources: ['pm.js'] }),
        error: /ui\[0\] srcDir '\.\.\/\.\.\/elsewhere' resolves outside the usermod folder/ },
      'a source file escaping the usermod folder': {
        json: uiOp({ sources: ['../../../../etc/hosts.js'] }),
        error: /ui\[0\]\.sources\[0\] file '.*etc\/hosts\.js' resolves outside the usermod folder/ },
    };

    for (const [desc, { json, error }] of Object.entries(invalidUi)) {
      it(`rejects ${desc}`, async () => {
        await assert.rejects(emitDeps(write('invalid.json', json)), error);
      });
    }
  });

  describe('building the main UI', () => {
    let modDir;

    before(() => {
      modDir = fs.mkdtempSync(path.join(os.tmpdir(), 'powermanager-'));
      fs.mkdirSync(path.join(modDir, 'ui'));
      fs.writeFileSync(path.join(modDir, 'ui', 'pm.css'), '.pm-widget{color:red}\n');
      fs.writeFileSync(path.join(modDir, 'ui', 'panel.htm'), '<p>power ##VERSION##</p>\n');
      fs.writeFileSync(path.join(modDir, 'ui', 'pm.js'), 'console.log("pm"); // trailing\n');
      fs.writeFileSync(path.join(modDir, 'cdata.json'), JSON.stringify({
        schemaVersion: 1,
        ui: [{ target: 'index', srcDir: 'ui', module: 'powermanager',
               sources: ['pm.css', 'panel.htm', 'pm.js'] }],
      }));
    });

    // These build the real wled00/html_ui.h, so regenerate it without the
    // usermod afterwards -- otherwise the tree keeps a header carrying this
    // temporary module's code.
    after(async () => {
      fs.rmSync(modDir, { recursive: true, force: true });
      await runCdata('-f');
    });

    // Recover the page the firmware would serve: html_ui.h holds the gzipped
    // payload as a C byte array.
    function decodeIndexPage() {
      const header = fs.readFileSync(path.join('wled00', 'html_ui.h'), 'utf8');
      const m = header.match(/PAGE_index\[\] PROGMEM = \{([\s\S]*?)\n\};/);
      assert(m, 'html_ui.h does not contain a PAGE_index array');
      const bytes = m[1].trim().split(/,\s*/).map((b) => parseInt(b, 16));
      return zlib.gunzipSync(Buffer.from(bytes)).toString('utf8');
    }

    it('compiles the contributed files into html_ui.h', async () => {
      await runCdata(`-f --manifest "${path.join(modDir, 'cdata.json')}"`);
      const html = decodeIndexPage();
      assert.match(html, /WLED\.module\("powermanager"/, 'the module wrapper is missing from the page');
      assert.match(html, /<template id=["']?um-powermanager-panel["']?/, 'the template is missing from the page');
      assert.match(html, /\.pm-widget\{color:red\}/, 'the contributed css is missing from the page');
      // Contributed files get the same ##VERSION## substitution as core sources.
      assert.doesNotMatch(html, /##VERSION##/);
    });

    // The contribution set is configuration, not an input file, so no mtime moves when
    // a usermod joins or leaves the build.  Without the fingerprint recorded in the
    // header, dropping a usermod left its code compiled into the page indefinitely.
    it('rebuilds when the contribution set changes, and only then', async () => {
      const manifest = `--manifest "${path.join(modDir, 'cdata.json')}"`;
      await runCdata(`-f ${manifest}`);
      assert.match(decodeIndexPage(), /WLED\.module\("powermanager"/);

      // Dropping the usermod must take its code with it, with no forced rebuild.
      const dropped = await runCdata('');
      assert.doesNotMatch(dropped.stdout, /already built/);
      assert.doesNotMatch(decodeIndexPage(), /WLED\.module\("powermanager"/,
                          'the departed usermod is still compiled into the page');

      // Adding it back must bring it back.
      const added = await runCdata(manifest);
      assert.doesNotMatch(added.stdout, /already built/);
      assert.match(decodeIndexPage(), /WLED\.module\("powermanager"/);

      // An unchanged set must still be a no-op: this must not defeat incremental builds.
      const again = await runCdata(manifest);
      assert.match(again.stdout, /already built/);
    });

    it('fails the build if a contributed script would close its own tag', async () => {
      const evil = path.join(modDir, 'evil.json');
      fs.writeFileSync(path.join(modDir, 'ui', 'evil.js'), 'var s = "</script>";\n');
      fs.writeFileSync(evil, JSON.stringify({
        ui: [{ target: 'index', srcDir: 'ui', module: 'evil', sources: ['evil.js'] }],
      }));
      await assert.rejects(runCdata(`-f --manifest "${evil}"`), /evil\.js: a contributed script file must not contain/);
    });
  });
});
