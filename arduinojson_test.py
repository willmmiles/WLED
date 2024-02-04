"""
Repeated-build test for ArduinoJson v7 in WLED

Requires -Wframe-larger-than=128 in the build_flags to generate stack usage reports
"""

import re
import subprocess

FUNCTION_RE = re.compile("(.*): In(?: member)? function '.+? (.*)':")
STACK_RE = re.compile(".*: warning: the frame size of (\d+) bytes is larger than \d+ bytes \[-Wframe-larger-than=\]")
RAM_RE = re.compile("^RAM:.*used (\d+) bytes")
FLASH_RE = re.compile("^Flash:.*used (\d+) bytes")

def configure_ardinojson():
    """ Edit ArduinJson files to desired test configuration """

def parse_function_results(err_text: list[str]):
    """ Extract function stack size values from process outputs """
    # Look for 'in (member)? function' lines    
    function = None
    result = {}    
    for line in err_text:
        print(line)
        f_match = FUNCTION_RE.fullmatch(line)
        if f_match:
            print(f"Found function: {function}")
            function = ":".join(f_match.groups())
            print(f"Found function: {function}\n")
        else:
            s_match = STACK_RE.fullmatch(line)            
            if s_match is not None and function is not None:
                stack = int(s_match.group(1))
                print(f"Found stack: {stack}\n")
                result[function] = stack
    return result


def parse_prog_results(out_text: list[str]):
    """ Extract memory and program size values """
    ram = None
    flash = None    
    for line in out_text:
        r_match = RAM_RE.match(line)
        if r_match:
            ram = int(r_match.group(1))
        else:
            f_match = FLASH_RE.match(line)
            if f_match:
                flash = int(f_match.group(1))
        if flash and ram:
            break
    return (flash, ram)


def run_build():
    run_result = subprocess.run(["/home/vscode/.platformio/penv/bin/pio", "run", "--environment", "nodemcuv2_debug"], capture_output=True, encoding='utf-8')
    return (parse_function_results(run_result.stderr.splitlines()), parse_prog_results(run_result.stdout.splitlines()))


def main():
    """ Main program """
    (f, p) = run_build()
    print(f)
    print(p)



if (__name__) ==  "__main__":
    main()