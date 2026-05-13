# dependencies

A small Linux ELF binary that installs a fixed set of system packages and shared libraries — automatically detecting whichever package manager your distro ships with. Just run it, and the dependencies it bakes in get installed.

No flags to learn, no manifest to write, no "which package manager are you on" prompts. Drop the binary on a fresh box, run it, done.

> ⚠️ **Read this before you run it.** This binary modifies your system. It detects your package manager and installs software through it. Treat it the same way you'd treat a `curl … | sh` one-liner: take a moment to understand what it does, verify what you downloaded, and prefer to try it in a throwaway environment first. The [Before you run it](#before-you-run-it) and [Verifying the binary](#verifying-the-binary) sections walk through what "verify" actually means in practice.

## Table of contents

- [What it does](#what-it-does)
- [Design goals](#design-goals)
- [Supported package managers](#supported-package-managers)
- [Before you run it](#before-you-run-it)
- [Installation](#installation)
  - [Option 1: `git clone`](#option-1-git-clone)
  - [Option 2: `curl` / `wget` from `raw.githubusercontent.com`](#option-2-curl--wget-from-rawgithubusercontentcom)
  - [Option 3: GitHub REST API](#option-3-github-rest-api)
  - [Putting it on your `PATH`](#putting-it-on-your-path)
- [Verifying the binary](#verifying-the-binary)
  - [Checksums](#checksums)
  - [Reproducible builds](#reproducible-builds)
  - [Reading the source first](#reading-the-source-first)
- [Usage](#usage)
- [Running it safely the first time](#running-it-safely-the-first-time)
  - [In a virtual machine](#in-a-virtual-machine)
  - [In a container](#in-a-container)
  - [In a dry-run](#in-a-dry-run)
- [How it works](#how-it-works)
- [Security model](#security-model)
  - [Trust boundaries](#trust-boundaries)
  - [What is in scope](#what-is-in-scope)
  - [What is out of scope](#what-is-out-of-scope)
- [Privilege and permissions](#privilege-and-permissions)
- [Rollback and recovery](#rollback-and-recovery)
- [Network and offline behavior](#network-and-offline-behavior)
- [Logging and auditability](#logging-and-auditability)
- [Exit codes](#exit-codes)
- [Troubleshooting](#troubleshooting)
- [FAQ](#faq)
- [Reporting security issues](#reporting-security-issues)
- [Contributing](#contributing)
- [License](#license)

## What it does

When you run `dependencies`, it:

1. Detects your Linux distribution's package manager (`apt`, `dnf`, `pacman`, `zypper`, `apk`, …).
2. Iterates through its built-in list of required packages and shared libraries.
3. Skips anything already installed, so it never re-installs a package you already have.
4. Installs everything that's missing in a single batched, non-interactive call to your package manager.
5. Exits with a status code that tells you whether everything succeeded, partially succeeded, or failed.

The list of dependencies is hardcoded into the binary at build time — there's no config file to edit and nothing to pass on the command line. If you need a different set, rebuild from source with your own list and ship that binary.

That is the whole feature surface. No daemon, no background process, no persistent state, no scheduled re-runs. It does one thing on demand and exits.

## Design goals

The tool is intentionally small. The design priorities, in order, are:

1. **Predictability.** Two runs on identical machines produce identical changes. There is no version drift, no template expansion, no opportunistic resolution.
2. **Auditability.** Every action is observable on stdout/stderr and in your distro's package manager logs. There is no hidden behavior.
3. **Minimal trust footprint.** The binary shells out to your existing package manager rather than fetching `.deb`, `.rpm`, or `.tar.gz` files directly. Your distro keeps doing signature checks and dependency resolution.
4. **No moving parts.** No network calls of its own, no telemetry, no auto-update, no plugins. The only things it talks to are your package manager and the local filesystem.
5. **Easy to throw away.** It writes no state outside of what your package manager records, so removing it is `rm dependencies` and (optionally) uninstalling the packages it added.

If any of those design goals conflict with a feature you want, the feature loses. That is the whole point of the tool.

## Supported package managers

| Distro family | Package manager | Status |
| --- | --- | --- |
| Debian, Ubuntu, Mint, Pop!_OS | `apt` | Supported |
| Fedora, RHEL, Rocky, Alma | `dnf` / `yum` | Supported |
| Arch, Manjaro, EndeavourOS | `pacman` | Supported |
| openSUSE | `zypper` | Supported |
| Alpine | `apk` | Supported |

Detection is automatic; you don't tell it which one to use. If you happen to have multiple package managers installed on the same host (uncommon but not impossible — e.g. `apk` inside an Alpine chroot on a Debian host), the binary picks the first one it finds in `PATH` in the order listed above and uses that one for the whole run. It will never mix package managers within a single invocation.

If none of the supported managers are present, the binary exits cleanly with status `4` and changes nothing on disk.

## Before you run it

Anything that installs software on your system deserves a moment of scrutiny. Even a small, well-intentioned tool can leave you with surprises if you run it blindly. Here is a short checklist before you execute the binary on a machine that matters:

1. **Know where the binary came from.** If you fetched it via `curl` or the GitHub API, confirm you used the official URL (`github.com/HothIndustries/dependencies`). Typo-squatted forks happen. Compare the URL in your shell history against the one in this README.
2. **Verify the file's integrity.** See [Verifying the binary](#verifying-the-binary) for the exact commands. At minimum, compare a SHA-256 against the one published with the release you intended to download.
3. **Read the list of packages it will install.** The list is hardcoded into the binary, but you can extract a human-readable list from a built binary with `strings dependencies | grep -iE '^(lib|build|zlib|libssl|libcurl|libsqlite|…)'`, or — more reliably — clone the source and read it directly. If you're not comfortable with the list, do not run the binary.
4. **Try it in a throwaway environment first.** A VM, a Docker container, or a fresh cloud instance. See [Running it safely the first time](#running-it-safely-the-first-time).
5. **Have a rollback plan.** Know which packages will be added so you can remove them later. See [Rollback and recovery](#rollback-and-recovery).
6. **Avoid running on production hosts the first time.** Even if the binary is correct, your distro's package index may be stale, a mirror may be flaky, or a transitive dependency upgrade may surprise you. The first run should always be on a non-critical machine.
7. **Check the binary's architecture matches yours.** `file dependencies` will tell you. Running an x86_64 binary on an ARM box (or vice versa) is a noisy failure — but a binary mismatched to your libc version can be a quieter, more confusing one.

None of this is about distrusting this specific tool. It's the baseline hygiene you'd want for any binary that calls `apt install` on your behalf.

## Installation

This repository ships the compiled binary directly. Pick whichever fetch method fits your environment, then mark it executable and run it.

### Option 1: `git clone`

Best if you want the full repo (source, README, history) alongside the binary. This is the most auditable option: you can read every file, including the source, before running anything.

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

A shallow clone gives you the current state of `main` and skips history. That is fine for running, but if you ever need to bisect a regression or verify a historical signed tag, you'll want a full clone instead.

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

To pin to a specific commit instead of `main`, swap `main` for the commit SHA. Pinning a SHA is strongly recommended for production scripts — `main` is a moving target and can change under your feet:

```bash
curl -fsSLO https://raw.githubusercontent.com/HothIndustries/dependencies/e3e4ad1/dependencies
```

> Note: piping directly into a shell (`curl … | sh`) is a common pattern but a poor habit. It hides what you're running and gives you no chance to inspect the artifact before it executes. The snippets above intentionally download to disk in a separate step so you can run `file`, `sha256sum`, and `strings` against it before flipping the execute bit.

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

The `Authorization` header is optional for public repos but lifts you from the 60/hr unauthenticated rate limit to 5,000/hr. If you do supply a token, scope it as narrowly as possible — a token with `repo:read` on a single public repository is plenty. Never script with a token that has broader permissions than the task needs.

### Putting it on your `PATH`

However you fetched it, if you'd rather have it available anywhere:

```bash
chmod +x dependencies
sudo mv dependencies /usr/local/bin/
dependencies
```

`/usr/local/bin` is the conventional location for locally-installed binaries on most distros. If your organization manages `/usr/local` via configuration management, consider dropping it in `$HOME/bin` and prepending that to your `PATH` instead.

## Verifying the binary

A signature or checksum is only useful if you compare it to one you obtained through a different channel than the binary itself. Downloading both from the same compromised mirror buys you nothing. The recommendations below assume you treat the published checksum/signature as the trusted reference and the downloaded binary as the artifact you're verifying.

### Checksums

Compute the SHA-256 of whatever you downloaded:

```bash
sha256sum dependencies
```

Compare the output to the SHA-256 published alongside the release you intended to grab. If the values differ — even by one character — stop. Do not run the binary. Re-download from a different network or a different mirror and recompute. A mismatch can mean a truncated download, a man-in-the-middle, or a typo'd URL; none of those are conditions you want to debug by running the binary.

If you're scripting this, fail closed on mismatch:

```bash
EXPECTED="…paste the published SHA-256 here…"
GOT=$(sha256sum dependencies | awk '{print $1}')
if [ "$EXPECTED" != "$GOT" ]; then
  echo "checksum mismatch: refusing to run" >&2
  exit 1
fi
```

### Reproducible builds

The Makefile aims to produce byte-identical output across builds with the same toolchain and source tree. If you want strong assurance that the published binary corresponds to the public source tree, build it yourself and compare the resulting hashes:

```bash
git clone https://github.com/HothIndustries/dependencies.git
cd dependencies
make clean && make
sha256sum dependencies
```

If your locally-built binary's SHA-256 matches the published one, you've verified that the binary in the repository was built from the source in the repository. That's not the same as auditing the source — but it eliminates the "the binary was built from something other than what's published" class of supply-chain attack.

Reproducibility can be fragile across distro versions, compiler versions, and even filesystem inode order. If your local build doesn't match exactly, the cause is more often a build-environment difference than a tampered upstream — but it still warrants investigation before you trust the binary.

### Reading the source first

The source is small. Reading it end-to-end takes minutes, not hours, and is the most reliable form of verification. Things worth focusing on as you read:

- The exact list of packages that gets installed.
- The exact command-line arguments passed to your package manager. In particular, look for any flags that disable signature verification (`--allow-unauthenticated`, `--nogpgcheck`, `--insecure`, etc.). There should be none.
- Whether the binary makes any network calls of its own. It shouldn't — every network call should be the package manager's responsibility.
- Whether the binary touches any path outside of what the package manager touches. It shouldn't.

If you find anything in the source that contradicts what this README says, the source is authoritative — and please file an issue. Documentation can drift; the code is the truth.

## Usage

```bash
./dependencies
```

That's it. There are no flags.

The binary writes its progress to stdout and any errors to stderr, then exits. It does not fork, it does not background itself, and it does not leave anything running.

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

A few things to notice in that output:

- Each package is enumerated and labelled `[✓]` or `[!]` *before* anything is installed. If the list ever surprises you, you can Ctrl-C the run at this point and nothing will have been changed.
- The line `[*] Installing 3 packages...` precedes the actual install. The corresponding package manager output follows it directly so you see the same messages you'd see if you ran the install command yourself.
- The final `[+] Done.` only appears on a clean exit. If you don't see it, the run did not complete cleanly — check stderr and the exit code.

## Running it safely the first time

The single best thing you can do before running an unfamiliar binary on a host that matters is to run it on a host that doesn't. The cost is low and the upside is high: if the binary does something unexpected, you can throw the test environment away.

### In a virtual machine

A snapshot-capable VM (libvirt + qcow2, VMware, VirtualBox, UTM, etc.) is the gold standard. Take a snapshot of a clean install of your distro, run the binary, observe the result, and either commit or roll back. If anything is off, roll back and you're at a known-clean state in seconds.

### In a container

Containers are faster to spin up than full VMs and are usually sufficient for testing this kind of tool. The container's package manager is real, and the install commands are the same commands they'd be on the host — but the changes are contained to the image.

For example, on a Debian host:

```bash
docker run --rm -it -v "$PWD/dependencies:/usr/local/bin/dependencies:ro" debian:stable bash -c '
  apt-get update &&
  dependencies
'
```

Or on Alpine:

```bash
docker run --rm -it -v "$PWD/dependencies:/usr/local/bin/dependencies:ro" alpine:latest sh -c '
  apk update &&
  dependencies
'
```

Mounting the binary read-only (`:ro`) prevents the container from modifying it on the host, which is worth doing on principle even though the binary doesn't try.

### In a dry-run

The binary does not currently have a built-in `--dry-run` flag — see the [FAQ](#faq) for why. But you can approximate one by reading the source, extracting the package list, and running your package manager's native dry-run mode against it. For example, with `apt`:

```bash
sudo apt-get -s install build-essential libssl3 libcurl4 zlib1g libsqlite3-0
```

The `-s` flag simulates without making changes. `dnf` has `--assumeno`; `zypper` has `--dry-run`; `pacman` has `-p` (print).

This isn't as reliable as the binary's own logic, but it gives you a high-confidence preview of what changes are about to be made to your system.

## How it works

Under the hood, the binary does roughly this:

1. **Detect the package manager.** Walks `PATH` looking for `apt-get`, `dnf`, `yum`, `pacman`, `zypper`, and `apk` — in that order — and picks the first one it finds. Also checks `/etc/os-release` as a cross-reference. The cross-reference exists so that, for example, a chroot on a Debian host that happens to have `apk` in its `PATH` is recognized correctly.
2. **Check what's already installed.** Queries the package manager (`dpkg -s`, `rpm -q`, `pacman -Q`, etc.) for each item in the built-in list, so already-installed packages are skipped. The query is read-only and changes nothing on disk.
3. **Decide what to install.** Builds the list of missing packages. If the list is empty, the binary skips straight to a clean exit — your machine was already in the desired state.
4. **Install what's missing.** Hands the remaining list to the package manager in one batched, non-interactive call so you get a single transaction rather than one prompt per package. The non-interactive flag is the package manager's own (e.g. `apt-get -y`, `dnf -y`, `pacman --noconfirm`), not a bypass of any safety check.
5. **Surface the result.** Streams the package manager's stdout/stderr to your terminal so you see exactly the same output you'd see if you'd run the install command yourself. Then it exits with a code that reflects the outcome.

Shared libraries are installed via the package that ships them — the binary doesn't drop `.so` files directly into `/usr/lib`. That keeps everything tracked by your distro's package database so it can be cleanly updated or removed later. It also means that signature verification, dependency resolution, and conflict checking all stay where they belong: in the package manager you already trust.

## Security model

It's worth being explicit about what this tool does and does not guarantee, so you can decide whether the trust assumptions match your environment.

### Trust boundaries

The binary trusts:

- **Your package manager.** Whatever `apt`, `dnf`, `pacman`, etc. installs is what ends up on disk. The binary does not second-guess them, does not bypass their signature checks, and does not pre-fetch packages from elsewhere.
- **Your distro's package repositories.** The binary doesn't ship its own mirror or override your sources list.
- **The integrity of the binary itself.** Once you run it, you are trusting the binary; that's what [Verifying the binary](#verifying-the-binary) is for.

The binary does *not* trust user input, because there is no user input — there are no flags, no config file, no environment variables it reads.

### What is in scope

- Correct detection of the package manager.
- Correct enumeration of already-installed packages.
- Correct invocation of the install command with the intended package list and no other flags.
- Honest exit codes.
- Honest stdout/stderr that matches what the package manager actually emitted.

### What is out of scope

- The contents of your distro's repositories. If your distro ships a compromised package, this tool installs the compromised package.
- Whether the packages in the built-in list are themselves "safe" to install on your host. That's a judgment call about your environment, not something a tool can answer.
- Repository signing key management. Use your distro's tools for that (`apt-key`, `rpm --import`, etc.).
- Anything that happens after the binary exits.
- Hardening the host. This tool installs software; it does not configure firewalls, SELinux profiles, AppArmor profiles, or kernel hardening flags.

## Privilege and permissions

Installing system packages is a privileged operation on every supported distro. The binary itself doesn't need to be setuid or owned by root — what it needs is the ability to invoke your package manager with sufficient privileges to install. How that privilege is conferred is your environment's choice:

- **Sudoers entry.** Run the binary as a normal user, with a sudoers rule that allows that user to invoke the package manager without a password prompt. This is a common pattern for CI and configuration management.
- **Root login.** Inside containers and minimal VMs you're often already root, so no elevation is needed.
- **Setuid wrapper.** Not recommended. Setuid binaries have a long history of subtle escalation bugs, and there is no reason to take on that risk for a tool with this scope.

Whichever path you choose, the principle is the same: grant the binary only the privileges it needs to invoke the package manager, and grant the package manager only the privileges it needs to install. Avoid running unrelated workloads in the same elevated context.

If the binary is invoked without enough privilege to install packages, the underlying package manager will fail and the binary will exit with code `7`. It will not attempt to elevate privileges on its own.

## Rollback and recovery

Because every install happens through your package manager, every install can be reversed with that same package manager. The binary writes nothing outside the package manager's view of the system, so there are no orphan files to clean up.

To undo a run, use your distro's standard removal command against the packages that were newly installed. For example, on Debian/Ubuntu:

```bash
sudo apt-get remove --auto-remove build-essential libcurl4 zlib1g libsqlite3-0
```

The `--auto-remove` flag also removes any dependencies that were only pulled in to satisfy the packages you're removing. On `dnf`-based systems the equivalent is `sudo dnf remove`, which is auto-remove-aware by default; on `pacman`, `sudo pacman -Rs` removes a package and its no-longer-needed dependencies.

Your distro keeps a transaction log that makes this even easier:

- Debian/Ubuntu: `/var/log/apt/history.log` records every transaction with timestamps and package lists.
- Fedora/RHEL: `dnf history` lists every transaction; `dnf history undo <id>` reverses one.
- Arch: `/var/log/pacman.log` records every install/remove.
- openSUSE: `zypper history`.
- Alpine: `/var/log/apk.log`.

If you ran the binary by mistake, look up the most recent transaction in the relevant log and remove exactly that set of packages. There is no separate state that the binary needs to clean up.

## Network and offline behavior

The binary makes no network calls of its own. The only network traffic associated with a run comes from your package manager fetching packages — and only if the install step requires it.

In practice:

- If every package is already installed, the run finishes without touching the network at all.
- If packages are missing, the network traffic is whatever your package manager would generate for an equivalent install command. The traffic goes to the mirrors configured in your `sources.list`, `/etc/yum.repos.d/`, `/etc/pacman.d/mirrorlist`, etc. — not to anywhere this tool chose.
- The binary itself does not phone home, does not check for updates, does not download a manifest, and does not emit telemetry.

If you're operating in an air-gapped environment, mirror your distro's repositories locally and point the package manager at them. The binary will work transparently because it never reaches around the package manager.

## Logging and auditability

Two layers of logs cover a run:

1. **The binary's own output.** Each detected package, each install decision, and the final result are written to stdout. Errors go to stderr. There is no separate log file — what you see in your terminal is the entire trace.
2. **Your package manager's logs.** Every install operation is recorded in the distro's package manager history (see [Rollback and recovery](#rollback-and-recovery) for the exact paths). Those logs are durable and survive reboots.

To capture both streams to a file for later review:

```bash
./dependencies 2>&1 | tee dependencies-$(date +%F-%H%M%S).log
```

For CI environments where you want a structured artifact, redirecting the same output to a build artifact directory is the simplest approach.

The binary writes nothing to `/var/log`, nothing to `/tmp`, nothing to `~/.config`, and nothing to `/etc`. If you ever find a file there that looks like it was written by this tool, it isn't — investigate.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success — every dependency is satisfied. |
| `1` | Generic error. |
| `2` | Invalid invocation (e.g. unexpected arguments were passed). |
| `4` | No supported package manager was detected. |
| `5` | Package query failed — the binary could not determine which packages are installed. |
| `6` | Package manager exited non-zero (install failed). |
| `7` | Permission denied. |

These are stable; feel free to script against them. A non-zero exit is always a real failure — the binary does not exit zero on partial success.

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

Two distinct causes share this message:

- **The file isn't executable.** Fix with `chmod +x dependencies`.
- **The package manager can't be invoked with enough privilege to install.** Your environment needs to grant the binary or its parent process the right to call the package manager. See [Privilege and permissions](#privilege-and-permissions).

If you're already root inside a container and still see this, the cause is almost always the missing execute bit.

### "no supported package manager detected"

You're on a distro outside the supported list, or you're inside a stripped-down container that doesn't ship one. Install the package manager binary first, then re-run.

If you're certain your package manager is supported but the binary doesn't see it, check that the manager is in `PATH` for the user invoking the binary (and not just for root, or vice versa).

### "cannot execute binary file: Exec format error"

The binary was built for a different CPU architecture than yours (most likely you're on ARM and the binary is x86_64, or vice versa). Confirm with:

```bash
file dependencies
uname -m
```

If the two don't match, you'll need to rebuild from source for your architecture.

### Package manager prompts hang the binary

The binary runs the package manager non-interactively, but some package managers will still prompt under specific conditions (e.g. a held dependency conflict on `apt`, or an unsigned-repository confirmation on `pacman`). If that happens, run your package manager manually to clear whatever it's waiting on, then re-run `dependencies`.

If you're scripting around this, you can capture stdin in advance:

```bash
./dependencies < /dev/null
```

That ensures the binary fails fast on any unexpected prompt rather than hanging indefinitely.

### A package failed to install

Run your distro's update command first — the binary doesn't refresh the package index for you:

```bash
sudo apt update        # Debian/Ubuntu
sudo dnf check-update  # Fedora/RHEL
sudo pacman -Sy        # Arch
sudo zypper refresh    # openSUSE
sudo apk update        # Alpine
```

Then try again. If it still fails, the failure is now your package manager's failure (not this tool's), and the package manager's error message is the right place to start debugging.

### The binary exits 0 but something I expected isn't installed

The most common cause is that the package you expected wasn't on the binary's built-in list — different builds can have different lists. To inspect what a particular build will install, read the source for that commit, or extract the strings from the binary:

```bash
strings dependencies | sort -u | less
```

Package names are a subset of those strings. Skim for the package families you expected (`libssl`, `libcurl`, `libsqlite`, etc.).

### The binary made changes I didn't expect

Check the package manager's transaction log (see [Rollback and recovery](#rollback-and-recovery)) for the exact list. Then file an issue with that list, the distro you're on, and the SHA-256 of the binary you ran. The combination of those three pieces of information is enough to reproduce a run.

## FAQ

**Can I change the list of dependencies it installs?**
Not without rebuilding. The list is hardcoded into the binary at compile time. Edit the source, rebuild with `make`, and ship the new binary. The lack of a config file is deliberate — see [Design goals](#design-goals).

**Why hardcode the list instead of reading a config file?**
Predictability. The binary is a self-contained "set up this machine" artifact you can drop on any supported distro and run without dragging along extra files. A config file would introduce a second artifact, a second place for bugs to hide, and a second thing to keep in sync.

**Why is there no `--dry-run` flag?**
Because your package manager already has one, and the binary's whole job is to delegate to your package manager. Adding a parallel dry-run path inside the binary would duplicate logic and create a real risk that the dry-run output disagrees with the real run. See [In a dry-run](#in-a-dry-run) for the recommended pattern.

**Does it uninstall or update anything?**
No. It only installs what's missing. Already-installed packages are left alone — even if a newer version is available. Updating is your package manager's job, not this tool's.

**Does it work in a Dockerfile?**
Yes. It's designed to run non-interactively, so it works well as a single `RUN` step. Make sure your base image has its package index refreshed first (`apt-get update`, `dnf makecache`, etc.). A typical use looks like:

```dockerfile
COPY dependencies /usr/local/bin/dependencies
RUN chmod +x /usr/local/bin/dependencies \
 && apt-get update \
 && dependencies \
 && rm -rf /var/lib/apt/lists/*
```

**Why a C binary instead of a shell script?**
Speed, no shell-quoting surprises, no dependency on Bash/Python/etc. being present, and one file to ship. A static binary also has fewer "did the interpreter behave the same way on this distro" failure modes than a script.

**Does it support 32-bit systems / non-x86 architectures?**
Whatever the Makefile builds for. The repo ships one prebuilt binary; for other architectures you'll need to rebuild from source.

**Does it phone home or send any telemetry?**
No. It makes no network calls of its own. The only network traffic associated with a run is whatever your package manager generates while installing the missing packages.

**Is there an auto-update mechanism?**
No, and there will not be one. An installer that updates itself is a much larger attack surface than one that doesn't. To update, replace the binary the same way you installed it.

**Can it install packages from third-party repositories?**
Only if those repositories are already configured on the host. The binary doesn't add repositories, doesn't import signing keys, and doesn't change `sources.list`.

**What happens if the package list contains a package my distro doesn't have?**
The package manager will fail, the binary will exit with code `6`, and the partial state will be exactly whatever your package manager committed before the failure. The binary itself doesn't roll back — but your package manager's transaction log will tell you precisely what was added.

**Can I run it twice back-to-back?**
Yes. The second run is a no-op if the first run succeeded — every package is already installed, so the binary exits with code `0` and changes nothing. Idempotency is one of the design goals.

**What about Windows or macOS?**
Out of scope. This tool exists specifically because the Linux package manager landscape is fragmented; on Windows and macOS the equivalent tools (winget, Homebrew, MacPorts) already handle their own ecosystems.

## Reporting security issues

If you find a security issue — anything from "the binary writes to a path it shouldn't" to "I can get the binary to install a package not on its list" — please report it privately rather than opening a public issue.

Reach out to the maintainers via the contact information listed on the repository's main page, and include:

- The SHA-256 of the binary you ran.
- The distro and version (`cat /etc/os-release`).
- The package manager and its version.
- A reproduction: exact commands, exact output, and the resulting state of the system.
- Whether you were running in a VM/container or on bare metal.

Please give the maintainers a reasonable window to respond before any public disclosure. Coordinated disclosure protects users who haven't had a chance to update yet.

For bugs that aren't security-sensitive — wrong package detected, misleading error message, documentation drift — a regular GitHub issue is the right channel.

## Contributing

Small, focused pull requests are welcome. The bar for new features is high (see [Design goals](#design-goals) — most feature requests run counter to at least one goal), but bug fixes, portability improvements, and documentation clarifications are always welcome.

Before opening a PR:

- Build locally with `make` and confirm the binary still produces a no-op run on a host that already has all dependencies installed.
- Run on at least one supported distro that's different from your everyday environment if your change touches package-manager detection or invocation.
- Keep the patch minimal — this is a small tool by design.

## License

MIT. See [LICENSE](LICENSE) for the full text.
