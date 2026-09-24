#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys

out, adt, chromium, patch_sha, artifact_sha, jobs = sys.argv[1:]
def head(path):
    return subprocess.check_output(["git", "-C", path, "rev-parse", "HEAD"], text=True).strip()
data = {
    "adt_commit": head(adt),
    "chromium_commit": head(chromium),
    "patch_queue_sha256": patch_sha,
    "artifact_sha256": artifact_sha,
    "jobs": int(jobs),
    "tests": {"launch": "unknown", "youtube": "unknown", "persistence": "unknown", "exit": "unknown"},
}
results = pathlib.Path(out).with_name("smoke-results.txt")
if results.exists():
    for line in results.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            if key in data["tests"]:
                data["tests"][key] = value
site_results = pathlib.Path(out).with_name("site-results.tsv")
data["site_tests"] = []
if site_results.exists():
    for line in site_results.read_text(encoding="utf-8").splitlines():
        name, status, url = line.split("\t", 2)
        data["site_tests"].append({"name": name, "status": status, "url": url})
pathlib.Path(out).write_text(json.dumps(data, indent=2) + "\n", encoding="ascii")
