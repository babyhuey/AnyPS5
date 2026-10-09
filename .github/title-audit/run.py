import argparse
import hashlib
import io
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import unself

CATALOG = "blackbearreloaded/ps5-homebrew-catalog"
MODULE_DIRS = ("sce_module", "sce_modules", "prx")
STOP = re.compile(r"FAIL:|Failed to load module|NotImplemented|not implemented|terminate called|symbol lookup error|unresolved|Unhandled|exception", re.I)
NID = re.compile(r"(?<![A-Za-z0-9+\-])[A-Za-z0-9+\-]{11}(?![A-Za-z0-9+\-])")


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "anyps5-title-audit"})
    with urllib.request.urlopen(request, timeout=600) as response:
        return response.read()


def catalog(cache):
    cache.mkdir(parents=True, exist_ok=True)
    listing = json.loads(fetch(f"https://api.github.com/repos/{CATALOG}/contents/apps"))
    records = []
    for entry in listing:
        path = cache / entry["name"]
        if not path.exists():
            path.write_bytes(fetch(f"https://raw.githubusercontent.com/{CATALOG}/main/apps/{entry['name']}"))
        records.append(json.loads(path.read_text()))
    return records


def archive(record, cache):
    url = record.get("artifact_url") or ""
    if not url.endswith(".zip"):
        return None, f"release is not a zip: {url or 'none'}"
    path = cache / f"{record['titleid']}.zip"
    if not path.exists():
        data = fetch(url)
        if hashlib.sha256(data).hexdigest() != record.get("sha256"):
            return None, "sha256 mismatch"
        path.write_bytes(data)
    return path, None


def prepare(zip_path, work):
    if work.exists():
        shutil.rmtree(work)
    (work / "app0").mkdir(parents=True)
    (work / "src").mkdir()
    with zipfile.ZipFile(zip_path) as z:
        names = [n for n in z.namelist() if not n.endswith("/")]
        eboot = next(n for n in names if n.lower().endswith("eboot.bin"))
        root = eboot[: -len("eboot.bin")]
        for name in names:
            if not name.startswith(root):
                continue
            relative = name[len(root):]
            top = relative.split("/", 1)[0]
            target = work / ("src" if top in MODULE_DIRS else "app0") / relative
            if not target.resolve().is_relative_to(work.resolve()):
                raise ValueError(f"zip entry outside the work folder: {name}")
            if relative.lower() == "eboot.bin":
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(z.read(name))
        data = z.read(eboot)
    elf = data if data[:4] == b"\x7fELF" else unself.unwrap(data)[0]
    (work / "src" / "eboot.elf").write_bytes(elf)


MINGW_RUNTIME = ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")


def add_runtime(libs, mingw_bin):
    for name in MINGW_RUNTIME:
        if not (libs / name).exists():
            shutil.copy2(mingw_bin / name, libs / name)


def run(app, cwd, timeout, log):
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    started = time.time()
    with open(log, "wb") as out:
        process = subprocess.Popen([str(app)], cwd=cwd, stdout=out, stderr=subprocess.STDOUT, creationflags=flags,
                                   start_new_session=os.name != "nt")
        try:
            code = process.wait(timeout=timeout)
            state = f"exit {code}"
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True)
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            state = f"still running after {timeout}s"
    return state, round(time.time() - started, 1)


def first_stop(log, names):
    text = Path(log).read_bytes().decode("utf-8", "replace").splitlines()
    for line in text:
        if STOP.search(line):
            line = line.strip()[:300]
            named = NID.sub(lambda m: f"{m.group(0)} ({names[m.group(0)]})" if m.group(0) in names else m.group(0), line)
            return named
    return (text[-1].strip()[:300] if text else "")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--relinker", required=True, type=Path)
    parser.add_argument("--libs", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--label", default="main")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--linux", action="store_true")
    parser.add_argument("--skip", nargs="*", default=["PPSA99169"])
    parser.add_argument("--mingw-bin", type=Path, help="WinLibs mingw64\\bin; defaults to the directory of g++ on PATH")
    parser.add_argument("titles", nargs="*")
    args = parser.parse_args()
    names = json.loads((HERE / "nid_names.json").read_text())
    if not args.linux:
        mingw_bin = args.mingw_bin or Path(shutil.which("g++") or "").parent
        add_runtime(args.libs, mingw_bin)
    rows = []
    for record in sorted(catalog(args.cache / "records"), key=lambda r: r["titleid"]):
        tid = record["titleid"]
        if (args.titles and tid not in args.titles) or tid in args.skip:
            continue
        row = {"title": tid, "name": record.get("name"), "label": args.label}
        zip_path, problem = archive(record, args.cache)
        if problem:
            rows.append({**row, "stop": f"skipped: {problem}"})
            print(tid, rows[-1]["stop"], flush=True)
            continue
        work = args.work / args.label / tid
        try:
            prepare(zip_path, work)
        except Exception as error:
            rows.append({**row, "stop": f"skipped: {error}"})
            print(tid, rows[-1]["stop"], flush=True)
            continue
        app = work / ("app.elf" if args.linux else "app.exe")
        command = [str(args.relinker)] + ([] if args.linux else ["--windows"]) + ["--rpath", str(args.libs.resolve()).replace("\\", "/"), str(work / "src" / "eboot.elf"), str(app)]
        relink = subprocess.run(command, cwd=work, capture_output=True, text=True)
        (work / "relink.log").write_text(relink.stdout + relink.stderr)
        if relink.returncode != 0:
            last = (relink.stderr or relink.stdout).strip().splitlines()[-1:] or ["?"]
            rows.append({**row, "stop": f"relink failed: {last[0][:200]}"})
            print(tid, rows[-1]["stop"], flush=True)
            continue
        for module_dir in MODULE_DIRS:
            if (work / "src" / module_dir).is_dir() and not (work / "app0" / module_dir).exists():
                shutil.copytree(work / "src" / module_dir, work / "app0" / module_dir)
        if args.linux:
            app.chmod(0o755)
        state, seconds = run(app, work, args.timeout, work / "run.log")
        rows.append({**row, "stop": first_stop(work / "run.log", names), "state": state, "seconds": seconds})
        print(tid, state, "|", rows[-1]["stop"], flush=True)
    out = args.work / f"results-{args.label}"
    Path(f"{out}.json").write_text(json.dumps(rows, indent=1))
    lines = [f"| Title | Name | Result | First stop |", "|---|---|---|---|"]
    for r in rows:
        lines.append(f"| {r['title']} | {r['name']} | {r.get('state', '')} | {r['stop'].replace('|', '/')} |")
    Path(f"{out}.md").write_text("\n".join(lines) + "\n")
    print(f"wrote {out}.json and {out}.md")


if __name__ == "__main__":
    main()
