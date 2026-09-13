"""
radio_tests.py — unit tests for the sikradio internet radio client.

Key design rules
----------------
- Do not call proc.wait() before draining proc.stdout/stderr.
  Use proc.communicate() when collecting process output.
- For interactive tests (quit, SIGINT), use launch_client_interactive()
  and call communicate() after sending input or a signal.
- Never use a hardcoded port for refused connections; use refused_url().
- On test failure, stdout and stderr are written to <TestName>.log so that
  the grader has a concrete dump of the client's output.
- On success, no log file is left behind.
"""

import os
import signal
import socket
import threading
import time
import unittest
import subprocess
import sys


# ---------------------------------------------------------------------------
# Locate the client binary
# ---------------------------------------------------------------------------

CLIENT_BIN_CANDIDATES = (
    "./sikradio",
    "./cmake-build-release/sikradio",
    "./cmake-build-debug/sikradio",
)
CLIENT_BIN = None

LOG_DIR = "test_logs"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

LOCALHOST = "127.0.0.1"


def terminate_process(proc, timeout=1.0):
    """Send SIGINT and wait for the process to exit; kill it on timeout."""
    if proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def free_port():
    """Return an ephemeral TCP port that is currently free."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("", 0))
        return s.getsockname()[1]


def refused_url():
    """
    Return a URL whose TCP connection should be refused immediately.

    Bind an ephemeral port, record it, close the socket, and return a URL
    using the now-unused port.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((LOCALHOST, 0))
        port = s.getsockname()[1]
    return f"http://{LOCALHOST}:{port}/"


class MockServer:
    """
    Minimal TCP server that calls handler(conn) for each accepted connection.

    When reuse=True, the server accepts multiple sequential connections.
    """

    def __init__(self, handler, reuse=False):
        self.handler = handler
        self.reuse   = reuse
        self.port    = free_port()
        self._sock   = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((LOCALHOST, self.port))
        self._sock.listen(8)
        self._sock.settimeout(5.0)
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._stop   = threading.Event()

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        try:
            self._sock.close()
        except OSError:
            pass

    def _serve(self):
        while not self._stop.is_set():
            try:
                conn, _ = self._sock.accept()
            except OSError:
                break
            threading.Thread(
                target=self._handle_one, args=(conn,), daemon=True
            ).start()
            if not self.reuse:
                break

    def _handle_one(self, conn):
        try:
            self.handler(conn)
        except OSError:
            # klient mógł zamknąć połączenie przed odpowiedzią (reconnect / RST)
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def url(self, path="/stream"):
        return f"http://{LOCALHOST}:{self.port}{path}"


def http_200(conn, body=b"AUDIO", metaint=None):
    headers  = "HTTP/1.1 200 OK\r\n"
    headers += "Content-Type: audio/mpeg\r\n"
    if metaint is not None:
        headers += f"icy-metaint: {metaint}\r\n"
    headers += "Connection: close\r\n\r\n"
    conn.sendall(headers.encode() + body)


def icy_200(conn, body=b"AUDIO"):
    headers  = "ICY 200 OK\r\n"
    headers += "icy-name: Test Radio\r\n"
    headers += "content-type: audio/mpeg\r\n"
    headers += "\r\n"
    conn.sendall(headers.encode() + body)


def http_302(conn, location):
    resp  = "HTTP/1.1 302 Found\r\n"
    resp += f"Location: {location}\r\n"
    resp += "Content-Length: 0\r\nConnection: close\r\n\r\n"
    conn.sendall(resp.encode())


def read_request(conn):
    """Read from conn until the blank line terminating the HTTP headers."""
    data = b""
    conn.settimeout(3.0)
    while b"\r\n\r\n" not in data and b"\n\n" not in data:
        try:
            chunk = conn.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        data += chunk
    return data.decode(errors="replace")


def header_value(request, name):
    """Return the value of a request header, or None if it is absent."""
    prefix = f"{name}: "
    for line in request.split("\r\n"):
        if line.startswith(prefix):
            return line[len(prefix):]
    return None


def build_icy_stream(audio_chunk: bytes, metaint: int, title: str) -> bytes:
    """
    Build one complete ICY metadata cycle used by metadata tests:

        [metaint audio bytes][1-byte block-count][block-count * 16 meta bytes]

    No trailing audio is added; the caller decides what follows.
    """
    assert len(audio_chunk) == metaint
    raw_meta    = f"StreamTitle='{title}';\x00".encode()
    blocks      = (len(raw_meta) + 15) // 16
    meta_blob   = raw_meta.ljust(blocks * 16, b"\x00")
    length_byte = bytes([blocks])
    return audio_chunk + length_byte + meta_blob


# ---------------------------------------------------------------------------
# Base class — failure logging
# ---------------------------------------------------------------------------

