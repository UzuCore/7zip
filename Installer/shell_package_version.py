"""Give CI attempts and local previews distinct, ordered MSIX versions."""
import argparse
import json
from pathlib import Path


def shell_version(major, minor, run_number, attempt=None, local_state=None):
    if any(not 0 <= value <= 65535 for value in (major, minor, run_number)):
        raise ValueError("MSIX version components must fit in 16 bits")
    if attempt is not None:
        if not 1 <= attempt < 32768:
            raise ValueError("CI attempt must be between 1 and 32767")
        revision = attempt
    else:
        if local_state is None:
            raise ValueError("Local builds require a persistent revision state path")
        local_state = Path(local_state)
        previous = json.loads(local_state.read_text()) if local_state.exists() else {}
        if previous.get("runNumber", 0) > run_number:
            raise ValueError("Local build number is older than the last local preview")
        revision = previous["revision"] + 1 if previous.get("runNumber") == run_number else 32768
        if revision > 65535:
            raise ValueError("Local revision range exhausted; use a newer CI build number")
        local_state.parent.mkdir(parents=True, exist_ok=True)
        local_state.write_text(json.dumps({"runNumber": run_number, "revision": revision}))
    return f"{major}.{minor}.{run_number}.{revision}"


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--major", type=int, required=True)
    p.add_argument("--minor", type=int, required=True)
    p.add_argument("--run-number", type=int, required=True)
    p.add_argument("--attempt", type=int)
    p.add_argument("--local-state", type=Path)
    a = p.parse_args()
    print(shell_version(a.major, a.minor, a.run_number, a.attempt, a.local_state))
