"""Exercise release version selection in disposable, repository-local Git trees."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / ".github/bump_version.sh"


class VersionBumpTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "builds"
        scratch.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="version-test-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Version tests")
        self.git("config", "user.email", "version-tests@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.write("VERSION", "28.0.2\n")
        self.commit("ci: baseline [skip ci]")

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.repo, check=True, capture_output=True, text=True)

    def write(self, name, value):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)

    def commit(self, message):
        self.git("add", ".")
        self.git("commit", "-qm", message)

    def bump(self):
        subprocess.run(["bash", str(SCRIPT)], cwd=self.repo, check=True, capture_output=True, text=True)
        return (self.repo / "VERSION").read_text().strip()

    def test_no_changes(self):
        self.assertEqual(self.bump(), "28.0.2")

    def test_automatic_patch(self):
        self.write("README.md", "Documentation change")
        self.commit("Docs: Update")
        self.assertEqual(self.bump(), "28.0.3")

    def test_automatic_minor(self):
        self.write("code/framework/src/scripting/builtins/example.cpp", "change")
        self.commit("Scripting: Update")
        self.assertEqual(self.bump(), "28.1.0")

    def test_automatic_major(self):
        self.write("code/framework/src/networking/replication/example.cpp", "change")
        self.commit("Networking: Update")
        self.assertEqual(self.bump(), "29.0.0")

    def test_explicit_major_is_not_bumped_twice(self):
        self.write("code/framework/src/networking/replication/example.cpp", "change")
        self.write("VERSION", "29.0.0\n")
        self.commit("Networking: Protocol 29")
        self.assertEqual(self.bump(), "29.0.0")
        self.assertEqual(self.bump(), "29.0.0")

    def test_insufficient_explicit_bump_still_raises_protocol_major(self):
        self.write("code/framework/src/networking/replication/example.cpp", "change")
        self.write("VERSION", "28.1.0\n")
        self.commit("Networking: Update")
        self.assertEqual(self.bump(), "29.0.0")

    def test_changes_after_manual_release_use_the_new_baseline(self):
        self.write("code/framework/src/networking/replication/example.cpp", "change")
        self.write("VERSION", "29.0.0\n")
        self.commit("Networking: Protocol 29")
        self.git("tag", "v29.0.0")
        self.assertEqual(self.bump(), "29.0.0")
        self.write("README.md", "Documentation change")
        self.commit("Docs: Update")
        self.assertEqual(self.bump(), "29.0.1")


if __name__ == "__main__":
    unittest.main()