class SikradioTestBase(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        global CLIENT_BIN
        for exe in CLIENT_BIN_CANDIDATES:
            if os.path.exists(exe):
                CLIENT_BIN = exe
                return
        raise FileNotFoundError(
            f"sikradio binary not found in {CLIENT_BIN_CANDIDATES}"
        )

    def setUp(self):
        # Track whether any subtest in this method has failed.
        # This prevents a passing subtest from removing a failing sibling's log.
        self._had_failure = False

    def _log_path(self, label=None):
        os.makedirs(LOG_DIR, exist_ok=True)
        raw = label if label else self.id()
        safe = raw.replace(" ", "_").replace("/", "-").replace(":", "-")
        return os.path.join(LOG_DIR, f"{safe}.log")

    def _write_log(self, rc, stdout, stderr, args, label=None):
        """Write a failure dump to <LOG_DIR>/<label>.log."""
        self._had_failure = True
        path = self._log_path(label)
        with open(path, "wb") as f:
            f.write(f"=== COMMAND ===\n{CLIENT_BIN} {' '.join(args)}\n".encode())
            f.write(f"=== RETURN CODE ===\n{rc}\n".encode())
            f.write(b"=== STDOUT ===\n")
            f.write(stdout[:4096])
            if len(stdout) > 4096:
                f.write(f"\n... ({len(stdout)} bytes total, truncated)\n".encode())
            f.write(b"\n=== STDERR ===\n")
            f.write(stderr)

    def _remove_log(self, label=None):
        # Never remove a log if any failure has occurred in this test method.
        if self._had_failure:
            return
        path = self._log_path(label)
        if os.path.exists(path):
            os.remove(path)

    def assertTestPasses(self, rc, stdout, stderr, args, assertions, label=None):
        """
        Run assertions(rc, stdout, stderr).

        On failure, write <label>.log and re-raise.
        On success, remove a stale log for this label if no prior failure occurred.
        Use a unique label for each subTest iteration so each gets its own log.
        """
        try:
            assertions(rc, stdout, stderr)
        except AssertionError:
            self._write_log(rc, stdout, stderr, args, label)
            raise
        else:
            self._remove_log(label)


    def launch_client(self, args, input_bytes=None, comm_timeout=15):
        """
        Launch the client and immediately call communicate() to drain output.

        Returns (returncode, stdout_bytes, stderr_bytes).
        On timeout, kill the process, write a log, and fail the test.
        """
        cmd  = [CLIENT_BIN] + args
        proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.addCleanup(terminate_process, proc)
        try:
            out, err = proc.communicate(input=input_bytes, timeout=comm_timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = proc.communicate()
            self._write_log(-1, out, err, args)
            self.fail(f"client timed out after {comm_timeout}s: {args}")
        return proc.returncode, out, err

    def launch_client_interactive(self, args):
        """
        Launch the client without waiting for it to exit.

        The caller controls input and termination. Cleanup terminates the process
        if the test does not terminate it explicitly.
        """
        cmd  = [CLIENT_BIN] + args
        proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.addCleanup(terminate_process, proc)
        return proc

    def communicate_interactive(self, proc, args, timeout=5):
        """
        Drain output from an interactive process.

        On timeout, kill the process, write a log, and fail the test.
        Returns (stdout_bytes, stderr_bytes).
        """
        try:
            out, err = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = proc.communicate()
            self._write_log(proc.returncode, out, err, args)
            self.fail(f"interactive client timed out after {timeout}s")
        return out, err


# ===========================================================================
# 1. Argument Tests
# ===========================================================================

class ArgTests(SikradioTestBase):

    def test_missing_mandatory_url(self):
        args = ["-m", "-t", "5000", "-v", "2"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_invalid_args(self):
        cases = [
            (["-t", "99"],    "t_too_low"),
            (["-t", "100001"],"t_too_high"),
            (["-t", "abc"],   "t_not_a_number"),
            (["-v", "-1"],    "v_negative"),
            (["-v", "5"],     "v_too_high"),
            (["-v", "abc"],   "v_not_a_number"),
            (["-x"],          "unknown_flag"),
        ]
        for extra_args, name in cases:
            with self.subTest(name=name):
                args = ["-u", refused_url()] + extra_args
                rc, out, err = self.launch_client(args)
                self.assertTestPasses(
                    rc, out, err, args,
                    lambda r, o, e: self.assertEqual(1, r),
                    label=f"{self.id()}.{name}",
                )

    def test_valid_minimum_invocation(self):
        """A refused connection must not be reported as an argument error."""
        args = ["-u", refused_url(), "-t", "100"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(1, r),
            self.assertNotEqual(-signal.SIGSEGV, r),
        ))

    def test_both_ip_flags_accepted(self):
        """Using -4 and -6 together is not an argument error according to the spec."""
        args = ["-u", refused_url(), "-4", "-6", "-t", "100"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(
            rc, out, err, args,
            lambda r, o, e: self.assertNotEqual(-signal.SIGSEGV, r)
        )

    def test_q_flag_produces_no_stderr(self):
        def handler(conn):
            read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)

        args = ["-u", srv.url(), "-q"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(b"", e))

    def test_params_without_space(self):
        """The -t100 form is accepted by getopt."""
        args = ["-u", refused_url(), "-t100"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(
            rc, out, err, args,
            lambda r, o, e: self.assertNotEqual(-signal.SIGSEGV, r)
        )

    def test_blocked_flags(self):
        """The -m46 form is parsed as -m -4 -6."""
        args = ["-u", refused_url(), "-m46", "-t100"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(
            rc, out, err, args,
            lambda r, o, e: self.assertNotEqual(-signal.SIGSEGV, r)
        )

    def test_timeout_boundary_100_accepted(self):
        args = ["-u", refused_url(), "-t", "100"]
        rc, out, err = self.launch_client(args)
        # rc=1 indicates a connection failure, not an argument error.
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_timeout_boundary_100000_accepted(self):
        args = ["-u", refused_url(), "-t", "100000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(
            rc, out, err, args,
            lambda r, o, e: self.assertNotEqual(-signal.SIGSEGV, r)
        )


# ===========================================================================
# 2. Quit Tests
# ===========================================================================

class QuitTests(SikradioTestBase):

    def _streaming_server(self, conn):
        """Send an infinite audio stream and keep the connection open."""
        read_request(conn)
        conn.sendall(
            b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
            b"Connection: keep-alive\r\n\r\n"
        )
        conn.settimeout(0.1)
        try:
            while True:
                conn.sendall(b"\xff" * 512)
                time.sleep(0.05)
        except OSError:
            pass

    def test_quit_exits_zero(self):
        srv = MockServer(self._streaming_server, reuse=True).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "5000"]

        proc = self.launch_client_interactive(args)
        time.sleep(0.5)
        proc.stdin.write(b"quit\n")
        proc.stdin.flush()

        out, err = self.communicate_interactive(proc, args, timeout=4)
        self.assertTestPasses(
            proc.returncode, out, err, args,
            lambda r, o, e: self.assertEqual(0, r)
        )

    def test_non_quit_input_ignored(self):
        """Non-quit input must not terminate the client."""
        srv = MockServer(self._streaming_server, reuse=True).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "5000"]

        proc = self.launch_client_interactive(args)
        time.sleep(0.4)
        proc.stdin.write(b"hello\n")
        proc.stdin.flush()
        time.sleep(0.4)

        still_running = proc.poll() is None
        terminate_process(proc)
        out, err = b"", b""
        try:
            out, err = proc.communicate(timeout=1)
        except subprocess.TimeoutExpired:
            pass

        self.assertTestPasses(
            proc.returncode or 0, out, err, args,
            lambda r, o, e: self.assertTrue(
                still_running, "client exited on non-quit input"
            )
        )


# ===========================================================================
# 3. Server Interaction Tests
# ===========================================================================

class ServerTests(SikradioTestBase):

    def test_audio_forwarded_to_stdout(self):
        payload = b"\x00\x01\x02\x03" * 256

        def handler(conn):
            read_request(conn)
            http_200(conn, body=payload)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(payload, o),
        ))

    def test_icy_200_response_accepted(self):
        payload = b"X" * 512

        def handler(conn):
            read_request(conn)
            icy_200(conn, body=payload)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(payload, o),
        ))

    def test_server_close_exits_zero(self):
        """A server closing the connection after a valid response is a clean exit."""
        def handler(conn):
            read_request(conn)
            http_200(conn, body=b"data")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(0, r))

    def test_audio_bytes_not_corrupted(self):
        """All 256 byte values must survive the pipe unchanged."""
        payload = bytes(range(256)) * 16

        def handler(conn):
            read_request(conn)
            http_200(conn, body=payload)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "3000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertEqual(payload, o),
        ))

    def test_large_audio_stream(self):
        """A 1 MiB stream must arrive without corruption."""
        payload = b"\xAB\xCD" * (512 * 1024)

        def handler(conn):
            read_request(conn)
            http_200(conn, body=payload)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "5000"]

        rc, out, err = self.launch_client(args, comm_timeout=30)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertEqual(payload, o),
        ))

    def test_non_200_status_exits_one(self):
        def handler(conn):
            read_request(conn)
            conn.sendall(b"HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_404_exits_one(self):
        def handler(conn):
            read_request(conn)
            conn.sendall(b"HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_302_redirect_followed(self):
        payload = b"REDIRECTED_AUDIO"

        def final_handler(conn):
            read_request(conn)
            http_200(conn, body=payload)

        final_srv = MockServer(final_handler).start()
        self.addCleanup(final_srv.stop)

        def redirect_handler(conn):
            read_request(conn)
            http_302(conn, location=final_srv.url())

        redir_srv = MockServer(redirect_handler).start()
        self.addCleanup(redir_srv.stop)
        args = ["-u", redir_srv.url(), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(payload, o),
        ))

    def test_302_no_location_exits_one(self):
        def handler(conn):
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 302 Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
            )

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_timeout_triggers_reconnect(self):
        """
        A data timeout must cause the client to disconnect and reconnect.

        The first connection goes silent after the headers; the second
        connection delivers audio.
        """
        call_count = {"n": 0}

        def handler(conn):
            call_count["n"] += 1
            read_request(conn)
            if call_count["n"] == 1:
                conn.sendall(
                    b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                    b"Connection: close\r\n\r\n"
                )
                time.sleep(4)   # longer than -t 500
            else:
                http_200(conn, body=b"AUDIO_AFTER_RECONNECT")

        srv = MockServer(handler, reuse=True).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "500", "-v4"]

        rc, out, err = self.launch_client(args, comm_timeout=12)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(b"AUDIO_AFTER_RECONNECT", o),
            self.assertGreaterEqual(call_count["n"], 2),
        ))

    def test_timeout_message_on_stderr(self):
        """The data receiving timeout message appears at verbosity >= 1."""
        def handler(conn):
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                b"Connection: close\r\n\r\n"
            )
            time.sleep(4)

        srv = MockServer(handler, reuse=True).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "300", "-v1"]

        proc = self.launch_client_interactive(args)
        time.sleep(1.5)
        proc.send_signal(signal.SIGINT)
        out, err = self.communicate_interactive(proc, args, timeout=3)

        self.assertTestPasses(
            proc.returncode, out, err, args,
            lambda r, o, e: self.assertIn(b"data receiving timeout", e)
        )

    def test_icy_metadata_printed_to_stderr(self):
        """With -m enabled, StreamTitle appears on stderr."""
        metaint = 64
        title   = "Test Artist - Test Song"
        stream  = build_icy_stream(b"A" * metaint, metaint, title)

        def handler(conn):
            read_request(conn)
            headers = (
                f"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                f"icy-metaint: {metaint}\r\nConnection: close\r\n\r\n"
            )
            conn.sendall(headers.encode() + stream)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-m", "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(title.encode(), e),
        ))

    def test_metadata_not_in_audio_stream(self):
        """ICY metadata bytes must not appear in stdout."""
        metaint = 64
        title   = "SHOULD_NOT_BE_IN_AUDIO"
        audio   = b"A" * metaint
        stream  = build_icy_stream(audio, metaint, title)

        def handler(conn):
            read_request(conn)
            headers = (
                f"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                f"icy-metaint: {metaint}\r\nConnection: close\r\n\r\n"
            )
            conn.sendall(headers.encode() + stream)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-m", "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertNotIn(title.encode(), o),
            self.assertIn(audio, o),
        ))

    def test_no_icy_header_without_m_flag(self):
        received = {}

        def handler(conn):
            received["req"] = read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertNotIn("Icy-MetaData", received.get("req", "")),
        ))

    def test_icy_header_present_with_m_flag(self):
        received = {}

        def handler(conn):
            received["req"] = read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-m", "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertIn("Icy-MetaData: 1", received.get("req", "")),
        ))

    def test_host_header_correct(self):
        received = {}

        def handler(conn):
            received["req"] = read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url("/audio"), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(
                f"{LOCALHOST}:{srv.port}",
                header_value(received.get("req", ""), "Host"),
            ),
        ))

    def test_get_path_correct(self):
        received = {}

        def handler(conn):
            received["req"] = read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url("/test/path?q=1"), "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertIn("GET /test/path?q=1 HTTP/1.1", received.get("req", "")),
        ))

    def test_root_path_when_url_has_no_path(self):
        received = {}

        def handler(conn):
            received["req"] = read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", f"http://{LOCALHOST}:{srv.port}", "-t", "2000"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertRegex(received.get("req", ""), r"GET / HTTP/1\.1"),
        ))

    def test_verbosity_0_no_stderr(self):
        def handler(conn):
            read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-v0"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertEqual(b"", e),
        ))

    def test_verbosity_1_shows_resolving(self):
        def handler(conn):
            read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-v1"]

        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(b"resolving name", e),
        ))

    def test_verbosity_2_shows_error_on_failure(self):
        """At default verbosity, critical errors appear on stderr."""
        args = ["-u", refused_url(), "-t", "200"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(1, r),
            self.assertGreater(len(e), 0),
        ))


