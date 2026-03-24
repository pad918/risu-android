### RUN RISU TESTS ON PLATFORMS THAT DON'T SUPPORT SIGNALS

import subprocess
import shlex
import time
import os
from tqdm import tqdm
from enum import Enum

# Paths
MASTER_RISU_FILE = "aarch64_hang.risu"
#MASTER_RISU_FILE = "../aarch64_minimal.risu"
LOCAL_TMP_RISU   = "single_test.risu"
LOCAL_TMP_BIN    = "single_test.bin"
REMOTE_TMP_BIN   = "/data/local/tmp/risu/single_test.bin"

FLUSH_INTERVAL = 30
PRINT_ERROR_LOGS = False
# For testing frida
ARE_SIGILL_SIGTRAP_SAME = True

# Define paths and commands here

CMD_RISUGEN = "./risugen --numinsns 10 {local_risu} {local_bin}"
CMD_PUSH = "adb push {local_bin} {remote_bin}"
CMD_MASTER = "adb shell /data/local/tmp/risu/risu --master {remote_bin}"
CMD_APPRENTICE = "../.venv/bin/frida -U -l ../frida_instrument_risu.js -f /data/local/tmp/risu/risu -- --host localhost {remote_bin}"
CMD_CLEANUP = "adb shell pkill -9 risu"

#frida -U \
#      -l frida_instrument_risu.js \
#      -f /data/local/tmp/risu/risu \
#      -- --host localhost /data/local/tmp/risu/aarch_android.out

class TestResult(Enum):
    PASS_SAME_ERROR = 1
    PASS_NO_ERROR = 2
    FAIL_HANGED = 4
    FAIL_DIFF_SIGNAL = 8
    FAIL_SCILENT = 16
    FAIL_STATE_DIFF = 16


# PARSER LOGIC
def parse_risu_file(filepath):
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    header = []
    instructions = {}
    
    current_logical_line = ""
    for line in lines:
        stripped = line.strip()
        
        # Ignore comments and empty line
        if not stripped or stripped.startswith('#'):
            continue
            
        # Handle line continuations (backslash at the end)
        if stripped.endswith('\\'):
            current_logical_line += line.rstrip()[:-1] + " "
        else:
            current_logical_line += line
            
            if current_logical_line.startswith('.'):
                header.append(current_logical_line)
            else:
                parts = current_logical_line.split()
                if parts:
                    insn_name = parts[0]
                    instructions[insn_name] = current_logical_line
            
            current_logical_line = ""
            
    return header, instructions


def run_command(cmd_template, blocking=True, timeout=None):
    cmd_str = cmd_template.format(
        local_risu=LOCAL_TMP_RISU,
        local_bin=LOCAL_TMP_BIN,
        remote_bin=REMOTE_TMP_BIN
    )
    args = shlex.split(cmd_str)
    
    if blocking:
        return subprocess.run(args, 
                              stdout=subprocess.PIPE, 
                              stderr=subprocess.PIPE, 
                              text=True, 
                              timeout=timeout)
    else:
        return subprocess.Popen(args, 
                                stdout=subprocess.PIPE, 
                                stderr=subprocess.PIPE, 
                                text=True)

