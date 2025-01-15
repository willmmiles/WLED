Import('env')
from pathlib import Path   # For OS-agnostic path manipulation

usermod_dir = Path(env["PROJECT_DIR"]) / "usermods"
all_usermods = [f for f in usermod_dir.iterdir() if f.is_dir() and f.joinpath('library.json').exists()]

if env['PIOENV'] == "usermods":
   # Add all usermods
   env.GetProjectConfig().set(f"env:usermods", 'custom_usermods', " ".join([f.name for f in all_usermods]))

def find_usermod(mod: str):
  """Locate this library in the usermods folder.
     We do this to avoid needing to rename a bunch of folders;
     this could be removed later
  """
  # Check name match
  mp = usermod_dir / mod
  if mp.exists():
    return mp
  mp = usermod_dir / f"usermod_v2_{mod}"
  if mp.exists():
    return mp
  raise RuntimeError(f"Couldn't locate module {mod} in usermods directory!")

usermods = env.GetProjectOption("custom_usermods","")
if usermods:
  proj = env.GetProjectConfig()
  deps = env.GetProjectOption('lib_deps')  
  src_dir = proj.get("platformio", "src_dir")
  src_dir = src_dir.replace('\\','/')
  mod_paths = {mod: find_usermod(mod) for mod in usermods.split(" ")}
  usermods = [f"{mod} = symlink://{path}" for mod, path in mod_paths.items()]
  proj.set("env:" + env['PIOENV'], 'lib_deps', deps + usermods)  


# Monkey-patch ConfigureProjectLibBuilder to mark up the dependencies
# Save the old value
cpl = env.ConfigureProjectLibBuilder
# Our new wrapper
def cpl_wrapper(xenv):
  # Update usermod properties  
  lib_builders = xenv.GetLibBuilders()  
  wled_dir = xenv["PROJECT_SRC_DIR"]
  um_deps = [dep for dep in lib_builders if usermod_dir in Path(dep.src_dir).parents]
  other_deps = [dep for dep in lib_builders if usermod_dir not in Path(dep.src_dir).parents]
  print(xenv.Dump())
  for dep in lib_builders:
    xr = usermod_dir in Path(dep.src_dir).parents
    print(f"{dep.src_dir}: {xr}")

  for um in um_deps:
    # Add include paths for all non-usermod dependencies
    for dep in other_deps:
        for dir in dep.get_include_dirs():
            um.env.PrependUnique(CPPPATH=dir)
    # Add the wled folder to the include path
    print(f"{str(um)}: adding {wled_dir}")
    um.env.PrependUnique(CPPPATH=wled_dir)
    # Make sure we link directly, not through an archive
    # Archives drop the .dtor table section we need
    build = um._manifest.get("build", {})
    build["libArchive"] = False
    um._manifest["build"] = build
  return cpl.clone(xenv)()


# Replace the old one with ours
env.AddMethod(cpl_wrapper, "ConfigureProjectLibBuilder")