# ===========================================================================
# 4. Signal Tests
# ===========================================================================

class SignalTests(SikradioTestBase):

    def _infinite_server(self, conn):
        read_request(conn)
        conn.sendall(
            b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
            b"Connection: keep-alive\r\n\r\n"
        )
        conn.settimeout(0.1)
        try:
            while True:
                conn.sendall(b"\x00" * 256)
                time.sleep(0.1)
        except OSError:
            pass


# ===========================================================================
# 5. Malicious & Edge Case Tests
# ===========================================================================

class MaliciousEdgeCaseTests(SikradioTestBase):
    """
    Test robustness against malformed or malicious server behavior.

    Expected return codes:
      rc=0  server closed the connection = STREAM_DONE = clean exit
      rc=1  parse/protocol error or connection failure = EXIT_FAILURE
    """

    # -------------------------------------------------------------------
    # Garbled / truncated status line
    # -------------------------------------------------------------------

    def test_immediate_connection_close(self):
        """
        The server accepts the connection and immediately closes it.

        Zero bytes are received, so the client should exit with rc=1.
        """
        def handler(conn):
            pass   # MockServer closes the connection after the handler returns

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_malformed_status_line(self):
        """A response that starts with neither HTTP/ nor ICY must result in rc=1."""
        def handler(conn):
            read_request(conn)
            conn.sendall(b"HELLO THIS IS DOG NOT HTTP\r\n\r\n")

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_drop_mid_status_line(self):
        """
        The connection closes in the middle of the status line before CRLF.
        The incomplete status code should be rejected with rc=1.
        """
        def handler(conn):
            read_request(conn)
            conn.sendall(b"HTTP/1.1 20")   # no CRLF, EOF after that

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_garbled_responses_no_crash(self):
        """
        Test several malformed server responses.

        The common requirement is that the client must not hang or crash.
        Each variation uses a unique log label.

        Expected return codes:
          null_bytes_only   -> not HTTP/ICY -> rc=1
          eof_mid_protocol  -> incomplete protocol prefix -> rc=1
          http_missing_code -> no status code -> rc=1 or rc=0 depending on implementation
          icy_garbage_code  -> invalid status code -> rc=1
          binary_noise      -> not HTTP/ICY -> rc=1
        """
        cases = [
            (b"\x00\x00\x00\x00",              "null_bytes_only",    1),
            (b"HTT",                            "eof_mid_protocol",   1),
            (b"HTTP/1.1 \r\n\r\n",             "http_missing_code",  1),
            (b"ICY abc OK\r\n\r\n",            "icy_garbage_code",   1),
            (b"\xff\xfe\x00\x01" * 32,         "binary_noise",       1),
        ]
        for payload, name, expected_rc in cases:
            with self.subTest(variation=name):
                # Each iteration needs its own MockServer because MockServer
                # is single-use by default.
                def make_handler(p):
                    def handler(conn):
                        read_request(conn)
                        conn.sendall(p)
                    return handler

                srv = MockServer(make_handler(payload)).start()
                args = ["-u", srv.url(), "-t", "2000"]
                rc, out, err = self.launch_client(args)
                srv.stop()
                self.assertTestPasses(
                    rc, out, err, args,
                    lambda r, o, e, exp=expected_rc: (
                        self.assertNotEqual(-signal.SIGSEGV, r),
                        self.assertEqual(exp, r),
                    ),
                    label=f"{self.id()}.{name}",
                )

    # -------------------------------------------------------------------
    # Oversized / overflowing headers
    # -------------------------------------------------------------------

    def test_massive_header_value_no_crash(self):
        """
        Send a single header with an 8 KiB value.

        The client may truncate or reject the value, but must not segfault.
        """
        def handler(conn):
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\n"
                b"X-Oversized: " + (b"A" * 8192) + b"\r\n"
                                                   b"Content-Type: audio/mpeg\r\nConnection: close\r\n\r\n"
                                                   b"AUDIO"
            )

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(
            rc, out, err, args,
            lambda r, o, e: self.assertNotEqual(-signal.SIGSEGV, r),
        )

    def test_header_name_no_colon_no_crash(self):
        """A malformed header without a colon must not crash the client."""
        def handler(conn):
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\n"
                b"ThisHasNoColonAtAll\r\n"
                b"Content-Type: audio/mpeg\r\nConnection: close\r\n\r\n"
                b"AUDIO"
            )

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertNotEqual(-signal.SIGSEGV, r),
            self.assertEqual(0, r),
        ))

    def test_hundreds_of_headers_no_crash(self):
        """A response with 200 headers must not crash the client."""
        def handler(conn):
            read_request(conn)
            headers = b"HTTP/1.1 200 OK\r\n"
            for i in range(200):
                headers += f"X-Header-{i}: value\r\n".encode()
            headers += b"Content-Type: audio/mpeg\r\nConnection: close\r\n\r\nAUDIO"
            conn.sendall(headers)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(
            rc, out, err, args,
            lambda r, o, e: self.assertNotEqual(-signal.SIGSEGV, r),
        )

    # -------------------------------------------------------------------
    # Mid-stream drops
    # -------------------------------------------------------------------

    def test_drop_mid_audio(self):
        """
        The connection closes after valid headers and partial audio data.

        A server-initiated close is a clean exit, and data received before
        the close must be present on stdout.
        """
        payload = b"FIRST_HALF_DATA"

        def handler(conn):
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                b"Connection: close\r\n\r\n"
            )
            conn.sendall(payload)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(payload, o),
        ))


    # -------------------------------------------------------------------
    # ICY metadata edge cases
    # -------------------------------------------------------------------

    def test_zero_length_metadata_block(self):
        """
        A metadata length byte of 0x00 represents an empty metadata block.

        The client must skip the empty block and continue reading audio.
        """
        metaint = 8

        def handler(conn):
            read_request(conn)
            conn.sendall((
                             "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                             f"icy-metaint: {metaint}\r\nConnection: close\r\n\r\n"
                         ).encode())
            conn.sendall(b"AUDIO123")    # 8 bytes of audio
            conn.sendall(b"\x00")        # Empty metadata block
            conn.sendall(b"AUDIOBCD")    # Next 8 bytes of audio

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-m", "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(b"AUDIO123", o),
            self.assertIn(b"AUDIOBCD", o),
            self.assertNotIn(b"\x00", o),   # The length byte must not reach stdout
        ))

    def test_huge_metadata_block_no_crash(self):
        """
        Parse the maximum ICY metadata block (255 * 16 = 4080 bytes)
        without crashing. After the complete block, EOF should produce
        STREAM_DONE and rc=0.
        """
        meta = b"StreamTitle='Giant Meta Block';\x00".ljust(4080, b"\x00")

        def handler(conn):
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                b"icy-metaint: 5\r\nConnection: close\r\n\r\n"
            )
            conn.sendall(b"12345")  # 5 bytes of audio (= metaint)
            conn.sendall(b"\xFF")   # Metadata block length: 255 * 16 = 4080 bytes
            conn.sendall(meta)      # Complete metadata block, then EOF

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-m", "-t", "3000"]
        rc, out, err = self.launch_client(args, comm_timeout=10)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(b"Giant Meta Block", e),
            self.assertNotIn(b"\xFF" + meta[:8], o),  # Metadata must not reach stdout
        ))

    def test_metadata_with_special_chars(self):
        """Metadata titles may contain apostrophes, quotes, and UTF-8 characters."""
        metaint = 16
        title   = "Édith Piaf - L'Hymne à l'amour"
        stream  = build_icy_stream(b"X" * metaint, metaint, title)

        def handler(conn):
            read_request(conn)
            conn.sendall((
                             "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                             f"icy-metaint: {metaint}\r\nConnection: close\r\n\r\n"
                         ).encode() + stream)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-m", "-t", "2000"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertNotEqual(-signal.SIGSEGV, r),
            # The title must appear on stderr; check an ASCII fragment.
            self.assertIn(b"Piaf", e),
        ))

    # -------------------------------------------------------------------
    # Slowloris / drip-feed
    # -------------------------------------------------------------------

    def test_slowloris_drip_audio(self):
        """
        Data arrives every 200 ms with an 800 ms timeout.

        Each recv() receives data before the timeout, so the client must
        keep the connection open.
        """
        def handler(conn):
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                b"Connection: close\r\n\r\n"
            )
            for _ in range(5):
                conn.sendall(b"A")
                time.sleep(0.2)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "800"]
        rc, out, err = self.launch_client(args, comm_timeout=10)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertEqual(b"AAAAA", o),
        ))

    def test_drip_feed_exactly_at_timeout_boundary(self):
        """
        Data arrives every 400 ms with a 300 ms timeout.

        The gap exceeds the timeout, so the client must disconnect and reconnect.
        """
        call_count = {"n": 0}

        def handler(conn):
            call_count["n"] += 1
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                b"Connection: close\r\n\r\n"
            )
            if call_count["n"] == 1:
                conn.sendall(b"A")
                time.sleep(1.0)   # 1000 ms > 300 ms timeout
            else:
                conn.sendall(b"RECONNECTED")

        srv = MockServer(handler, reuse=True).start()
        self.addCleanup(srv.stop)
        args = ["-u", srv.url(), "-t", "300"]
        rc, out, err = self.launch_client(args, comm_timeout=10)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertGreaterEqual(call_count["n"], 2),
        ))

    # -------------------------------------------------------------------
    # Connection failure
    # -------------------------------------------------------------------

    def test_connection_refused_exits_one(self):
        """A refused connection is a critical failure and must return rc=1."""
        args = ["-u", refused_url(), "-t", "100"]
        rc, out, err = self.launch_client(args)
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_no_reconnect_on_connection_failure(self):
        """
        A failed initial connection must not trigger reconnect attempts.

        The client should terminate quickly rather than retry indefinitely.
        """
        args = ["-u", refused_url(), "-t", "5000"]
        start = time.monotonic()
        rc, out, err = self.launch_client(args, comm_timeout=5)
        elapsed = time.monotonic() - start
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(1, r),
            self.assertLess(elapsed, 2.0,
                            f"client took {elapsed:.2f}s — appears to retry after refused connection"),
        ))


