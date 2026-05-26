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


def run_session(repo_root, build_dir, *args):
    return subprocess.run(
        [
            sys.executable,
            str(repo_root / "tools/python/run_sitl_session.py"),
            "--build-dir",
            str(build_dir),
            "--dry-run",
            *args,
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def port_is_free(host, port, kind):
    sock_type = socket.SOCK_STREAM if kind == "tcp" else socket.SOCK_DGRAM
    with socket.socket(socket.AF_INET, sock_type) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, port))


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


def run_pty_interrupt(
    command,
    repo_root,
    ready_text=None,
    ready_delay=1.0,
    post_ready_delay=0.0,
    timeout=20.0,
    env=None,
):
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
    ready_at = time.monotonic() + ready_delay
    try:
        while time.monotonic() < deadline:
            chunk = read_pty_once(selector, fd)
            output.extend(chunk)
            text = output.decode("utf-8", errors="replace")
            if ready_text is not None and ready_text in text:
                time.sleep(post_ready_delay)
                break
            if ready_text is None and time.monotonic() >= ready_at:
                if os.waitpid(pid, os.WNOHANG) != (0, 0):
                    return 1, text
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


def require_lifecycle_prereqs(repo_root, build_dir):
    sitl_runner = build_dir / "vehicle" / "sitl_runner"
    if not sitl_runner.exists():
        print(
            f"skipping lifecycle checks: {sitl_runner.relative_to(repo_root)} is missing",
            file=sys.stderr,
        )
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


def run_short_rerun(command, repo_root):
    result = subprocess.run(command, cwd=repo_root, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return False
    return True


def run_lifecycle_checks(repo_root, build_dir):
    if not require_lifecycle_prereqs(repo_root, build_dir):
        return SKIP

    with tempfile.TemporaryDirectory(prefix="altair-sitl-ctrlc-") as tmp:
        tmp_path = Path(tmp)

        standalone_output = tmp_path / "standalone.csv"
        standalone = [
            sys.executable,
            str(repo_root / "tools/python/run_sitl.py"),
            "--build-dir",
            str(build_dir),
            "--scenario",
            "cruise6dof",
            "--initial",
            str(repo_root / "tests/integration/cruise6dof_initial.ini"),
            "--duration",
            "30",
            "--dt",
            "0.01",
            "--output",
            str(standalone_output),
            "--realtime",
        ]
        code, output = run_pty_interrupt(standalone, repo_root)
        if not assert_interrupt_result("standalone run_sitl.py", code, output):
            return 1
        if not assert_no_processes("standalone run_sitl.py", [str(standalone_output)]):
            return 1
        if not run_short_rerun(
            [
                *standalone[:-6],
                "0.05",
                "--dt",
                "0.01",
                "--output",
                str(tmp_path / "standalone-rerun.csv"),
            ],
            repo_root,
        ):
            return 1

        session_output = tmp_path / "session.csv"
        session_bridge_port = 27551
        session_source_port = 27600
        session_ws_port = 28765
        session = [
            sys.executable,
            str(repo_root / "tools/python/run_sitl_session.py"),
            "--build-dir",
            str(build_dir),
            "--no-qgc",
            "--bridge-port",
            str(session_bridge_port),
            "--mavlink-port-base",
            str(session_source_port),
            "--ws-port",
            str(session_ws_port),
            "--duration",
            "30",
            "--output",
            str(session_output),
        ]
        code, output = run_pty_interrupt(
            session, repo_root, ready_text="== Run sitl ==", post_ready_delay=0.5, timeout=25.0
        )
        if not assert_interrupt_result("run_sitl_session.py", code, output):
            return 1
        if not assert_no_processes(
            "run_sitl_session.py",
            [
                str(session_output),
                str(session_bridge_port),
                str(session_source_port),
                str(session_ws_port),
            ],
        ):
            return 1
        for port, kind in (
            (session_bridge_port, "udp"),
            (session_ws_port, "tcp"),
        ):
            port_is_free("127.0.0.1", port, kind)
        if not run_short_rerun([*session, "--duration", "0.05"], repo_root):
            return 1

    return 0


def main():
    if len(sys.argv) != 3:
        print("usage: test_sitl_session.py <repo-root> <build-dir>", file=sys.stderr)
        return 2

    repo_root = Path(sys.argv[1])
    build_dir = Path(sys.argv[2])
    if not build_dir.is_absolute():
        build_dir = repo_root / build_dir
    result = run_session(
        repo_root, build_dir, "--duration", "0.2", "--output", "sitl_live_test.csv"
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    if "mavlink_live_bridge.py" not in result.stdout:
        print("session did not start the MAVLink bridge", file=sys.stderr)
        return 1
    if "--forward 127.0.0.1:14550" not in result.stdout:
        print("session did not forward to QGC by default", file=sys.stderr)
        return 1
    if "--ws-host 127.0.0.1 --ws-port 8765" not in result.stdout:
        print("session did not configure the live-link WebSocket endpoint", file=sys.stderr)
        return 1
    if "ws=ws://127.0.0.1:8765" not in result.stdout:
        print("session did not report the live-link WebSocket endpoint", file=sys.stderr)
        return 1
    retired_endpoint_label = "anim" "us_ws"
    retired_write_flag = "--writable-" + "".join(
        chr(code) for code in (97, 110, 105, 109, 117, 115)
    )
    if retired_endpoint_label in result.stdout or retired_write_flag in result.stdout:
        print("session dry-run output still contains retired UI wording", file=sys.stderr)
        return 1
    if "--mavlink-port 14551" not in result.stdout or "--realtime" not in result.stdout:
        print("session did not route realtime SITL through the bridge", file=sys.stderr)
        return 1
    if (
        "--mavlink-system-id 1" not in result.stdout
        or "--mavlink-source-port 14600" not in result.stdout
    ):
        print("session did not assign predictable MAVLink system id/source port", file=sys.stderr)
        return 1
    if "tests/integration/cruise6dof_initial.ini" not in result.stdout:
        print(
            "session did not use the repository initial-condition fixture by default",
            file=sys.stderr,
        )
        return 1

    result = run_session(repo_root, build_dir, "--no-qgc")
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    if "--no-forward" not in result.stdout:
        print("session did not disable bridge forwarding with --no-qgc", file=sys.stderr)
        return 1

    result = run_session(
        repo_root,
        build_dir,
        "--vehicles",
        "3",
        "--system-id-base",
        "21",
        "--mavlink-port-base",
        "14700",
        "--duration",
        "0.2",
        "--output",
        "swarm.csv",
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    for system_id, source_port in ((21, 14700), (22, 14701), (23, 14702)):
        if f"sitl sys{system_id}:" not in result.stdout:
            print(f"swarm did not include system {system_id}", file=sys.stderr)
            return 1
        if (
            f"--mavlink-system-id {system_id}" not in result.stdout
            or f"--mavlink-source-port {source_port}" not in result.stdout
        ):
            print(
                f"swarm did not assign predictable id/port for system {system_id}", file=sys.stderr
            )
            return 1
        if f"swarm_sys{system_id}.csv" not in result.stdout:
            print(f"swarm did not isolate output for system {system_id}", file=sys.stderr)
            return 1
    return run_lifecycle_checks(repo_root, build_dir)


if __name__ == "__main__":
    raise SystemExit(main())
