import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from shell_package_version import shell_version


class ShellVersionTests(unittest.TestCase):
    def test_ci_attempts_and_previous_release(self):
        first = shell_version(26, 5, 42, attempt=1)
        retry = shell_version(26, 5, 42, attempt=2)
        parts = lambda value: tuple(map(int, value.split(".")))
        self.assertGreater(parts(first), parts("26.5.41.0"))
        self.assertGreater(parts(retry), parts(first))

    def test_local_previews_increment_and_next_ci_supersedes_them(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "shell-version.json"
            first = shell_version(26, 5, 41, local_state=state)
            second = shell_version(26, 5, 41, local_state=state)
            parts = lambda value: tuple(map(int, value.split(".")))
            self.assertGreater(parts(second), parts(first))
            self.assertGreater(parts(first), parts(shell_version(26, 5, 41, attempt=1)))
            self.assertGreater(parts(shell_version(26, 5, 42, attempt=1)), parts(second))
            with self.assertRaises(ValueError):
                shell_version(26, 5, 40, local_state=state)

    def test_invalid_version_components(self):
        for args in ((26, 5, 65536, 1), (26, 5, 42, 0), (26, 5, 42, 32768)):
            with self.assertRaises(ValueError):
                shell_version(*args)


if __name__ == "__main__":
    unittest.main()