# ===========================================================================
# 6. Timeout Timing Tests
# ===========================================================================

class TimeoutTimingTests(SikradioTestBase):
    """
    Test the timing and behavior of the -t timeout mechanism.

    Specification:
      - -t TIMEOUT: time limit for receiving subsequent data (ms, 100–100000)
      - Default timeout: 5000 ms
      - Minimum required timeout resolution: 500 ms
      - The timeout applies only while receiving data, not while connecting

    Measurement methodology:
      Each test measures the time from when the server stops sending data
      until the client disconnects or sends another request.
      The configured timing tolerance accounts for OS scheduling overhead.
    """

    TIMING_TOLERANCE_MS = 50  # Maximum allowed excess over -t

    def _measure_timeout_firing(self, timeout_ms):
        """
        Start a server that sends a 200 OK response and then goes silent.

        Measure the time from the end of the headers until the client
        disconnects, observed when recv() returns 0.
        Return the elapsed time in milliseconds.
        """

        timing = {}
        disconnect_event = threading.Event()

        def handler(conn):
            if "client_disconnected" in timing:
                return
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                b"Connection: close\r\n\r\n"
            )
            timing["headers_sent"] = time.monotonic()
            # Wait for the client to disconnect; recv() should return 0.
            conn.settimeout(timeout_ms / 1000.0 * 3)  # 3x timeout as a guard
            try:
                while True:
                    data = conn.recv(1)
                    if not data:
                        break
            except OSError:
                pass
            timing["client_disconnected"] = time.monotonic()
            disconnect_event.set()

        srv = MockServer(handler, reuse=True).start()
        self.addCleanup(srv.stop)

        args = ["-u", srv.url(), "-t", str(timeout_ms)]
        proc = self.launch_client_interactive(args)

        # Wait for the client to disconnect; the timeout should fire first.
        fired = disconnect_event.wait(timeout=timeout_ms / 1000.0 * 4 + 2.0)
        terminate_process(proc)
        try:
            proc.communicate(timeout=1)
        except subprocess.TimeoutExpired:
            pass

        srv.stop()

        if not fired:
            self.fail(f"Timeout {timeout_ms}ms never fired (server never saw disconnect)")

        elapsed_ms = (timing["client_disconnected"] - timing["headers_sent"]) * 1000
        return elapsed_ms

    def test_timeout_fires_within_tolerance(self):
        """
        Verify that a 1000 ms timeout fires within the configured tolerance.
        """
        timeout_ms = 1000
        elapsed = self._measure_timeout_firing(timeout_ms)

        args = ["-u", "mock", "-t", str(timeout_ms)]
        lower = timeout_ms - 100          # The timeout should not fire too early.
        upper = timeout_ms + self.TIMING_TOLERANCE_MS

        self.assertTestPasses(0, b"", b"", args, lambda r, o, e: (
            self.assertGreater(elapsed, lower,
                               f"Timeout fired too early: {elapsed:.0f}ms < {lower}ms"),
            self.assertLess(elapsed, upper,
                            f"Timeout fired too late: {elapsed:.0f}ms > {upper}ms"),
        ))

    def test_minimum_timeout_100ms_fires(self):
        """Verify that the minimum 100 ms timeout is actually enforced."""
        timeout_ms = 100
        elapsed = self._measure_timeout_firing(timeout_ms)
        upper = timeout_ms + self.TIMING_TOLERANCE_MS

        args = ["-u", "mock", "-t", str(timeout_ms)]
        self.assertTestPasses(0, b"", b"", args, lambda r, o, e:
        self.assertLess(elapsed, upper,
                        f"Minimum timeout {timeout_ms}ms fired too late: {elapsed:.0f}ms")
                              )

    def test_timeout_500ms_resolution(self):
        """
        Verify the required timeout resolution using a 500 ms timeout.
        """
        timeout_ms = 500
        elapsed = self._measure_timeout_firing(timeout_ms)
        upper = timeout_ms + self.TIMING_TOLERANCE_MS

        args = ["-u", "mock", "-t", str(timeout_ms)]
        self.assertTestPasses(0, b"", b"", args, lambda r, o, e:
        self.assertLess(elapsed, upper,
                        f"500ms timeout fired too late: {elapsed:.0f}ms > {upper}ms")
                              )

    def test_data_arriving_before_timeout_resets_it(self):
        """
        Data arriving regularly before the timeout must prevent it from firing.

        This verifies that the timeout is reset after each recv().
        """
        interval_ms  = 150   # Data arrives every 150 ms.
        timeout_ms   = 600   # Timeout is 600 ms and should be reset.
        num_chunks   = 6     # 6 * 150 ms = 900 ms total.
        payload_byte = b"D"

        def handler(conn):
            read_request(conn)
            conn.sendall(
                b"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\n"
                b"Connection: close\r\n\r\n"
            )
            for _ in range(num_chunks):
                conn.sendall(payload_byte)
                time.sleep(interval_ms / 1000.0)

        srv = MockServer(handler).start()
        self.addCleanup(srv.stop)

        args = ["-u", srv.url(), "-t", str(timeout_ms)]
        rc, out, err = self.launch_client(args, comm_timeout=10)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r,
                             "Timeout fired while data was arriving regularly (timeout not being reset)"),
            self.assertEqual(payload_byte * num_chunks, o,
                             f"Expected {num_chunks} bytes, got {len(o)}"),
        ))


