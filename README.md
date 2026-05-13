# dependencies

A small Linux ELF binary that installs a fixed set of system packages and shared libraries — automatically detecting whichever package manager your distro ships with. Just run it, and the dependencies it bakes in get installed.

No flags to learn, no manifest to write, no "which package manager are you on" prompts. Drop the binary on a fresh box, run it, done.

Built to be boring and predictable: it delegates to your distro's package manager, stays local, and exits cleanly. The whole tool is designed so that the safe thing and the easy thing are the same thing.

## Table of contents

- [What it does](#what-it-does)
- [Why it's safe](#why-its-safe)
- [Design goals](#design-goals)
- [Supported package managers](#supported-package-managers)
- [Installation](#installation)
  - [Option 1: `git clone`](#option-1-git-clone)
  - [Option 2: `curl` / `wget` from `raw.githubusercontent.com`](#option-2-curl--wget-from-rawgithubusercontentcom)
  - [Option 3: GitHub REST API](#option-3-github-rest-api)
  - [Putting it on your `PATH`](#putting-it-on-your-path)
- [Usage](#usage)
- [How it works](#how-it-works)
- [Idempotency](#idempotency)
- [Auditability](#auditability)
- [Rollback](#rollback)
- [Network behavior](#network-behavior)
- [Testing in a container](#testing-in-a-container)
- [Exit codes](#exit-codes)
- [Troubleshooting](#troubleshooting)
- [FAQ](#faq)
- [Contributing](#contributing)
- [License](#license)

## What it does

When you run `dependencies`, it:

1. Detects your Linux distribution's package manager (`apt`, `dnf`, `pacman`, `zypper`, `apk`, …).
2. Iterates through its built-in list of required packages and shared libraries.
3. Skips anything already installed, so it never re-installs a package you already have.
4. Installs everything that's missing in a single batched, non-interactive call to your package manager.
5. Exits with a status code that tells you the outcome.

The list of dependencies is hardcoded into the binary at build time — there's no config file to edit and nothing to pass on the command line. If you need a different set, you'll need a different build of the binary.

That is the entire feature surface. It does one thing on demand, in the foreground, and exits.

## Why it's safe

The tool is small and deliberately uninteresting, and that's the point. Every design choice below exists to make the binary easier to trust and easier to reason about:

- **It uses your package manager.** Every install happens through `apt`, `dnf`, `pacman`, `zypper`, or `apk`, so your distro's signature verification, dependency resolution, and conflict checking all keep working exactly as they normally do.
- **It honors your package manager's integrity checks.** Every install runs through your package manager with its standard non-interactive flag (`-y`, `--noconfirm`), so signature verification and dependency resolution apply the same way they would for any other install on your system.
- **Network activity stays with your package manager.** The only network traffic during a run is whatever your package manager generates while pulling missing packages from your already-configured mirrors.
- **All changes go through your package manager's database.** The on-disk changes after a run are exactly the packages your distro recorded itself, listed in the standard transaction log and visible to your normal tooling.
- **It's idempotent.** Running it twice produces the same end state as running it once. Already-installed packages are detected and skipped, so a second run on the same machine is a clean no-op.
- **It's easy to undo.** Because every change goes through your package manager, every change is in your distro's transaction log and is removable with one ordinary `apt remove` / `dnf remove` / `pacman -Rs` command. See [Rollback](#rollback).
- **One behavior, every time.** With no command-line arguments, the binary does exactly what's described here on every run — the behavior you read about is the behavior you get.

If you wanted a one-line summary: the binary is a thin, predictable wrapper around the package manager you already trust.

## Design goals

The tool is intentionally small. The design priorities, in order, are:

1. **Predictability.** Two runs on identical machines produce identical changes — the same packages, in the same versions, every time.
2. **Auditability.** Every action is observable on stdout/stderr and in your distro's package manager logs.
3. **Minimal trust footprint.** The binary shells out to your existing package manager rather than fetching software directly. Your distro keeps doing signature checks and dependency resolution.
4. **No moving parts.** The binary talks to your package manager and the local filesystem — that's the full scope of what it does.
5. **Easy to throw away.** Everything it changes is tracked by your package manager, so removing it is `rm dependencies` and (optionally) uninstalling the packages it added.

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

## Installation

This repository ships the compiled binary directly. Pick whichever fetch method fits your environment, then mark it executable and run it.

### Option 1: `git clone`

Best if you want the full repo (README, history) alongside the binary:

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

For reproducible automation, pin to a specific commit instead of `main`:

```bash
curl -fsSLO https://raw.githubusercontent.com/HothIndustries/dependencies/e3e4ad1/dependencies
```

Pinning a commit SHA gives you byte-identical downloads on every run, which is useful for build pipelines that need to be repeatable.

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

`/usr/local/bin` is the conventional location for locally-installed binaries on most distros.

## Usage

```bash
./dependencies
```

That's it. There are no flags.

The binary writes its progress to stdout and any errors to stderr, then exits. It runs in the foreground and returns control to your shell as soon as it's done.

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

A couple of things worth noticing in that output:

- Each package is enumerated and labelled `[✓]` or `[!]` *before* anything is installed, so the full list is visible up front.
- The line `[*] Installing 3 packages...` precedes the actual install, and the package manager's own output streams directly under it. You see exactly the same messages you'd see if you'd run the install command yourself.
- The final `[+] Done.` appears on a clean exit. A non-zero exit always reflects a real failure; exit `0` means every install completed.

## How it works

Under the hood, the binary does roughly this:

1. **Detect the package manager.** Walks `PATH` looking for `apt-get`, `dnf`, `yum`, `pacman`, `zypper`, and `apk` — in that order — and picks the first one it finds. Also checks `/etc/os-release` as a cross-reference. The cross-reference exists so that a chroot or container that happens to have a foreign package manager in its `PATH` is recognized correctly.
2. **Check what's already installed.** Queries the package manager (`dpkg -s`, `rpm -q`, `pacman -Q`, etc.) for each item in the built-in list. The query is read-only.
3. **Decide what to install.** Builds the list of missing packages. If the list is empty, the binary skips straight to a clean exit — your machine was already in the desired state.
4. **Install what's missing.** Hands the remaining list to the package manager in one batched, non-interactive call so you get a single transaction rather than one prompt per package.
5. **Surface the result.** Streams the package manager's stdout/stderr to your terminal, then exits with a code that reflects the outcome.

Shared libraries arrive via the package that ships them, so they appear in the same package-manager-tracked location as anything else your distro installed — updatable and removable through your distro's normal tooling.

## Idempotency

Running the binary twice produces the same end state as running it once. On the second run:

- Every package is already installed, so the detection step marks all of them `[✓]`.
- The install step has nothing to do and is skipped entirely.
- The binary exits `0` immediately.

That property is useful for configuration management, image builds, and CI pipelines: you can include the binary unconditionally in a setup step without worrying about double-application.

## Auditability

Two layers of logs cover a run:

1. **The binary's own output.** Each detected package, each install decision, and the final result are written to stdout. Errors go to stderr. What you see in your terminal is the entire trace.
2. **Your package manager's logs.** Every install is recorded by your distro:
    - Debian/Ubuntu: `/var/log/apt/history.log`
    - Fedora/RHEL: `dnf history`
    - Arch: `/var/log/pacman.log`
    - openSUSE: `zypper history`
    - Alpine: `/var/log/apk.log`

To capture both streams to a file for later review:

```bash
./dependencies 2>&1 | tee dependencies-$(date +%F-%H%M%S).log
```

Between the binary's own output and the package manager's transaction log, every change made during a run is recorded somewhere durable and reviewable.

## Rollback

Because every install happens through your package manager, every install is reversible with that same package manager. Everything the binary changed is tracked by the package manager's transaction log, so a single removal command undoes any run.

To undo a run, use your distro's standard removal command. For example, on Debian/Ubuntu:

```bash
sudo apt-get remove --auto-remove build-essential libcurl4 zlib1g libsqlite3-0
```

The `--auto-remove` flag also removes any dependencies that were only pulled in to satisfy the packages you're removing. On `dnf`, `sudo dnf remove` is auto-remove-aware by default; on `pacman`, `sudo pacman -Rs` removes a package and its no-longer-needed dependencies.

If you don't remember which packages were added, your distro's transaction log (see [Auditability](#auditability)) has the exact list, timestamped.

## Network behavior

All network activity during a run flows through your package manager: when packages need to be fetched, your distro pulls them from the mirrors configured in its existing config — the same mirrors it would use for any other install.

In practice:

- If every package is already installed, the run finishes locally.
- If packages are missing, traffic goes to the mirrors your package manager is already configured to use.

If you're operating in an air-gapped environment, mirror your distro's repositories locally and point the package manager at them. The binary works transparently in that setup because everything still flows through your package manager.

## Testing in a container

If you'd like to try the binary in a disposable environment before using it on a long-lived host, a container is a quick way to do it. The container's package manager is real, so the behavior matches what you'd see on a host of the same distro.

Debian:

```bash
docker run --rm -it -v "$PWD/dependencies:/usr/local/bin/dependencies:ro" debian:stable bash -c '
  apt-get update &&
  dependencies
'
```

Alpine:

```bash
docker run --rm -it -v "$PWD/dependencies:/usr/local/bin/dependencies:ro" alpine:latest sh -c '
  apk update &&
  dependencies
'
```

This is also a handy way to confirm support for a distro you don't run day-to-day.

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

These are stable; feel free to script against them. A non-zero exit always reflects a real failure; exit `0` means every install completed.

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
- **The package manager can't be invoked with enough privilege to install.** Installing system packages is a privileged operation on every supported distro; your environment needs to grant the binary (or its parent process) the right to call the package manager. The usual options are running as root inside a container, or a sudoers entry for the user running the binary.

### "no supported package manager detected"

You're on a distro outside the supported list, or you're inside a stripped-down container that doesn't ship one. Install the package manager binary first, then re-run.

### "cannot execute binary file: Exec format error"

The binary was built for a different CPU architecture than yours (most likely you're on ARM and the binary is x86_64, or vice versa). Confirm with:

```bash
file dependencies
uname -m
```

If the two don't match, you'll need a binary built for your architecture.

### Package manager prompts hang the binary

The binary runs the package manager non-interactively, but some package managers will still prompt under specific conditions (e.g. a held dependency conflict on `apt`). If that happens, run your package manager manually to clear whatever it's waiting on, then re-run `dependencies`.

### A package failed to install

Run your distro's update command first — the binary doesn't refresh the package index for you:

```bash
sudo apt update        # Debian/Ubuntu
sudo dnf check-update  # Fedora/RHEL
sudo pacman -Sy        # Arch
sudo zypper refresh    # openSUSE
sudo apk update        # Alpine
```

Then try again. If it still fails, the failure is now your package manager's failure, and its error message is the right place to start debugging.

### The binary exits 0 but something I expected isn't installed

The most common cause is that the package you expected wasn't on the binary's built-in list — different builds can have different lists. To inspect what a particular build will install, extract the strings from the binary:

```bash
strings dependencies | sort -u | less
```

## FAQ

**Can I change the list of dependencies it installs?**
Not from outside the binary. The list is hardcoded at compile time, so changing it means producing a new build with a different list. The lack of a config file is deliberate — see [Design goals](#design-goals).

**Why hardcode the list instead of reading a config file?**
Predictability. The binary is a self-contained "set up this machine" artifact you can drop on any supported distro and run without dragging along extra files. A config file would introduce a second artifact, a second place for bugs to hide, and a second thing to keep in sync.

**Does it uninstall or update anything?**
No. It only installs what's missing. Already-installed packages are left alone — even if a newer version is available. Updating is your package manager's job, not this tool's.

**Does it work in a Dockerfile?**
Yes. It's designed to run non-interactively, so it works well as a single `RUN` step. Make sure your base image has its package index refreshed first. A typical use:

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
Whatever the published binary was built for. The repo ships one prebuilt binary; for other architectures you'd need a separate build.

**Can it install packages from third-party repositories?**
Only if those repositories are already configured on the host. The binary uses your existing repository configuration as-is.

**How do I get a new version?**
Replace the binary the same way you installed it. Updates are explicit — you choose when to upgrade.

**What happens if the package list contains a package my distro doesn't have?**
The package manager will fail, the binary will exit with code `6`, and the partial state will be whatever your package manager committed before the failure. The binary itself doesn't roll back — but your package manager's transaction log will show exactly what was added.

**Can I run it twice back-to-back?**
Yes. The second run is a no-op if the first run succeeded — every package is already installed, so the binary exits with code `0` and changes nothing. See [Idempotency](#idempotency).

**What about Windows or macOS?**
Out of scope. This tool exists specifically because the Linux package manager landscape is fragmented; on Windows and macOS the equivalent tools (winget, Homebrew, MacPorts) already handle their own ecosystems.

## Contributing

Documentation fixes and clarifications via pull request are welcome.

## License

MIT. See [LICENSE](LICENSE) for the full text.
