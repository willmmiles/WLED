Import("env")
import os
import re
import subprocess
from pathlib import Path   # For OS-agnostic path manipulation
import shutil
from SCons.Script import Exit

# The web UI build banner lives here (Python), not in cdata.js, so it prints
# once per build during script evaluation -- before any compilation output --
# instead of appearing partway through the build (or not at all) whenever the
# node process happened to run.
WLED_BANNER = (
    "\n"
    "\t\x1b[34m  ##  ##      ##        ######    ######\n"
    "\t\x1b[34m##      ##    ##      ##        ##  ##  ##\n"
    "\t\x1b[34m##  ##  ##  ##        ######        ##  ##\n"
    "\t\x1b[34m##  ##  ##  ##        ##            ##  ##\n"
    "\t\x1b[34m  ##  ##      ######    ######    ######\n"
    "\t\t\x1b[36m build script for web UI\n"
    "\x1b[0m"
)
print(WLED_BANNER)

node_ex = shutil.which("node")
# Check if Node.js is installed and present in PATH if it failed, abort the build
if node_ex is None:
    print('\x1b[0;31;43m' + 'Node.js is not installed or missing from PATH html css js will not be processed check https://kno.wled.ge/advanced/compiling-wled/' + '\x1b[0m')
    exitCode = env.Execute("null")
    exit(exitCode)

PROJECT_DIR = Path(env["PROJECT_DIR"]).resolve()
CDATA_JS = PROJECT_DIR / "tools" / "cdata.js"


# --- Dependency graph -----------------------------------------------------------
#
# cdata.js is the single source of truth for which web UI headers exist and what
# they depend on; it emits that graph as a Makefile-style ".d" depfile.  We read
# that cached depfile to declare a single SCons Command -- covering the main UI
# and every usermod -- with its complete list of target headers and input files.
# The actual build (one node invocation) then runs as a normal build node, at
# build time, ordered ahead of anything that #includes the generated headers.

def _graph_inputs(manifests):
    """Files whose change may alter the *shape* of the graph (which outputs exist
    / what depends on what), so the cached depfile can no longer be trusted."""
    inputs = [CDATA_JS, PROJECT_DIR / "package.json"]
    for name in ("platformio.ini", "platformio_override.ini"):
        p = PROJECT_DIR / name
        if p.exists():
            inputs.append(p)
    inputs.extend(Path(m) for m in manifests)
    return inputs


def _depfile_structure_current(depfile, manifests):
    """True if the depfile exists and is newer than every structural input."""
    if not depfile.exists():
        return False
    depfile_mtime = depfile.stat().st_mtime
    for src in _graph_inputs(manifests):
        try:
            if src.stat().st_mtime > depfile_mtime:
                return False
        except OSError:
            pass
    return True


def _resolve_dep(token):
    """Un-escape a depfile token ("\\ " -> " ") and resolve it against the project."""
    token = token.strip().replace('\\ ', ' ')
    return os.path.normpath(os.path.join(str(PROJECT_DIR), token))


def _parse_depfile(depfile):
    """Read a Makefile-style depfile into (targets, sources) lists of absolute
    paths.  Paths are project-relative with forward slashes (no Windows
    drive-letter colons); spaces within a path are escaped as "\\ ", so the
    dependency list is split only on *unescaped* whitespace."""
    targets, sources, seen = [], [], set()
    for line in depfile.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('#') or ':' not in line:
            continue
        target, deps = line.split(':', 1)
        targets.append(_resolve_dep(target))
        for dep in re.split(r'(?<!\\)\s+', deps.strip()):
            if not dep:
                continue
            ap = _resolve_dep(dep)
            if ap not in seen:
                seen.add(ap)
                sources.append(ap)
    return targets, sources


def _emit_depfile(depfile, manifests):
    """Write the dependency graph WITHOUT building the UI.  `--emit-deps` only
    enumerates files (Node builtins, no node_modules, no minify), so this is a
    cheap graph-description step -- not a build -- used only when the cached
    depfile is missing or structurally stale, so the Command below can be
    declared with a complete and current target/source list."""
    depfile.parent.mkdir(parents=True, exist_ok=True)
    cmd = [node_ex, str(CDATA_JS), "--emit-deps", "--depfile", str(depfile)]
    for m in manifests:
        cmd += ["--manifest", m]
    try:
        subprocess.run(cmd, cwd=str(PROJECT_DIR), check=True)
    except subprocess.CalledProcessError:
        # cdata.js has already printed the specific reason (e.g. an invalid
        # usermod manifest) to stderr.  Stop the build cleanly with that message
        # rather than surfacing a Python CalledProcessError traceback.
        print('\x1b[0;31;43m' + 'Web UI dependency scan failed -- see the cdata.js error above.' + '\x1b[0m')
        Exit(1)


