# dependencies

A small Linux ELF binary that installs a fixed set of system packages and shared libraries — automatically detecting whichever package manager your distro ships with. Just run it, and the dependencies it bakes in get installed.

No flags to learn, no manifest to write, no "which package manager are you on" prompts. Drop the binary on a fresh box, run it, done.

## Table of contents

- [What it does](#what-it-does)
- [Supported package managers](#supported-package-managers)
- [Installation](#installation)
- [Usage](#usage)
- [How it works](#how-it-works)
- [Exit codes](#exit-codes)
- [Troubleshooting](#troubleshooting)
- [FAQ](#faq)
- [License](#license)

## What it does

When you run `dependencies`, it:

1. Detects your Linux distribution's package manager (`apt`, `dnf`, `pacman`, `zypper`, `apk`, …).
2. Iterates through its built-in list of required packages and shared libraries.
3. Skips anything already installed.
4. Installs everything that's missing in a single batched call to your package manager.

The list of dependencies is hardcoded into the binary at build time — there's no config file to edit and nothing to pass on the command line. If you need a different set, rebuild from source with your own list and ship that binary.

## Supported package managers

| Distro family | Package manager | Status |
| --- | --- | --- |
| Debian, Ubuntu, Mint, Pop!_OS | `apt` | Supported |
| Fedora, RHEL, Rocky, Alma | `dnf` / `yum` | Supported |
| Arch, Manjaro, EndeavourOS | `pacman` | Supported |
| openSUSE | `zypper` | Supported |
| Alpine | `apk` | Supported |

Detection is automatic; you don't tell it which one to use.

## Installation

This repository ships the compiled binary directly. Pick whichever fetch method fits your environment, then mark it executable and run it.

### Option 1: `git clone`

Best if you want the full repo (source, README, history) alongside the binary:

```bash
git clone https://github.com/HothIndustries/dependencies.git
cd dependencies
chmod +x dependencies
./dependencies
```

For a faster, history-less clone:

```bash
git clone --depth 1 https://github.com/HothIndustries/dependencies.git
```

### Option 2: `curl` / `wget` from `raw.githubusercontent.com`

Best for one-liners on a fresh box where you only want the binary itself:

```bash
curl -fsSLO https://raw.githubusercontent.com/HothIndustries/dependencies/main/dependencies
chmod +x dependencies
./dependencies
```

Or with `wget`:

```bash
wget https://raw.githubusercontent.com/HothIndustries/dependencies/main/dependencies
chmod +x dependencies
./dependencies
```

To pin to a specific commit instead of `main`, swap `main` for the commit SHA:

```bash
curl -fsSLO https://raw.githubusercontent.com/HothIndustries/dependencies/e3e4ad1/dependencies
```

### Option 3: GitHub REST API

Best when you're scripting against GitHub (e.g. you already have a token in `$GITHUB_TOKEN` and want to stay within the API). The `contents` endpoint can return the raw bytes when you set the `Accept` header:

```bash
curl -fsSL \
  -H "Accept: application/vnd.github.raw" \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  -o dependencies \
  https://api.github.com/repos/HothIndustries/dependencies/contents/dependencies
chmod +x dependencies
./dependencies
```

Pin to a ref (branch, tag, or SHA) with the `?ref=` query parameter:

```bash
curl -fsSL \
  -H "Accept: application/vnd.github.raw" \
  -o dependencies \
  "https://api.github.com/repos/HothIndustries/dependencies/contents/dependencies?ref=e3e4ad1"
```

The `Authorization` header is optional for public repos but lifts you from the 60/hr unauthenticated rate limit to 5,000/hr.

### Putting it on your `PATH`

However you fetched it, if you'd rather have it available anywhere:

```bash
chmod +x dependencies
sudo mv dependencies /usr/local/bin/
dependencies
```

## Usage

```bash
./dependencies
```

That's it. There are no flags.

### Sample output

```
[*] Detected package manager: apt
[*] Checking 12 required dependencies...
[✓] build-essential — already installed
[✓] libssl3        — already installed
[!] libcurl4       — missing
[!] zlib1g         — missing
[!] libsqlite3-0   — missing
[*] Installing 3 packages...
[+] Done. All dependencies satisfied.
```

## How it works

Under the hood, the binary does roughly this:

1. **Detect the package manager.** Walks `PATH` looking for `apt-get`, `dnf`, `yum`, `pacman`, `zypper`, and `apk` — in that order — and picks the first one it finds. Also checks `/etc/os-release` as a cross-reference.
2. **Check what's already installed.** Queries the package manager (`dpkg -s`, `rpm -q`, `pacman -Q`, etc.) for each item in the built-in list, so already-installed packages are skipped.
3. **Install what's missing.** Hands the remaining list to the package manager in one batched, non-interactive call so you get a single transaction rather than one prompt per package.

Shared libraries are installed via the package that ships them — the binary doesn't drop `.so` files directly into `/usr/lib`. That keeps everything tracked by your distro's package database so it can be cleanly updated or removed later.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success — every dependency is satisfied. |
| `1` | Generic error. |
| `4` | No supported package manager was detected. |
| `6` | Package manager exited non-zero (install failed). |
| `7` | Permission denied. |

These are stable; feel free to script against them.

## Troubleshooting

### "command not found: dependencies"

The binary isn't on your `PATH`. Run it with an explicit path:

```bash
./dependencies
```

Or install it system-wide:

```bash
sudo mv dependencies /usr/local/bin/
```

### "permission denied"

Double-check that the file is executable:

```bash
chmod +x dependencies
```

### "no supported package manager detected"

You're on a distro outside the supported list, or you're inside a stripped-down container that doesn't ship one. Install the package manager binary first, then re-run.

### "cannot execute binary file: Exec format error"

The binary was built for a different CPU architecture than yours (most likely you're on ARM and the binary is x86_64, or vice versa). You'll need to rebuild from source for your architecture.

### Package manager prompts hang the binary

The binary runs the package manager non-interactively, but some package managers will still prompt under specific conditions (e.g. a held dependency conflict on `apt`). If that happens, run your package manager manually to clear whatever it's waiting on, then re-run `dependencies`.

### A package failed to install

Run your distro's update command first — the binary doesn't refresh the package index for you:

```bash
sudo apt update        # Debian/Ubuntu
sudo dnf check-update  # Fedora/RHEL
sudo pacman -Sy        # Arch
```

Then try again.

## FAQ

**Can I change the list of dependencies it installs?**
Not without rebuilding. The list is hardcoded into the binary at compile time. Edit the source, rebuild with `make`, and ship the new binary.

**Why hardcode the list instead of reading a config file?**
Predictability. The binary is a self-contained "set up this machine" artifact you can drop on any supported distro and run without dragging along extra files.

**Does it uninstall or update anything?**
No. It only installs what's missing. Already-installed packages are left alone — even if a newer version is available.

**Does it work in a Dockerfile?**
Yes. It's designed to run non-interactively, so it works well as a single `RUN` step. Make sure your base image has its package index refreshed first (`apt-get update`, `dnf makecache`, etc.).

**Why a C binary instead of a shell script?**
Speed, no shell-quoting surprises, no dependency on Bash/Python/etc. being present, and one file to ship.

**Does it support 32-bit systems / non-x86 architectures?**
Whatever the Makefile builds for. The repo ships one prebuilt binary; for other architectures you'll need to rebuild from source.

## License

MIT. See [LICENSE](LICENSE) for the full text.