"""
    Executes a test with the data that is currently
    on the device and returns Success / Failure.
"""
def execute_single_test(insn_name):
    # Start Master non-blocking
    master_proc = run_command(CMD_MASTER, blocking=False)
    time.sleep(0.5)
    
    # Start Apprentice
    status = TestResult.PASS_NO_ERROR
    app_out = ""
    app_err = ""
    is_fail = False

    try:
        res_app = run_command(CMD_APPRENTICE, blocking=True, timeout=10)
        app_out = res_app.stdout
        app_err = res_app.stderr
        
        # Avoid race conditions
        try:
            master_proc.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            pass

        # Get apprentice crash
        full_output = (res_app.stdout + res_app.stderr).lower()
        app_simulated_exit = 0
        
        # Allow a virtual exit code to be used in the form
        # CRASH_SIGNAL: X
        for line in [l.strip() for l in full_output.split("\n")]:
            if(line.startswith("signal")):
                app_crash_signal = int(line.split(" ")[1])
                app_simulated_exit = 128 + app_crash_signal
                if(app_simulated_exit == 133 and ARE_SIGILL_SIGTRAP_SAME):
                    app_simulated_exit = 132

        # Check Master's final state after Apprentice finishes/crashes
        master_proc.poll()
        master_crashed = master_proc.returncode is not None and master_proc.returncode != 0
        
        #if app_simulated_exit == 0 and res_app.returncode != 0:
        #    app_simulated_exit = res_app.returncode

        if master_crashed and app_simulated_exit!=0:
            if master_proc.returncode != app_simulated_exit:
                status = TestResult.FAIL_DIFF_SIGNAL
            else:
                status = TestResult.PASS_SAME_ERROR
        elif master_crashed:
            status = TestResult.FAIL_DIFF_SIGNAL
        elif app_simulated_exit!=0:
            status = TestResult.FAIL_DIFF_SIGNAL
        else:
            if "ending tests normally" in full_output or "match in apprentice" in full_output:
                status = TestResult.PASS_NO_ERROR
            elif "mismatch" in full_output:
                status = TestResult.FAIL_STATE_DIFF
            else:
                status = TestResult.FAIL_SCILENT
                
    except subprocess.TimeoutExpired:
        status = TestResult.FAIL_HANGED

    
    # Check if this iteration was a failure
    is_fail = (status.value & 0b11) == 0
    master_out, master_err = master_proc.communicate()
    fail_block = (
        # String concatenation works the same in 
        # python as in C!
        f"\n{'='*70}\n"
        f" DETAILED FAILURE LOG: {insn_name}\n"
        f" STATUS: {status}\n"
        f"{'-'*70}\n"
        f" [MASTER STDOUT]\n{master_out.strip()}\n\n"
        f" [MASTER STDERR]\n{master_err.strip()}\n"
        f"{'-'*70}\n"
        f" [APPRENTICE STDOUT]\n{app_out.strip()}\n\n"
        f" [APPRENTICE STDERR]\n{app_err.strip()}\n"
        f"{'='*70}\n"
    )

    return is_fail, status, fail_block


def main():
    print("=== Parsing master risu file ===")
    header, instructions = parse_risu_file(MASTER_RISU_FILE)
    print(f"\tFound {len(instructions)} individual instructions to test.\n")

    # Open a log file to store the results permanently
    with open("test_results.log", "w") as log:
        all_inst = instructions.items()
        for test_num, (insn_name, insn_logic) in tqdm(
                                                    enumerate(all_inst),
                                                    desc="TESTING INSTRUCTIONS",  
                                                    total=len(all_inst)):
            
            # Creat .risu for single instruciton type
            with open(LOCAL_TMP_RISU, "w") as f:
                f.writelines(header)
                f.write(insn_logic)
                
            # Run risugen
            res_gen = run_command(CMD_RISUGEN)
            if res_gen.returncode != 0:
                print("FAIL (Risugen Error)")
                log.write(f"[{insn_name}] FAIL: Risugen Error\n")
                continue

            # Push to device over ADB
            res_push = run_command(CMD_PUSH)
            if res_push.returncode != 0:
                print("FAIL (ADB Push Error)")
                log.write(f"[{insn_name}] FAIL: ADB Push Error\n")
                continue

            # Give it multiple tries since it sometimes hangs
            for i in range(2):
                is_fail, status, fail_block = execute_single_test(insn_name)

                # Cleanup Environment (kill stuck process e.t.c.)
                run_command(CMD_CLEANUP)
                
                # Avoid race conditions
                time.sleep(0.2)
                if(status != TestResult.FAIL_HANGED):
                    break
                else:
                    print("HANGED!, retrying instruction: ", insn_name)

            if is_fail and PRINT_ERROR_LOGS:
                log.write(fail_block)
            log.write(f"[{status.name}] <{insn_name}>\n")

            # Flush often so we keep results even it
            # It dies prematurly
            if(test_num%FLUSH_INTERVAL==0):
                log.flush()

    print("\==== TESTS DONE ====")

if __name__ == "__main__":
    main()