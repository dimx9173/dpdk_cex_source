import os
import subprocess
import sys
import threading
import time

HOST = "192.168.193.128"
USER = "brian"
PASS = "choas=88415"
REMOTE_DIR = "~/dpdk_cex/"

def run_expect(cmd_str, password, timeout=300, script_name="temp.exp"):
    exp_script = f"""
set timeout {timeout}
log_user 1
spawn bash -c {{{cmd_str}}}
expect {{
    "assword:" {{
        send "{password}\\r"
        exp_continue
    }}
    "yes/no" {{
        send "yes\\r"
        exp_continue
    }}
    eof
}}
catch wait result
exit [lindex $result 3]
"""
    # Write temp expect script
    import random
    if script_name == "temp.exp":
        script_name = f"temp_{random.randint(1000,9999)}.exp"

    with open(script_name, "w") as f:
        f.write(exp_script)
    
    # Run expect
    # Run expect
    try:
        result = subprocess.run(["expect", script_name], capture_output=True, text=True)
        if result.stderr:
            print("STDERR:", result.stderr)
            
        print("\n=== FILTERED LOGS (Classifier/Strategy/Forwarding) ===")
        for line in result.stdout.splitlines():
            if "[Classifier]" in line or "[Strategy]" in line or "Fast Path" in line or "[Forwarding]" in line or "Launched" in line:
                print(line)
        print("===========================================\n")

        # Check for Fast Path confirmation in this command's output
        if "[Strategy] Fast Path RX" in result.stdout:
            print("\n*** VERIFICATION SUCCESS: Fast Path HFT Packets Detected! ***\n")
        else:
            print("\n*** VERIFICATION WARNING: 'Fast Path RX' not found. ***\n")
            print("=== FULL OUTPUT (First 500 lines) ===")
            print("\n".join(result.stdout.splitlines()[:500]))
            print("=====================================")

        # Check for Latency Stats
        if "Latency Stats" in result.stdout:
             print("\n*** VERIFICATION SUCCESS: Latency Stats Detected! ***\n")
             for line in result.stdout.splitlines():
                 if "Latency Stats" in line or "P50" in line or "P99" in line:
                     print(f"  {line}")
        else:
            print("\n*** VERIFICATION WARNING: 'Latency Stats' not found. ***\n")

        return result.returncode
    finally:
        if os.path.exists(script_name):
            os.remove(script_name)

