"""Protocol 1 client. Uses only the Python standard library."""

import json
import os
from pathlib import Path
import signal
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid


class QAError(RuntimeError):
    pass


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


class Connection:
    def __init__(self, directory):
        self.directory = Path(directory)
        endpoint = json.loads((self.directory / "endpoint.json").read_text(encoding="utf-8"))
        if endpoint.get("protocol") != 1:
            raise QAError("Unsupported QA protocol")
        url = urllib.parse.urlsplit(endpoint["url"])
        if url.scheme != "http" or url.hostname != "127.0.0.1" or not url.port or url.path:
            raise QAError("QA endpoint must be an HTTP loopback address")
        self.url = endpoint["url"]
        self.token = (self.directory / "token").read_text(encoding="utf-8").strip()
        # Ignore HTTP_PROXY and desktop proxy settings for local control.
        self.http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(self, route, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.url + route, data=data, headers={
            "Authorization": "Bearer " + self.token, "Content-Type": "application/json"})
        try:
            with self.http.open(request, timeout=3) as response:
                result = json.load(response)
        except urllib.error.HTTPError as error:
            raise QAError(f"QA HTTP {error.code}: {error.read().decode(errors='replace')}") from error
        if result.get("evidence_lost") or result.get("gap"):
            raise QAError("QA evidence is incomplete (journal failure, overflow, or event cursor gap)")
        return result

    def status(self):
        return self.request("/status")

    def submit(self, operation, arguments=None, request_id=None):
        body = {"request_id": request_id or uuid.uuid4().hex,
                "operation": operation, "arguments": arguments or {}}
        # A lost HTTP reply must not turn a mutation into a second mutation.
        for attempt in range(3):
            try:
                return self.request("/command", body)
            except (urllib.error.URLError, TimeoutError):
                if attempt == 2:
                    raise
        raise AssertionError("unreachable")

    def operation(self, request_id):
        return self.request("/operation?id=" + urllib.parse.quote(request_id, safe=""))

    def command(self, operation, arguments=None, timeout=30, heartbeat=None):
        ack = self.submit(operation, arguments)
        deadline = time.monotonic() + timeout
        result = ack
        while result["state"] == "queued":
            if time.monotonic() >= deadline:
                raise QAError(f"{operation} timed out; request {ack['request_id']}")
            if heartbeat:
                heartbeat()
            time.sleep(0.1)
            result = self.operation(ack["request_id"])
        if result["state"] != "completed":
            raise QAError(f"{operation} failed: {result.get('result')}")
        return result


class Child:
    def __init__(self, role, directory, command, env):
        self.role = role
        self.directory = directory
        self.log = (directory / "process.log").open("wb")
        flags = {"start_new_session": True} if os.name != "nt" else {
            "creationflags": subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW}
        try:
            self.process = subprocess.Popen(command, cwd=directory / "bin", env=env,
                                            stdin=subprocess.DEVNULL, stdout=self.log,
                                            stderr=subprocess.STDOUT, **flags)
        except BaseException:
            self.log.close()
            raise
        self.connection = None
        self.forced = False

    def poll_connection(self):
        if self.process.poll() is not None:
            raise QAError(f"{self.role} exited before shutdown (code {self.process.returncode}); see {self.directory / 'process.log'}")
        if self.connection is None and (self.directory / "endpoint.json").is_file():
            try:
                self.connection = Connection(self.directory)
            except json.JSONDecodeError:
                return None  # Writer may still be finishing the discovery file.
        return self.connection.status() if self.connection else None

    def stop(self, timeout=15):
        if self.process.poll() is None and self.connection:
            try:
                self.connection.submit("quit")
            except (OSError, QAError):
                pass
        try:
            self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.forced = True
            if os.name == "nt":
                # Only this recorded child PID and its descendants.
                subprocess.run(["taskkill", "/PID", str(self.process.pid), "/T", "/F"],
                               stdout=self.log, stderr=subprocess.STDOUT, check=False)
            else:
                os.killpg(self.process.pid, signal.SIGTERM)
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(self.process.pid, signal.SIGKILL)
            self.process.wait(timeout=5)
        finally:
            self.log.close()
        return {"role": self.role, "pid": self.process.pid,
                "exit_code": self.process.returncode, "forced": self.forced}