# ===========================================================================
# 7. IPv6 Tests
# ===========================================================================

def _ipv6_available():
    """
    Return (available: bool, reason: str).

    Check that the kernel supports AF_INET6 and that ::1 is usable through
    a bind/connect loopback round-trip. Both are required for local tests.
    """
    try:
        s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    except OSError as e:
        return False, f"kernel has no AF_INET6 support: {e}"
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        s.bind(("::1", 0))
        s.listen(1)
        port = s.getsockname()[1]
        c = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        c.settimeout(1.0)
        c.connect(("::1", port))
        c.close()
        s.close()
        return True, "ok"
    except OSError as e:
        return False, f"::1 loopback unusable: {e}"


def _find_ipv6_hostname():
    """
    Return the first hostname that resolves to an IPv6 address, or None.

    Try 'ip6-localhost' first and then 'localhost'.
    """
    for name in ("ip6-localhost", "localhost"):
        try:
            results = socket.getaddrinfo(
                name, None, socket.AF_INET6, socket.SOCK_STREAM
            )
            if results:
                return name
        except socket.gaierror:
            pass
    return None


# Run IPv6 capability checks once at import time.
_IPV6_OK, _IPV6_REASON = _ipv6_available()
_IPV6_HOSTNAME = _find_ipv6_hostname() if _IPV6_OK else None


