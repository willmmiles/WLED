Import('env')
import subprocess
import json
import re
import os
from SCons.Script import Action

def get_github_repo():
    """Extract GitHub repository name from git remote URL.
    
    Uses the remote that the current branch tracks, falling back to 'origin'.
    This handles cases where repositories have multiple remotes or where the
    main remote is not named 'origin'.
    
    Returns:
        str: Repository name in 'owner/repo' format for GitHub repos,
             'unknown' for non-GitHub repos, missing git CLI, or any errors.
    """
    try:
        remote_name = 'origin'  # Default fallback
        
        # Try to get the remote for the current branch
        try:
            # Get current branch name
            branch_result = subprocess.run(['git', 'rev-parse', '--abbrev-ref', 'HEAD'], 
                                         capture_output=True, text=True, check=True)
            current_branch = branch_result.stdout.strip()
            
            # Get the remote for the current branch
            remote_result = subprocess.run(['git', 'config', f'branch.{current_branch}.remote'], 
                                         capture_output=True, text=True, check=True)
            tracked_remote = remote_result.stdout.strip()
            
            # Use the tracked remote if we found one
            if tracked_remote:
                remote_name = tracked_remote
        except subprocess.CalledProcessError:
            # If branch config lookup fails, continue with 'origin' as fallback
            pass
        
        # Get the remote URL for the determined remote
        result = subprocess.run(['git', 'remote', 'get-url', remote_name], 
                              capture_output=True, text=True, check=True)
        remote_url = result.stdout.strip()
        
        # Check if it's a GitHub URL
        if 'github.com' not in remote_url.lower():
            return None
        
        # Parse GitHub URL patterns:
        # https://github.com/owner/repo.git
        # git@github.com:owner/repo.git
        # https://github.com/owner/repo
        
        # Remove .git suffix if present
        if remote_url.endswith('.git'):
            remote_url = remote_url[:-4]
        
        # Handle HTTPS URLs
        https_match = re.search(r'github\.com/([^/]+/[^/]+)', remote_url, re.IGNORECASE)
        if https_match:
            return https_match.group(1)
        
        # Handle SSH URLs
        ssh_match = re.search(r'github\.com:([^/]+/[^/]+)', remote_url, re.IGNORECASE)
        if ssh_match:
            return ssh_match.group(1)
        
        return None
        
    except FileNotFoundError:
        # Git CLI is not installed or not in PATH
        return None
    except subprocess.CalledProcessError:
        # Git command failed (e.g., not a git repo, no remote, etc.)
        return None
    except Exception:
        # Any other unexpected error
        return None


def get_wled_version():
    """ Return the WLED version number
        WLED version is managed by package.json; this is picked up in several places
        - It's integrated in to the UI code
        - Here, for wled_metadata.cpp
        - The output_bins script
        We always take it from package.json to ensure consistency across users        
    """
    with open("package.json", "r") as package:
        return json.load(package)["version"]

def has_def(cppdefs, name):
    """ Returns true if a given name is set in a CPPDEFINES collection """
    for f in cppdefs:
        if isinstance(f, tuple):
            f = f[0]
        if f == name:
            return True
    return False

def generate_version_header(target, source, env):
    """Generate version.h header file if version has changed"""
   
    # Target is a list, get the first (and only) element
    header_file = str(target[0])

    # Load the version number
    wled_version = get_wled_version()

    # Generate the header content
    header_content = f"""#pragma once
#define WLED_VERSION "{wled_version}"
"""

    # Add WLED_REPO if it's not externally declared
    if not has_def(env["CPPDEFINES"], "WLED_REPO"):
        header_content += f"""#define WLED_REPO "{get_github_repo()}"
"""

  # Open in append mode (creates file if it doesn't exist)
    with open(header_file, "a+") as version_header:
        version_header.seek(0)
        existing_content = version_header.read()
        if existing_content != header_content:
            # Content changed or file was empty, rewind and write
            version_header.seek(0)
            version_header.write(header_content)
            version_header.truncate()

# Define the output header file path in the build directory
build_dir = env.subst("$BUILD_DIR")
header_file = os.path.join(build_dir, "wled_version.h")

# Add the build directory to the include path so the header can be found
env.Append(CPPPATH=[build_dir])

# Create an SCons target for the version header
version_target = env.Command(
    target=header_file,
    source=None,
    action=Action(generate_version_header, None)
)

# Make the version header always be considered out of date so our
# custom logic can decide whether to regenerate it
env.AlwaysBuild(version_target)
env.NoCache(version_target)

# Add the version header as a dependency of the specific cpp file that uses it
src_file = os.path.join(env.get("PROJECT_DIR"), "wled00", "wled_metadata.cpp")
env.Depends(src_file, version_target)
