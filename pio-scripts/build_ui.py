Import("env")
from pathlib import Path   # For OS-agnostic path manipulation
import shutil

node_ex = shutil.which("node")
# Check if Node.js is installed and present in PATH if it failed, abort the build
if node_ex is None:
    print('\x1b[0;31;43m' + 'Node.js is not installed or missing from PATH html css js will not be processed check https://kno.wled.ge/advanced/compiling-wled/' + '\x1b[0m')
    exitCode = env.Execute("null")
    exit(exitCode)


def build_ui(target, source, env):
    # Install the necessary node packages for the pre-build asset bundling script
    print('\x1b[6;33;42m' + 'Installing node packages' + '\x1b[0m')
    env.Execute("npm ci")

    # Call the bundling script
    exitCode = env.Execute("npm run build")

    # If it failed, abort the build
    if (exitCode):
      print('\x1b[0;31;43m' + 'npm run build fails check https://kno.wled.ge/advanced/compiling-wled/' + '\x1b[0m')
      exit(exitCode)

ui_srcdir = Path(env["PROJECT_DIR"]).resolve() / "wled00" / "data"

build_ui_cmd = env.Command(
   target=env.File('$PROJECT_DIR/wled00/html_ui.h'),   # A representative file
   source=[
      env.File('$PROJECT_DIR/package-lock.json'),
      env.File('$PROJECT_DIR/package.json'),
      env.File('$PROJECT_DIR/tools/cdata.js'),
      *[env.File(str(p)) for p in ui_srcdir.rglob("*") if p.is_file()]
   ],
   action=build_ui)

# Ensure UI gets built before any cpp files by hooking the Object constructor
def _wrap_object_method(name):
    if not hasattr(env, name): return
    orig = getattr(env, name)
    def wrapped(*args, **kwargs):
        nodes = orig(*args, **kwargs)
        env.Requires(nodes, build_ui_cmd)
        return nodes
    setattr(env, name, wrapped)

for m in ("Object", "StaticObject", "SharedObject"):
    _wrap_object_method(m)

# Also make an an explicit dependency of the final build target, so it will always be built the first time
env.Depends("$BUILD_DIR/$PROGNAME$PROGSUFFIX", build_ui_cmd)