class MockServerV6:
    """
    Single-connection IPv6-only TCP server bound to ::1.

    Mirrors the interface of MockServer. IPV6_V6ONLY is enabled so the server
    cannot accidentally accept IPv4-mapped connections.
    """

    def __init__(self, handler):
        self.handler = handler
        self._sock   = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET,    socket.SO_REUSEADDR,  1)
        self._sock.setsockopt(socket.IPPROTO_IPV6,  socket.IPV6_V6ONLY,   1)
        self._sock.bind(("::1", 0))
        self._sock.listen(4)
        self._sock.settimeout(5.0)
        self.port    = self._sock.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._stop   = threading.Event()

    def start(self):
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        try:
            self._sock.close()
        except OSError:
            pass

    def _serve(self):
        try:
            conn, _ = self._sock.accept()
        except OSError:
            return
        try:
            self.handler(conn)
        except OSError:
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def url(self, path="/stream"):
        # RFC 2732 bracket notation for IPv6 literals in URLs.
        return f"http://[::1]:{self.port}{path}"

    def hostname_url(self, hostname, path="/stream"):
        """Build a URL using a hostname that resolves to ::1."""
        return f"http://{hostname}:{self.port}{path}"


class IPv6Tests(SikradioTestBase):
    """
    Test IPv6 protocol handling.

    The entire class is skipped when:
      - the kernel does not support AF_INET6;
      - the ::1 loopback address is unavailable.

    Tests that require a hostname resolving to an IPv6 address are skipped
    separately when neither standard hostname is available.
    """

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        if not _IPV6_OK:
            raise unittest.SkipTest(
                f"IPv6 not available on this host: {_IPV6_REASON}"
            )

    # -------------------------------------------------------------------
    # Internal helpers
    # -------------------------------------------------------------------

    def _require_v6_hostname(self):
        """Skip the test if no hostname resolves to an IPv6 address."""
        if _IPV6_HOSTNAME is None:
            self.skipTest(
                "No hostname resolves to IPv6 on this host "
                "(no '::1 ip6-localhost' or '::1 localhost' in /etc/hosts)"
            )
        return _IPV6_HOSTNAME

    # -------------------------------------------------------------------
    # Basic connectivity over IPv6
    # -------------------------------------------------------------------

    def test_ipv6_flag_connects_and_streams(self):
        """
        Verify that -6 forces an IPv6 connection and that audio is received.

        The server listens only on ::1 with IPV6_V6ONLY enabled, so an IPv4
        connection cannot satisfy this test.
        """
        payload = b"IPV6_AUDIO"

        def handler(conn):
            read_request(conn)
            http_200(conn, body=payload)

        srv = MockServerV6(handler).start()
        self.addCleanup(srv.stop)

        hostname = self._require_v6_hostname()
        args = ["-u", srv.hostname_url(hostname), "-6", "-t", "3000"]
        rc, out, err = self.launch_client(args)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r,
                             f"rc={r}, stderr={e.decode(errors='replace')!r}"),
            self.assertIn(payload, o),
        ))

    def test_ipv6_audio_bytes_not_corrupted(self):
        """All byte values from 0 to 255 must survive an IPv6 connection unchanged."""
        payload = bytes(range(256)) * 8   # 2 KiB

        def handler(conn):
            read_request(conn)
            http_200(conn, body=payload)

        srv = MockServerV6(handler).start()
        self.addCleanup(srv.stop)

        hostname = self._require_v6_hostname()
        args = ["-u", srv.hostname_url(hostname), "-6", "-t", "3000"]
        rc, out, err = self.launch_client(args)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertEqual(payload, o),
        ))

    def test_ipv6_server_close_exits_zero(self):
        """An EOF from the IPv6 server is a clean exit with rc=0."""
        def handler(conn):
            read_request(conn)
            http_200(conn, body=b"DONE")

        srv = MockServerV6(handler).start()
        self.addCleanup(srv.stop)

        hostname = self._require_v6_hostname()
        args = ["-u", srv.hostname_url(hostname), "-6", "-t", "2000"]
        rc, out, err = self.launch_client(args)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(0, r))

    # -------------------------------------------------------------------
    # getaddrinfo filtering
    # -------------------------------------------------------------------

    def test_ipv6_flag_shown_in_protocol_log(self):
        """
        Verify that -6 and -v1 produce an IPv6 address in the protocol log.

        The presence of an address such as [::1] confirms that the client
        selected an IPv6 address from getaddrinfo.
        """
        def handler(conn):
            read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServerV6(handler).start()
        self.addCleanup(srv.stop)

        hostname = self._require_v6_hostname()
        args = ["-u", srv.hostname_url(hostname), "-6", "-v1", "-t", "2000"]
        rc, out, err = self.launch_client(args)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertIn(b"::1", e,
                          f"Expected IPv6 address in protocol log, stderr={e.decode(errors='replace')!r}"),
        ))

    def test_ipv6_only_server_rejects_ipv4_flag(self):
        """
        An IPv6-only server must reject a client using -4.

        The IPv4 address family lookup cannot return the IPv6 server address,
        so the client must exit with rc=1.
        """
        def handler(conn):
            read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServerV6(handler).start()
        self.addCleanup(srv.stop)

        hostname = self._require_v6_hostname()
        args = ["-u", srv.hostname_url(hostname), "-4", "-t", "500"]
        rc, out, err = self.launch_client(args)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: self.assertEqual(1, r))

    def test_ipv6_getaddrinfo_uses_ipv6_family(self):
        """
        Verify on the server side that the connection uses AF_INET6.

        This confirms that -6 is passed to getaddrinfo as AF_INET6 rather
        than AF_UNSPEC.
        """
        peer = {}

        def handler(conn):
            peer["family"] = conn.family
            read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServerV6(handler).start()
        self.addCleanup(srv.stop)

        hostname = self._require_v6_hostname()
        args = ["-u", srv.hostname_url(hostname), "-6", "-t", "2000"]
        rc, out, err = self.launch_client(args)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertEqual(socket.AF_INET6, peer.get("family"),
                             "Server received connection on non-IPv6 socket"),
        ))

    # -------------------------------------------------------------------
    # Bare IPv6 address literal (RFC 2732 bracket notation)
    # -------------------------------------------------------------------

    def test_bare_ipv6_literal_bracket_notation(self):
        """
        Verify parsing of an IPv6 literal using RFC 2732 bracket notation.

        The URL has the form http://[::1]:PORT/. The parser must correctly
        separate the address from the port.
        """
        payload = b"LITERAL_IPV6_OK"

        def handler(conn):
            read_request(conn)
            http_200(conn, body=payload)

        srv = MockServerV6(handler).start()
        self.addCleanup(srv.stop)

        # srv.url() returns http://[::1]:PORT/stream.
        args = ["-u", srv.url(), "-6", "-t", "3000"]
        rc, out, err = self.launch_client(args)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r,
                             "parse_url must handle RFC 2732 [::1] bracket notation; "
                             f"stderr={e.decode(errors='replace')!r}"),
            self.assertIn(payload, o),
        ))

    def test_host_header_bracket_notation_for_ipv6_literal(self):
        """
        Verify that the Host header for an IPv6 literal uses [::1]:PORT format.

        The test checks the request received directly by the server.
        """
        received = {}

        def handler(conn):
            received["req"] = read_request(conn)
            http_200(conn, body=b"X")

        srv = MockServerV6(handler).start()
        self.addCleanup(srv.stop)

        args = ["-u", srv.url(), "-6", "-t", "2000"]
        rc, out, err = self.launch_client(args)

        expected_host = f"[::1]:{srv.port}"
        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(0, r),
            self.assertEqual(
                expected_host,
                header_value(received.get("req", ""), "Host"),
                f"Expected 'Host: {expected_host}', "
                f"got: {received.get('req', '')!r}",
            ),
        ))

    # -------------------------------------------------------------------
    # IPv6 connection failure
    # -------------------------------------------------------------------

    def test_ipv6_refused_connection_exits_one(self):
        """
        A connection to an unused ::1 port must be refused and return rc=1.

        This is the IPv6 equivalent of test_connection_refused_exits_one.
        """
        with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(("::1", 0))
            port = s.getsockname()[1]
        # The port is now free because the socket has been closed.

        hostname = self._require_v6_hostname()
        args = ["-u", f"http://{hostname}:{port}/", "-6", "-t", "200"]
        rc, out, err = self.launch_client(args)

        self.assertTestPasses(rc, out, err, args, lambda r, o, e: (
            self.assertEqual(1, r),
            self.assertNotEqual(-signal.SIGSEGV, r),
        ))


# ===========================================================================
# Runner
# ===========================================================================

if __name__ == "__main__":
    try:
        make = subprocess.run(["make"], check=True)

    except FileNotFoundError:
        print("make not found", file=sys.stderr)
        sys.exit(1)

    except subprocess.CalledProcessError as e:
        print(f"make failed: {e}", file=sys.stderr)
        sys.exit(e.returncode)

    unittest.main(verbosity=2)