#!/usr/bin/env python3
"""Upload Halo CE map files to Xbox Dev Mode console via Xbox Device Portal REST API."""
import os
import sys
import glob
import time
import ssl
import json
import urllib.request
import urllib.parse
import subprocess

CONSOLE_ENDPOINT = "https://192.168.1.67:11443"
PACKAGE_FULL_NAME = "OpenCommunityEdition.Xbox_1.5.8.4_x64__z5m0xxnb6r8z8"
MAPS_DIR = "/storage/1F34-EBBE/apollo/Xemu/games/Halo (2276, Oct 12 2001)/maps"

def get_csrf_token(endpoint):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    req = urllib.request.Request(f"{endpoint.rstrip('/')}/")
    with urllib.request.urlopen(req, context=ctx) as resp:
        cookies = resp.headers.get("set-cookie", "")
        for part in cookies.split(";"):
            if "CSRF-Token=" in part:
                return part.split("CSRF-Token=")[1].strip()
    return None

def get_remote_maps(endpoint, csrf_token):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    params = {
        "knownfolderid": "LocalAppData",
        "packagefullname": PACKAGE_FULL_NAME,
        "path": "/LocalState/Warthog/maps"
    }
    url = f"{endpoint.rstrip('/')}/api/filesystem/apps/files?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={
        "Cookie": f"CSRF-Token={csrf_token}",
        "X-CSRF-Token": csrf_token
    })
    try:
        with urllib.request.urlopen(req, context=ctx) as resp:
            data = json.loads(resp.read().decode())
            return {item["Name"]: item.get("FileSize", 0) for item in data.get("Items", [])}
    except Exception as e:
        return {}

def ensure_remote_folder(endpoint, csrf_token):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    folders = [
        ("", "LocalState"),
        ("/LocalState", "Warthog"),
        ("/LocalState/Warthog", "maps"),
    ]
    for parent, name in folders:
        params = {
            "knownfolderid": "LocalAppData",
            "packagefullname": PACKAGE_FULL_NAME,
            "path": parent,
            "newfoldername": name
        }
        url = f"{endpoint.rstrip('/')}/api/filesystem/apps/folder?" + urllib.parse.urlencode(params)
        req = urllib.request.Request(url, data=b"", headers={
            "Cookie": f"CSRF-Token={csrf_token}",
            "X-CSRF-Token": csrf_token
        })
        try:
            with urllib.request.urlopen(req, context=ctx) as resp:
                pass
        except Exception:
            pass

def upload_map_file(endpoint, csrf_token, file_path):
    filename = os.path.basename(file_path)
    file_size = os.path.getsize(file_path)
    params = {
        "knownfolderid": "LocalAppData",
        "packagefullname": PACKAGE_FULL_NAME,
        "path": "/LocalState/Warthog/maps",
        "filename": filename
    }
    url = f"{endpoint.rstrip('/')}/api/filesystem/apps/file?" + urllib.parse.urlencode(params)
    cmd = [
        "xh", "--verify=no", "--form", "POST", url,
        f"Cookie:CSRF-Token={csrf_token}",
        f"X-CSRF-Token:{csrf_token}",
        f"file@{file_path}"
    ]
    started = time.time()
    res = subprocess.run(cmd, capture_output=True, text=True)
    elapsed = max(0.001, time.time() - started)
    speed_mb = (file_size / (1024 * 1024)) / elapsed
    if res.returncode != 0:
        raise RuntimeError(f"xh upload failed ({res.returncode}): {res.stderr}")
    if '"Success" : false' in res.stdout or '"Success":false' in res.stdout or '"Success": false' in res.stdout:
        raise RuntimeError(f"Upload rejected by Device Portal: {res.stdout.strip()}")
    return speed_mb

def main():
    source_dir = sys.argv[1] if len(sys.argv) > 1 else MAPS_DIR
    print(f"=== Halo CE Maps Transfer to Xbox One ===")
    print(f"Target: {CONSOLE_ENDPOINT}")
    print(f"Package: {PACKAGE_FULL_NAME}")
    print(f"Source Directory: {source_dir}")

    if not os.path.isdir(source_dir):
        print(f"Error: Maps directory not found: {source_dir}", file=sys.stderr)
        sys.exit(1)

    local_maps = sorted(glob.glob(os.path.join(source_dir, "*.map")))
    if not local_maps:
        print(f"Error: No .map files found in {source_dir}", file=sys.stderr)
        sys.exit(1)

    total_bytes = sum(os.path.getsize(f) for f in local_maps)
    print(f"Found {len(local_maps)} map files ({total_bytes / (1024*1024):.1f} MiB).")

    print("Connecting to Xbox Device Portal...")
    csrf_token = get_csrf_token(CONSOLE_ENDPOINT)
    if not csrf_token:
        print("Error: Could not acquire CSRF token from Xbox Device Portal.", file=sys.stderr)
        sys.exit(1)
    print(f"Connected (CSRF token: {csrf_token[:8]}...).")

    ensure_remote_folder(CONSOLE_ENDPOINT, csrf_token)
    remote_maps = get_remote_maps(CONSOLE_ENDPOINT, csrf_token)
    print(f"Existing maps on Xbox: {len(remote_maps)}")

    uploaded_count = 0
    uploaded_bytes = 0
    total_start = time.time()

    for idx, map_path in enumerate(local_maps, 1):
        filename = os.path.basename(map_path)
        size = os.path.getsize(map_path)
        size_mb = size / (1024 * 1024)

        if filename in remote_maps and remote_maps[filename] == size:
            print(f"[{idx}/{len(local_maps)}] {filename} ({size_mb:.1f} MiB) - already present, skipping.")
            continue

        print(f"[{idx}/{len(local_maps)}] Uploading {filename} ({size_mb:.1f} MiB)...", end="", flush=True)
        # Refresh token if expired
        try:
            speed = upload_map_file(CONSOLE_ENDPOINT, csrf_token, map_path)
        except Exception:
            csrf_token = get_csrf_token(CONSOLE_ENDPOINT)
            speed = upload_map_file(CONSOLE_ENDPOINT, csrf_token, map_path)

        uploaded_count += 1
        uploaded_bytes += size
        print(f" done ({speed:.1f} MiB/s)")

    total_elapsed = max(0.001, time.time() - total_start)
    print(f"\nTransfer complete! {uploaded_count} maps uploaded ({uploaded_bytes / (1024*1024):.1f} MiB in {total_elapsed:.1f}s).")

    # Verify final list
    final_maps = get_remote_maps(CONSOLE_ENDPOINT, csrf_token)
    print(f"Verified maps on Xbox console: {len(final_maps)}/{len(local_maps)}")
    missing = [os.path.basename(f) for f in local_maps if os.path.basename(f) not in final_maps]
    if missing:
        print(f"Warning: Missing maps: {missing}", file=sys.stderr)
        sys.exit(1)
    else:
        print("All Halo maps successfully transferred and verified!")

if __name__ == "__main__":
    main()
