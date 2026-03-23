### RUN RISU TESTS ON PLATFORMS THAT DON'T SUPPORT SIGNALS

import subprocess
import shlex
import time
import os
from tqdm import tqdm

# Paths
MASTER_RISU_FILE = "aarch64.risu"
#MASTER_RISU_FILE = "../aarch64_minimal.risu"
LOCAL_TMP_RISU   = "single_test.risu"
LOCAL_TMP_BIN    = "single_test.bin"
REMOTE_TMP_BIN   = "/data/local/tmp/risu/single_test.bin"

FLUSH_INTERVAL = 30

# Define paths and commands here

CMD_RISUGEN = "./risugen --numinsns 1 {local_risu} {local_bin}"
CMD_PUSH = "adb push {local_bin} {remote_bin}"
CMD_MASTER = "adb shell /data/local/tmp/risu/risu --master {remote_bin}"
CMD_APPRENTICE = "adb shell /data/local/tmp/risu/risu --host localhost {remote_bin}"
CMD_CLEANUP = "adb shell pkill -9 risu"

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

            # Start Master non-blocking
            master_proc = run_command(CMD_MASTER, blocking=False)
            time.sleep(0.5)
            
            # Start Apprentice
            status = ""
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

                # Check Master's final state after Apprentice finishes/crashes
                master_proc.poll()
                master_crashed = master_proc.returncode is not None and master_proc.returncode != 0
                app_crashed = res_app.returncode != 0

                if master_crashed and app_crashed:
                    if master_proc.returncode != res_app.returncode:
                        status = f"FAIL (Signal Mismatch! Master Exit: {master_proc.returncode}, App Exit: {res_app.returncode})"
                    else:
                        status = f"PASS (Both crashed with Exit {res_app.returncode})"
                elif master_crashed:
                    status = f"FAIL (Only Master Crashed, Master Exit: {master_proc.returncode}, App Exit: {res_app.returncode})"
                elif app_crashed:
                    status = f"FAIL (Only Emulator Crashed, App Exit: {res_app.returncode}, Master Exit: {master_proc.returncode})"
                else:
                    full_output = (res_app.stdout + res_app.stderr).lower()
                    if "ending tests normally" in full_output or "match in apprentice" in full_output:
                        status = "PASS (Match)"
                    elif "mismatch" in full_output:
                        status = "FAIL (Register/Mem Mismatch)"
                    else:
                        status = f"FAIL (Silent). Apprentice Output: {full_output.strip()}"
                        
            except subprocess.TimeoutExpired:
                status = "FAIL (Timeout - Hang detected)"
            
            # Check if this iteration was a failure
            is_fail = "FAIL" in status

            log.write(f"[{insn_name}] {status}\n")

            # Cleanup Environment (kill stuck process e.t.c.)
            run_command(CMD_CLEANUP)
            
            master_out, master_err = master_proc.communicate()
            
            if is_fail:
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
                log.write(fail_block)
            
            # Avoid race conditions
            time.sleep(0.2)

            # Flush often so we keep results even it
            # It dies prematurly
            if(test_num%FLUSH_INTERVAL==0):
                log.flush()

    print("\==== TESTS DONE ====")

if __name__ == "__main__":
    main()