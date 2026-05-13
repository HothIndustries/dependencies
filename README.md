# dependencies

A small Linux utility that inspects an ELF binary, figures out what it needs to run, and installs the missing pieces for you — both shared libraries and system packages — using whichever package manager your distro ships with.

If you've ever downloaded a binary, run it, and been greeted by `error while loading shared libraries: libfoo.so.6: cannot open shared object file`, this tool is for you.

## Table of contents

- [What it does](#what-it-does)
- [Supported package managers](#supported-package-managers)
- [Installation](#installation)
- [Usage](#usage)
- [Examples](#examples)
- [How it works](#how-it-works)
- [Exit codes](#exit-codes)
- [Troubleshooting](#troubleshooting)
- [FAQ](#faq)
- [License](#license)

## What it does

`dependencies` takes an ELF binary as input and:

1. Parses the binary's dynamic section to enumerate every shared object it links against.
2. Checks each `.so` against the dynamic linker's search path to see whether it's actually resolvable on the current system.
3. Maps any missing libraries back to the system package that provides them.
4. Detects your distribution's package manager and uses it to install the missing packages.

It handles both layers in one pass — the low-level ELF shared-library resolution and the higher-level "which system package owns this file" lookup — so you don't have to bounce between `ldd`, `apt-file`, `dnf provides`, and friends manually.

## Supported package managers

| Distro family | Package manager | Status |
| --- | --- | --- |
| Debian, Ubuntu, Mint, Pop!_OS | `apt` | Supported |
| Fedora, RHEL, Rocky, Alma | `dnf` / `yum` | Supported |
| Arch, Manjaro, EndeavourOS | `pacman` | Supported |
| openSUSE | `zypper` | Supported |
| Alpine | `apk` | Supported |

The package manager is auto-detected; you don't need to tell it which one to use.

## Installation

This repository ships the compiled binary directly. Grab it, mark it executable, and put it somewhere on your `PATH`:

```bash
# Download the binary from the repo
chmod +x dependencies
sudo mv dependencies /usr/local/bin/
```

Verify it works:

```bash
dependencies --version
```

> **Note:** the binary is built with a Makefile against glibc on a recent Linux distribution. If you're on something exotic (musl-only, very old glibc, non-x86_64), you may need to rebuild from source.

## Usage

```
dependencies [OPTIONS] <path-to-binary>
```

### Options

| Flag | Description |
| --- | --- |
| `-n`, `--dry-run` | Show what would be installed without actually installing anything. |
| `-y`, `--yes` | Assume "yes" to all prompts (passes through to the underlying package manager). |
| `-v`, `--verbose` | Print every resolution step. |
| `-q`, `--quiet` | Only print errors. |
| `--no-install` | Just report missing dependencies; never call the package manager. |
| `-h`, `--help` | Show help. |
| `--version` | Print the version and exit. |

Most operations need `sudo` because installing packages does. If you forget, the tool will prompt you.

## Examples

### Basic — fix a binary that won't run

```bash
sudo dependencies ./my-app
```

Sample output:

```
[*] Scanning ./my-app...
[*] Found 14 dynamic dependencies.
[!] Missing: libssl.so.3
[!] Missing: libcurl.so.4
[*] Detected package manager: apt
[*] Resolving providers...
    libssl.so.3   -> libssl3
    libcurl.so.4  -> libcurl4
[*] Installing 2 packages...
[+] Done. ./my-app should now run.
```

### Dry run — see what it would do

```bash
dependencies --dry-run ./my-app
```

No `sudo` needed for a dry run, since nothing is installed.

### Report only — no package manager calls

```bash
dependencies --no-install ./my-app
```

Useful in CI pipelines where you want to fail the build on missing deps without granting install privileges.

### Non-interactive

```bash
sudo dependencies --yes --quiet ./my-app
```

Great for provisioning scripts and Dockerfiles.

## How it works

Under the hood, `dependencies` does roughly this:

1. **ELF parsing.** Opens the target, reads the ELF header, walks the program headers to find the `PT_DYNAMIC` segment, and collects every `DT_NEEDED` entry. No shelling out to `readelf` or `ldd`.
2. **Resolution.** For each needed `SONAME`, it searches `DT_RPATH`, `DT_RUNPATH`, `LD_LIBRARY_PATH`, `/etc/ld.so.cache`, and the standard system library directories — mirroring what the dynamic linker itself would do.
3. **Package mapping.** For libraries that didn't resolve, it queries the active package manager's file-to-package database (`apt-file`, `dnf provides`, `pacman -F`, etc.) to find which package owns that filename.
4. **Install.** Hands the resolved package list to the package manager in a single batched call so you get one transaction, one confirmation prompt, and one chance to bail out.

Recursive dependencies are handled by the dynamic linker once the direct deps are in place, so the tool deliberately only resolves the first layer.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success — either nothing was missing, or all missing deps were installed. |
| `1` | Generic error. |
| `2` | Invalid arguments. |
| `3` | Input file is not a valid ELF binary. |
| `4` | No supported package manager was detected. |
| `5` | One or more missing libraries couldn't be mapped to a package. |
| `6` | Package manager exited non-zero (install failed). |
| `7` | Permission denied — needs `sudo`. |

These are stable; feel free to script against them.

## Troubleshooting

### "command not found: dependencies"

The binary isn't on your `PATH`. Either move it to `/usr/local/bin/` or invoke it with an explicit path (`./dependencies ...`).

### "not a valid ELF file"

The path you passed isn't an ELF binary. This includes shell scripts with shebangs, Python scripts, statically linked Go binaries that don't use dynamic loading, and anything that isn't a Linux executable. Check with:

```bash
file ./your-binary
```

You're looking for something like `ELF 64-bit LSB executable, x86-64, dynamically linked`.

### "no supported package manager detected"

You're probably on a distro outside the supported list, or `which` can't find any of `apt`, `dnf`, `yum`, `pacman`, `zypper`, or `apk` on your `PATH`. If you're inside a stripped-down container, install the package manager binary itself first.

### "couldn't map libXYZ.so to a package"

The file-to-package database for your package manager isn't populated. Run one of the following depending on your distro:

```bash
sudo apt-file update          # Debian/Ubuntu
sudo dnf makecache            # Fedora/RHEL
sudo pacman -Fy               # Arch
```

Then retry.

### "permission denied"

Installing packages requires root. Re-run with `sudo`:

```bash
sudo dependencies ./my-app
```

If you're inside a container running as root already, this shouldn't happen — double-check your shell.

### It says everything is installed but the binary still won't run

Two common causes:

1. **Stale linker cache.** Run `sudo ldconfig` and try again.
2. **The binary needs a specific `SONAME` that conflicts with what's installed.** Run `ldd ./your-binary` to see what's still unresolved; the version in your distro repos may simply be too old.

## FAQ

**Why not just use `ldd`?**
`ldd` actually executes the target binary (via `LD_TRACE_LOADED_OBJECTS`), which is a security risk for untrusted binaries. `dependencies` does pure static parsing of the ELF file and never runs it.

**Does it work on macOS binaries (Mach-O) or Windows binaries (PE)?**
No. It's Linux/ELF only.

**Will it uninstall anything?**
No. It only installs. Cleanup is up to you and your package manager.

**Does it support cross-architecture binaries?**
It can parse them, but installing 32-bit libraries on a 64-bit system (or vice versa) requires multilib repos to be enabled by you first.

**Can I use it in a Dockerfile?**
Yes — that's a great fit. Use `--yes --quiet` so it doesn't block on prompts.

## License

MIT. See [LICENSE](LICENSE) for the full text.
