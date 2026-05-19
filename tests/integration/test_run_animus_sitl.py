#!/usr/bin/env python3

import os
import pty
import selectors
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SKIP = 77


def run_launcher(repo_root, *args):
    return subprocess.run(
        [
            sys.executable,
            str(repo_root / "tools/python/run_animus_sitl.py"),
            "--dry-run",
            *args,
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def process_state(pid):
    try:
        fields = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8").split()
    except OSError:
        return None
    return fields[2] if len(fields) > 2 else None


def matching_processes(tokens):
    current = os.getpid()
    matches = []
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        pid = int(proc.name)
        if pid == current or process_state(pid) == "Z":
            continue
        try:
            raw = (proc / "cmdline").read_bytes()
        except OSError:
            continue
        if not raw:
            continue
        command = raw.replace(b"\0", b" ").decode("utf-8", errors="replace")
        if any(token in command for token in tokens):
            matches.append((pid, command))
    return matches


def wait_for_process_exit(pid, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = os.waitpid(pid, os.WNOHANG)
        if result == (0, 0):
            time.sleep(0.05)
            continue
        status = result[1]
        if os.WIFEXITED(status):
            return os.WEXITSTATUS(status)
        if os.WIFSIGNALED(status):
            return -os.WTERMSIG(status)
        return 1
    return None


def read_pty_once(selector, fd):
    events = selector.select(timeout=0.05)
    if not events:
        return b""
    try:
        return os.read(fd, 4096)
    except OSError:
        return b""


def run_pty_interrupt(command, repo_root, ready_text, timeout=25.0, env=None):
    child_env = os.environ.copy()
    if env:
        child_env.update(env)
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(repo_root)
        os.execvpe(command[0], command, child_env)

    output = bytearray()
    selector = selectors.DefaultSelector()
    selector.register(fd, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            chunk = read_pty_once(selector, fd)
            output.extend(chunk)
            text = output.decode("utf-8", errors="replace")
            if ready_text in text:
                time.sleep(0.5)
                break
        else:
            os.write(fd, b"\x03")
            code = wait_for_process_exit(pid, 5.0)
            return code if code is not None else 1, output.decode("utf-8", errors="replace")

        os.write(fd, b"\x03")
        code = wait_for_process_exit(pid, 10.0)
        while True:
            chunk = read_pty_once(selector, fd)
            if not chunk:
                break
            output.extend(chunk)
        if code is None:
            return 1, output.decode("utf-8", errors="replace")
        return code, output.decode("utf-8", errors="replace")
    finally:
        selector.close()
        try:
            os.close(fd)
        except OSError:
            pass


def port_is_free(host, port):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, port))


def require_lifecycle_prereqs(repo_root, build_dir, animus_build_dir):
    prereqs = (
        build_dir / "vehicle" / "sitl_runner",
        animus_build_dir / "tools" / "animus-qt" / "animus_qt",
    )
    for prereq in prereqs:
        if not prereq.exists():
            print(f"skipping lifecycle checks: {prereq.relative_to(repo_root)} is missing")
            return False
    return True


def assert_interrupt_result(label, code, output):
    if code != 130:
        print(f"{label} exited with {code}, expected 130", file=sys.stderr)
        print(output, end="")
        return False
    if "Traceback" in output:
        print(f"{label} printed a traceback during Ctrl-C shutdown", file=sys.stderr)
        print(output, end="")
        return False
    return True


def assert_no_processes(label, tokens):
    deadline = time.monotonic() + 15.0
    matches = []
    while time.monotonic() < deadline:
        matches = matching_processes(tokens)
        if not matches:
            return True
        time.sleep(0.1)
    print(f"{label} left matching processes running:", file=sys.stderr)
    for pid, command in matches:
        print(f"{pid}: {command}", file=sys.stderr)
    return False


def run_short_rerun(command, repo_root, env):
    result = subprocess.run(
        command, cwd=repo_root, check=False, capture_output=True, text=True, env=env
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return False
    return True


def run_lifecycle_checks(repo_root, build_dir, animus_build_dir):
    if not require_lifecycle_prereqs(repo_root, build_dir, animus_build_dir):
        return SKIP

    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    with tempfile.TemporaryDirectory(prefix="altair-animus-ctrlc-") as tmp:
        tmp_path = Path(tmp)
        output_path = tmp_path / "animus.csv"
        rerun_output_path = tmp_path / "animus-rerun.csv"
        udp_port = 28551
        source_port = 28600
        launcher = [
            sys.executable,
            str(repo_root / "tools/python/run_animus_sitl.py"),
            "--skip-build",
            "--udp-port",
            str(udp_port),
            "--mavlink-source-port",
            str(source_port),
            "--duration",
            "30",
            "--output",
            str(output_path),
        ]

        code, output = run_pty_interrupt(
            launcher, repo_root, ready_text="== Run SITL ==", timeout=25.0, env=env
        )
        if not assert_interrupt_result("run_animus_sitl.py", code, output):
            return 1
        if not assert_no_processes(
            "run_animus_sitl.py",
            [
                str(output_path),
                str(udp_port),
                str(source_port),
                str(animus_build_dir / "tools" / "animus-qt" / "animus_qt"),
                str(repo_root / "tools/python/run_animus_sitl.py"),
                str(repo_root / "tools/python/run_sitl.py"),
                str(build_dir / "vehicle" / "sitl_runner"),
            ],
        ):
            return 1
        port_is_free("127.0.0.1", udp_port)
        port_is_free("127.0.0.1", source_port)
        if not run_short_rerun(
            [
                *launcher[:-2],
                "--output",
                str(rerun_output_path),
                "--duration",
                "0.05",
            ],
            repo_root,
            env,
        ):
            return 1

    return 0


def main():
    if len(sys.argv) not in (3, 4):
        print(
            "usage: test_run_animus_sitl.py <repo-root> <build-dir> [animus-build-dir]",
            file=sys.stderr,
        )
        return 2

    repo_root = Path(sys.argv[1])
    build_dir = Path(sys.argv[2])
    if not build_dir.is_absolute():
        build_dir = repo_root / build_dir
    animus_build_dir = Path(sys.argv[3]) if len(sys.argv) == 4 else repo_root / "build-animus-qt"
    if not animus_build_dir.is_absolute():
        animus_build_dir = repo_root / animus_build_dir
    result = run_launcher(repo_root, "--duration", "0.2", "--output", "sitl_animus_test.csv")
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode

    checks = (
        ("cmake -S", "launcher did not configure builds by default"),
        ("--target sitl_runner", "launcher did not build sitl_runner"),
        ("--target animus_qt", "launcher did not build animus_qt"),
        (
            "--start-udp-telemetry --udp-host 127.0.0.1 --udp-port 14551",
            "Animus UDP startup missing",
        ),
        ("tools/python/run_sitl.py", "launcher did not run the SITL helper"),
        ("--scenario cruise6dof", "launcher did not select cruise6dof"),
        ("--realtime --mavlink", "launcher did not request realtime MAVLink output"),
        ("--mavlink-port 14551", "launcher did not route SITL to Animus UDP"),
        ("--mavlink-source-port 14600", "launcher did not set predictable source port"),
        (
            "tests/integration/cruise6dof_initial.ini",
            "launcher did not use the default initial fixture",
        ),
    )
    for needle, message in checks:
        if needle not in result.stdout:
            print(message, file=sys.stderr)
            print(result.stdout, end="")
            return 1

    result = run_launcher(repo_root, "--skip-build", "--udp-port", "27551", "--case", "case.ini")
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    if "build 1:" in result.stdout:
        print("--skip-build still printed build commands", file=sys.stderr)
        return 1
    if "--udp-port 27551" not in result.stdout or "--mavlink-port 27551" not in result.stdout:
        print("custom UDP port was not applied to both Animus and SITL", file=sys.stderr)
        return 1
    if "--case case.ini" not in result.stdout or "--initial" in result.stdout:
        print("--case did not replace the default --initial path", file=sys.stderr)
        return 1

    return run_lifecycle_checks(repo_root, build_dir, animus_build_dir)


if __name__ == "__main__":
    raise SystemExit(main())