def main():
    print("=== Step 1: Syncing Code ===")
    # Exclude build artifacts and git
    rsync_cmd = f"rsync -avz --exclude 'build' --exclude '.git' ./ {USER}@{HOST}:{REMOTE_DIR}"
    if run_expect(rsync_cmd, PASS) != 0:
        print("Sync failed")
        return

    print("\\n=== Step 2: Building on VM ===")
    build_cmd = f"ssh {USER}@{HOST} 'cd {REMOTE_DIR} && meson setup build -Denable_tests=true --reconfigure && ninja -C build'"
    if run_expect(build_cmd, PASS) != 0:
        print("Build failed")
        return

    print("\\n=== Step 3: Running Application on VM ===")
    # Run hft-app to verify Fast Path
    # using 'timeout 15s' to let it run for a while and gather packets, then kill it
    # We pipe to cat to avoid tty issues with sudo
    # Note: On VM, hugepages should be mounted.
    # The command: timeout 15s sudo -E ./build/src/hft-app -l 0,1 -n 4 --proc-type=auto --file-prefix=hft --vdev=net_virtio_user0,mac=00:00:00:00:00:01,path=/dev/vhost-net,queue_size=1024
    
    # We need to ensure we run with sudo.
    # We also check for the specific log message: "[Strategy] Fast Path RX"
    
    print("Launching hft-app...")
    # Update REMOTE_DIR to absolute to avoid ~ issues with sudo
    REMOTE_PATH = f"/home/{USER}/dpdk_cex"
    
    # We must wrap in ssh. And we need to pipe password to sudo inside the ssh session.
    # The run_expect function handles the "Password:" prompt for SSH.
    # But for SUDO inside SSH, we need to handle that too?
    # run_expect handles ONE password prompt.
    # If we use "echo PASS | sudo -S", we don't need expect for sudo.
    # But we DO need expect for SSH login (if keys not set up).
    # The previous code used run_expect which looks for "password:".
    # Assuming SSH needs password. 
    # The command inside SSH: "echo 'choas=88415' | sudo -S -E ..."
    
    # We need TWO vdevs:
    # 1. Physical Port (Simulated via AF_PACKET on ens160)
    # 2. Exception Port (Virtio-User -> Kernel TAP)
    
    cmd_inner = f"echo '{PASS}' | sudo -S -E {REMOTE_PATH}/build/src/hft-app -l 0,1 -n 4 --proc-type=auto --file-prefix=hft1 --vdev=net_af_packet0,iface=ens160 --vdev=net_virtio_user0,mac=00:01:02:03:04:05,path=/dev/vhost-net,queue_size=1024"
    cmd_inner = f"echo '{PASS}' | timeout 15s sudo -S -E OKX_API_KEY=test OKX_API_SECRET=test OKX_PASSPHRASE=test BYBIT_API_KEY=test BYBIT_API_SECRET=test {REMOTE_PATH}/build/src/hft-app -l 0,1 -n 4 --proc-type=primary --file-prefix=hft_verify --vdev=net_af_packet0,iface=ens160 --vdev=net_af_packet1,iface=lo --vdev=net_virtio_user0,mac=00:01:02:03:04:05,path=/dev/vhost-net,queue_size=1024"
    # Added -v to ssh for debug
    cmd = f"ssh -v {USER}@{HOST} \"{cmd_inner}\""
    
    # We use run_expect to handle the SSH password if prompted.
    # The sudo password is handled by echo pipe.
    # Updated verification with Packet Injection
    
    def inject_packets():
        print("Injecting JSON packets in 2 seconds...")
        time.sleep(2)
        
        # 1. Start a dummy listener on VM so we can connect and send TCP data
        # We run it in background with a timeout
        listener_cmd = f"ssh {USER}@{HOST} \"timeout 10 nc -l -k -p 8443 > /dev/null 2>&1 &\""
        print(f"Injector: Starting listener: {listener_cmd}")
        subprocess.run(listener_cmd, shell=True)
        
        time.sleep(1)

        # 2. Prepare Payloads
        # OKX: channel books, update with bids/asks
        okx_payload = '{"arg":{"channel":"books","instId":"BTC-USDT-SWAP"},"action":"update","data":[{"bids":[["88000","1.5"]],"asks":[["88001","1.0"]]}]}'
        
        # Bybit: topic orderbook, snapshot
        bybit_payload = '{"topic":"orderbook.1.BTCUSDT","type":"snapshot","data":{"s":"BTCUSDT","b":[["88100","2.0"]],"a":[["88101","3.0"]]}}'
        
        payloads = [okx_payload, bybit_payload]
        
        for i, payload in enumerate(payloads):
            # Escape quotes for shell -> inside python string -> insde ssh command
            # It gets messy. We use single quotes for echo, so payload double quotes are fine.
            # But we need to be careful about JSON structure.
            # Use 'nc -w 1 127.0.0.1 8443' to send to loopback (captured by lo vdev)
            
            # Using 127.0.0.1 because hft-app listens on vdev lo
            inject_cmd = f"echo '{payload}' | nc -w 1 127.0.0.1 8443"
            ssh_cmd = f"ssh {USER}@{HOST} \"{inject_cmd}\""
            
            print(f"Injector {i}: {ssh_cmd}")
            run_expect(ssh_cmd, PASS, timeout=5, script_name=f"injector_{i}.exp")
            time.sleep(1)

    # Start injector thread
    injector = threading.Thread(target=inject_packets)
    injector.start()

    print(f"Executing: {cmd}")
    run_expect(cmd, PASS, timeout=40)
    
    injector.join()

if __name__ == "__main__":
    main()