def _ensure_node_modules(env):
    """Install node packages only when needed, rather than on every UI build."""
    node_modules = PROJECT_DIR / "node_modules"
    lock = PROJECT_DIR / "package-lock.json"
    stale = (not node_modules.exists()) or (
        lock.exists() and lock.stat().st_mtime > node_modules.stat().st_mtime
    )
    if stale:
        print('\x1b[6;33;42m' + 'Installing node packages' + '\x1b[0m')
        rc = env.Execute("npm ci")
        if rc:
            print('\x1b[0;31;43m' + 'npm ci failed check https://kno.wled.ge/advanced/compiling-wled/' + '\x1b[0m')
            Exit(rc)


def _wire_object_methods(target_env, cmd):
    """Make every object compiled by target_env require the UI build command, so
    the generated headers exist before anything #including them compiles -- even
    under a parallel (-j) build."""
    for name in ("Object", "StaticObject", "SharedObject"):
        if not hasattr(target_env, name):
            continue
        orig = getattr(target_env, name)
        def wrapped(*args, _orig=orig, _env=target_env, **kwargs):
            nodes = _orig(*args, **kwargs)
            _env.Requires(nodes, cmd)
            return nodes
        setattr(target_env, name, wrapped)


# Guard so the UI build is registered at most once per environment.
_registered = {"done": False}


def _register_ui_build(xenv, result):
    if _registered["done"]:
        return
    _registered["done"] = True

    # Usermod HTML manifests discovered by load_usermods.py (may be empty).
    manifests = list(xenv.get("WLED_UI_MANIFESTS", []))
    depfile = Path(xenv.subst("$BUILD_DIR")).resolve() / "ui_deps.d"

    # Make sure the cached graph is present and structurally current so the
    # Command is declared with the right targets/sources.  This launches node
    # only to (re)describe the graph, and only when a manifest / cdata.js /
    # platformio.ini changed -- never to build the UI.  The build itself is the
    # deferred Command below.
    if not _depfile_structure_current(depfile, manifests):
        _emit_depfile(depfile, manifests)

    targets, sources = _parse_depfile(depfile)

    # The single node invocation that builds the whole web UI (main + usermods),
    # run as a normal build node when SCons finds a target stale.
    node_cmd = " ".join(
        ['node', '"%s"' % CDATA_JS, '--depfile', '"%s"' % depfile]
        + ['--manifest "%s"' % m for m in manifests]
    )

    def _build_ui(target, source, env, _cmd=node_cmd):
        _ensure_node_modules(env)
        rc = env.Execute(_cmd)
        if rc:
            print('\x1b[0;31;43m' + 'Web UI build failed check https://kno.wled.ge/advanced/compiling-wled/' + '\x1b[0m')
        return rc

    ui_cmd = env.Command(
        target=[env.File(t) for t in targets],
        source=[env.File(s) for s in sources],
        action=env.VerboseAction(_build_ui, "Building web UI"),
    )

    # By default SCons removes a builder's targets before running its action.
    # Because every header is a target of this one Command, editing any single
    # UI source would delete ALL of them, cdata.js would then see them missing
    # and rebuild every one (each with a fresh WEB_BUILD_TIME), and everything
    # that #includes a generated header would needlessly recompile.  Mark the
    # targets Precious so SCons leaves them in place and cdata.js's own
    # per-job staleness check does the incremental rebuild.  (Precious only
    # affects pre-build removal, not `pio run -t clean`.)
    env.Precious(ui_cmd)

    # Order the build ahead of compilation.  PlatformIO compiles the main WLED
    # sources with result.env (the project-as-library builder's env; see
    # ProcessProjectDeps -> plb.env.BuildSources), and each usermod's sources
    # with that module's own env -- so we wire both.
    _wire_object_methods(result.env, ui_cmd)
    # Match on realpath both sides: manifests come from load_usermods.py already
    # resolved, and a symlink:// usermod's src_dir may reach through a symlink,
    # so a plain normpath comparison could miss and leave its objects unwired.
    manifest_dirs = {os.path.realpath(os.path.dirname(m)) for m in manifests}
    for dep in result.depbuilders:
        if os.path.realpath(str(dep.src_dir)) in manifest_dirs:
            _wire_object_methods(dep.env, ui_cmd)

    # Belt-and-suspenders: also an explicit prerequisite of the final firmware.
    xenv.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", ui_cmd)


# Hook ConfigureProjectLibBuilder *after* load_usermods.py's wrapper, so the
# usermod manifest list is already populated and the returned builder (whose env
# compiles the main sources) is available.  This script is listed after
# load_usermods.py in platformio.ini, so our wrapper is outermost and runs last.
# We only register nodes here; the build runs later, at build time.
_old_ConfigureProjectLibBuilder = env.ConfigureProjectLibBuilder

def _wrapped_ConfigureProjectLibBuilder(xenv):
    result = _old_ConfigureProjectLibBuilder.clone(xenv)()
    _register_ui_build(xenv, result)
    return result

env.AddMethod(_wrapped_ConfigureProjectLibBuilder, "ConfigureProjectLibBuilder")
