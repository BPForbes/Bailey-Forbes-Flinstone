#!/usr/bin/env python3
"""scripts/kq_pty_answer.py never echoes the passphrase it types.

The program here prompts and reads its answer with the terminal's echo left as it found it, as a
program that has not turned echo off yet would. The answer must reach the program and must not
appear in the captured output. Usage: test_kq_pty_answer.py [ANSWERER] (default: the repo's script).
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    answerer = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "scripts", "kq_pty_answer.py")
    secret = "pty-answer-" + os.urandom(12).hex()
    with tempfile.TemporaryDirectory() as tmp:
        pass_file = os.path.join(tmp, "test.pass")
        fd = os.open(pass_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(secret + "\n")
        prog = ['/bin/sh', '-c', 'printf "Passphrase: "; read -r x; printf "\\nread %s bytes\\n" "${#x}"']
        res = subprocess.run([sys.executable, "-I", answerer, pass_file] + prog,
                             capture_output=True, timeout=30, check=False)
    out = res.stdout.decode("utf-8", "replace")
    failures = []
    if res.returncode != 0:
        failures.append(f"exit status {res.returncode}")
    if f"read {len(secret)} bytes" not in out:
        failures.append("the program did not read the answer")
    if secret in out:
        failures.append("the passphrase was echoed into the output")
    if failures:
        print("test_kq_pty_answer: FAILED: " + "; ".join(failures), file=sys.stderr)
        return 1
    print("test_kq_pty_answer: OK (answer read, not echoed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
