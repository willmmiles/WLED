"""
Repeated-build test for ArduinoJson v7 in WLED

Requires -Wframe-larger-than=128 in the build_flags to generate stack usage reports
"""

from collections import defaultdict
import fileinput
import os
import pickle
import re
import shutil
import subprocess
import sys

ARDUINOJSON_PATH = "lib/ArduinoJson"
BASE_PATH = ARDUINOJSON_PATH + "/src/ArduinoJson/"

FUNCTION_RE = re.compile("(.*): In(?: member)? function '.+? (.*)':")
STACK_RE = re.compile(".*: warning: the frame size of (\d+) bytes is larger than \d+ bytes \[-Wframe-larger-than=\]")
RAM_RE = re.compile("^RAM:.*used (\d+) bytes")
FLASH_RE = re.compile("^Flash:.*used (\d+) bytes")

def flush_platformio_cache():
    """ Delete platformio build cache, so we can start clean """
    shutil.rmtree(os.path.expanduser("~/.buildcache"))

def reset_arduinojson():
    """ Reset arduinojson to upstream state """
    subprocess.run(["git", "-C", ARDUINOJSON_PATH, "checkout", "."])

def file_search_and_replace(filename: str, line: int, target: str, replacement: str):
    """ Given a file, replace target with replacement on line line """
    with fileinput.input(filename, inplace=True) as input:
        for line_text in input:
            if input.filelineno() == line: 
                line_text = re.sub(target, replacement, line_text)
            sys.stdout.write(line_text)    # fileinput makes stdout the file

def knock_out_forceinline_global():
    """ Turn off forceinline globally """
    # TODO: avoid hardcoded constants
    file_search_and_replace(BASE_PATH + "Polyfills/attributes.hpp", 18, "__attribute__\(\(always_inline\)\)", "inline")

def knock_out_forceinline(file: str, line: int):
    """ Remove a forceinline entry """
    file_search_and_replace(BASE_PATH + file, line, "FORCE_INLINE", "inline")

def parse_file_line(text: str):
    """ Quickly parse a grep result"""
    (file, line) = text.split(':')[0:2]
    return(file[len(BASE_PATH):], int(line))

def find_forceinlines():
    """ Find all FORCE_INLINE references in ArduinoJson """
    # Lame approach, call grep
    run_result = subprocess.run(["grep", "-rn", "FORCE_INLINE", BASE_PATH], capture_output=True, encoding='utf-8')
    result = [parse_file_line(text) for text in run_result.stdout.splitlines()]
    # Filter out the global one
    return [entry for entry in result if not entry[0].startswith("Polyfills")]

def parse_function_results(err_text: list[str]):
    """ Extract function stack size values from process outputs """
    # Look for 'in (member)? function' lines    
    function = None
    result = {}    
    for line in err_text:
        #print(line)
        f_match = FUNCTION_RE.fullmatch(line)
        if f_match:
            function = ":".join(f_match.groups())
            #print(f"Found function: {function}\n")
        else:
            s_match = STACK_RE.fullmatch(line)            
            if s_match is not None and function is not None:
                stack = int(s_match.group(1))
                #print(f"Found stack: {stack}\n")
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
    return {"flash": flash, "ram": ram}


def run_build():
    run_result = subprocess.run([os.path.expanduser("~/.platformio/penv/bin/pio"), "run", "--environment", "nodemcuv2_debug"], capture_output=True, encoding='utf-8')
    return (parse_function_results(run_result.stderr.splitlines()), parse_prog_results(run_result.stdout.splitlines()))


def collect_metrics():
    """ Main program """
    flush_platformio_cache()
    
    # Collect baseline data
    reset_arduinojson()
    baseline = run_build()
    print(f"baseline: {baseline[1]['flash']}, {len(baseline[0])} functions tracked")
    
    # Collect no-force data
    knock_out_forceinline_global()
    no_inline = run_build()
    print(f"no_inline: {no_inline[1]['flash']}, {len(no_inline[0])} functions tracked")

    # Collect metrics disabling each forceinline     
    inline_results = {}
    for inline_entry in find_forceinlines():
        reset_arduinojson()
        knock_out_forceinline(*inline_entry)
        run_results = run_build()
        inline_results[inline_entry] = run_results
        print(f"{inline_entry[0]}:{inline_entry[1]}: {run_results[1]['flash']}, {len(run_results[0])} functions tracked")    

    return (baseline, no_inline, inline_results)


def produce_metrics():
    metrics = collect_metrics()
    with open('metrics.pickle', 'wb') as f:
        pickle.dump(metrics, f)
    return metrics

def load_metrics():
    with open('metrics.pickle', 'rb') as f:
        return pickle.load(f)


def invert_dict(d: dict, key, values):
    for k, v in values.items():
        d[k][key] = v

def flatten_results(r):
    #print(r)
    return {"flash": r[1]['flash'], **r[0]}


def filter_metrics(baseline, no_inline, per_line_metrics):
    """ Filter the results """
    # Invert the map to be by function instead of by test
    function_results = defaultdict(dict)
    invert_dict(function_results, "no_inline", no_inline)
    for key, value in per_line_metrics.items():
        invert_dict(function_results, key, value)

    # Map the results with respect to the baseline
    for key, values in function_results.items():
        baseline_value = baseline.get(key,128)
        function_results[key] = {k: v-baseline_value for k, v in values.items()}        

    # Discard any functions that show a change of zero, or are all the same (baseline issue)
    function_results = {k: values for k, values in function_results.items() if any(values.values()) and len(set(values.values())) > 1}

    return function_results

def print_function_results(results, keys):
    # print header
    print("| function | ",end="")
    print(" | ".join(results.keys()), end=" |\n")
    print("| ----- ",end="")
    print(" | ----------" * len(results), end=" |\n")
    for key in sorted(keys):
        print(f"| {key} | ", end="")
        k_results = [str(values.get(key, "N/A")) for values in results.values()]
        print(" | ".join(k_results), end=" |\n")


def main():
    """ Main program """
    #metrics = produce_metrics()
    metrics = load_metrics()
    #print(metrics)
    final_results = filter_metrics(flatten_results(metrics[0]), flatten_results(metrics[1]), {f"{k[0]}:{k[1]}": flatten_results(v) for k, v in metrics[2].items()})
    print_function_results(final_results, ["no_inline", *[f"{k[0]}:{k[1]}" for k in metrics[2].keys()]])




if (__name__) ==  "__main__":
    main()