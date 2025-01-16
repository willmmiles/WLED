Import('env')
import os.path
from pathlib import Path   # For OS-agnostic path manipulation
from platformio.package.manager.library import LibraryPackageManager

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
  # Inject usermods in to project lib_deps
  proj = env.GetProjectConfig()
  deps = env.GetProjectOption('lib_deps')
  src_dir = proj.get("platformio", "src_dir")
  src_dir = src_dir.replace('\\','/')
  mod_paths = {mod: find_usermod(mod) for mod in usermods.split(" ")}
  usermods = [f"{mod} = symlink://{path}" for mod, path in mod_paths.items()]
  proj.set("env:" + env['PIOENV'], 'lib_deps', deps + usermods)
  # Force usermods to be installed in to the environment build state before the LDF runs
  # Otherwise we won't be able to see them until it's too late to change their paths for LDF
  # Logic is largely borrowed from PlaformIO internals
  not_found_specs = []
  for spec in usermods:
    found = False
    for storage_dir in env.GetLibSourceDirs():
      #print(f"Checking {storage_dir} for {spec}")
      lm = LibraryPackageManager(storage_dir)
      if lm.get_package(spec):
          #print("Found!")
          found = True
          break
    if not found:
        #print("Missing!")
        not_found_specs.append(spec)
  if not_found_specs:
      lm = LibraryPackageManager(
          env.subst(os.path.join("$PROJECT_LIBDEPS_DIR", "$PIOENV"))
      )
      for spec in not_found_specs:
        #print(f"LU: forcing install of {spec}")
        lm.install(spec)
  # Clear GetLibBuilders cache
  DefaultEnvironment().Replace(__PIO_LIB_BUILDERS=None)


# Monkey-patch ConfigureProjectLibBuilder to mark up the dependencies
# Save the old value
cplb = env.ConfigureProjectLibBuilder
# Our new wrapper
def cplb_wrapper(xenv):
  # Update usermod properties
  print(xenv.GetProjectOption('lib_deps'))
  lib_builders = xenv.GetLibBuilders()
  wled_dir = xenv["PROJECT_SRC_DIR"]
  um_deps = [dep for dep in lib_builders if usermod_dir in Path(dep.src_dir).parents]
  other_deps = [dep for dep in lib_builders if usermod_dir not in Path(dep.src_dir).parents]
  if usermods and not um_deps:
    # Debug: try resetting the lib builders??
    print("REPLACING??")
    xenv.Replace(__PIO_LIB_BUILDERS=None)
    lib_builders = xenv.GetLibBuilders()
    um_deps = [dep for dep in lib_builders if usermod_dir in Path(dep.src_dir).parents]
    other_deps = [dep for dep in lib_builders if usermod_dir not in Path(dep.src_dir).parents]
  for dep in lib_builders:
     print(f"Found dep: {str(dep)}")
  for um in um_deps:
    print(f"Adding properties to {str(um)} - {wled_dir}")
    # Add the wled folder to the include path
    um.env.PrependUnique(CPPPATH=wled_dir)
    # Add WLED's own dependencies
    for dep in other_deps:
        for dir in dep.get_include_dirs():
            um.env.PrependUnique(CPPPATH=dir)      
    # Make sure we link directly, not through an archive
    # Archives drop the .dtor table section we need
    build = um._manifest.get("build", {})
    build["libArchive"] = False
    um._manifest["build"] = build
  return cplb.clone(xenv)()


# Replace the old one with ours
env.AddMethod(cplb_wrapper, "ConfigureProjectLibBuilder")
