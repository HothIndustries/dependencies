/*
 * dependencies -- installs a fixed set of system packages by detecting
 * and delegating to whichever package manager the host distro ships
 * with.  See README.md for the full description.
 *
 * The binary's whole job is to be a thin, predictable wrapper around
 * the package manager you already trust.  It does not fetch software
 * directly, it does not bypass signature checks, it makes no network
 * calls of its own, and it writes no state outside of what the
 * package manager itself records.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exit codes -- must match the table in README.md. */
#define EX_OK         0
#define EX_GENERIC    1
#define EX_USAGE      2
#define EX_NOPM       4
#define EX_QUERY_FAIL 5
#define EX_PM_FAIL    6
#define EX_PERM       7

#define MAX_ARGV      32   /* query/install prefix + one package per slot */
#define ARRLEN(a)     ((int)(sizeof(a) / sizeof(*(a))))

/* ---------- package lists ----------
 *
 * Twelve packages per manager.  The names are distro-native because
 * each manager's query and install commands expect its own naming.
 * Edit these arrays and rebuild to change what `dependencies` ships.
 */

static const char *apt_pkgs[] = {
    "build-essential", "libssl3",        "libcurl4",     "zlib1g",
    "libsqlite3-0",    "ca-certificates", "libffi8",     "libxml2",
    "libreadline8",    "libncursesw6",   "libbz2-1.0",   "liblzma5",
};

static const char *dnf_pkgs[] = {
    "gcc",             "openssl-libs",   "libcurl",      "zlib",
    "sqlite-libs",     "ca-certificates", "libffi",      "libxml2",
    "readline",        "ncurses-libs",   "bzip2-libs",   "xz-libs",
};

static const char *yum_pkgs[] = {
    "gcc",             "openssl-libs",   "libcurl",      "zlib",
    "sqlite",          "ca-certificates", "libffi",      "libxml2",
    "readline",        "ncurses-libs",   "bzip2-libs",   "xz-libs",
};

static const char *pacman_pkgs[] = {
    "base-devel",      "openssl",        "curl",         "zlib",
    "sqlite",          "ca-certificates", "libffi",      "libxml2",
    "readline",        "ncurses",        "bzip2",        "xz",
};

static const char *zypper_pkgs[] = {
    "gcc",             "libopenssl3",    "libcurl4",     "libz1",
    "sqlite3",         "ca-certificates", "libffi8",     "libxml2-2",
    "readline-devel",  "libncurses6",    "libbz2-1",     "xz",
};

static const char *apk_pkgs[] = {
    "build-base",      "openssl",        "curl",         "zlib",
    "sqlite-libs",     "ca-certificates", "libffi",      "libxml2",
    "readline",        "ncurses-libs",   "bzip2",        "xz-libs",
};

/* ---------- package manager table ----------
 *
 * Detection walks this table top-to-bottom; the first entry whose
 * `probe` binary is on PATH wins.  README.md documents the order.
 */

struct pm {
    const char        *name;
    const char        *probe;
    const char        *osrel_ids[8];
    const char        *query[4];     /* NULL-terminated argv prefix */
    const char        *install[5];   /* NULL-terminated argv prefix */
    const char *const *packages;
    int                npackages;
};

static const struct pm pms[] = {
    { "apt", "apt-get",
        { "debian", "ubuntu", "linuxmint", "pop", "raspbian", NULL },
        { "dpkg",   "-s",     NULL },
        { "apt-get","install","-y", NULL },
        apt_pkgs,    ARRLEN(apt_pkgs) },
    { "dnf", "dnf",
        { "fedora", "rhel", "rocky", "almalinux", "centos", NULL },
        { "rpm",    "-q",     NULL },
        { "dnf",    "install","-y", NULL },
        dnf_pkgs,    ARRLEN(dnf_pkgs) },
    { "yum", "yum",
        { "rhel",   "centos", NULL },
        { "rpm",    "-q",     NULL },
        { "yum",    "install","-y", NULL },
        yum_pkgs,    ARRLEN(yum_pkgs) },
    { "pacman", "pacman",
        { "arch", "manjaro", "endeavouros", "artix", NULL },
        { "pacman", "-Q",     NULL },
        { "pacman", "-S",     "--noconfirm", NULL },
        pacman_pkgs, ARRLEN(pacman_pkgs) },
    { "zypper", "zypper",
        { "opensuse", "opensuse-leap", "opensuse-tumbleweed", "sles", NULL },
        { "rpm",    "-q",     NULL },
        { "zypper", "--non-interactive", "install", NULL },
        zypper_pkgs, ARRLEN(zypper_pkgs) },
    { "apk",  "apk",
        { "alpine", NULL },
        { "apk",    "info",   "-e",   NULL },
        { "apk",    "add",    NULL },
        apk_pkgs,    ARRLEN(apk_pkgs) },
};

/* ---------- small helpers ---------- */

static void out(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

static void err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("[!] ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

/* Walk PATH looking for `bin`; return 1 if found and executable. */
static int found_in_path(const char *bin) {
    const char *path = getenv("PATH");
    if (!path || !*path)
        path = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

    char full[4096];
    size_t blen = strlen(bin);
    const char *p = path;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t dlen = colon ? (size_t)(colon - p) : strlen(p);
        if (dlen > 0 && dlen + 1 + blen + 1 <= sizeof(full)) {
            memcpy(full, p, dlen);
            full[dlen] = '/';
            memcpy(full + dlen + 1, bin, blen + 1);
            if (access(full, X_OK) == 0)
                return 1;
        }
        if (!colon) break;
        p = colon + 1;
    }
    return 0;
}

/* Cross-reference /etc/os-release.  Returns 1 if any token of ID or
 * ID_LIKE matches one of `ids`.  Missing file is not an error -- the
 * caller treats a no-match as a non-fatal mismatch. */
static int osrel_matches(const char *const ids[]) {
    FILE *f = fopen("/etc/os-release", "r");
    if (!f) return 0;

    char line[512];
    int hit = 0;
    while (!hit && fgets(line, sizeof(line), f)) {
        const char *rest = NULL;
        if      (!strncmp(line, "ID=",      3)) rest = line + 3;
        else if (!strncmp(line, "ID_LIKE=", 8)) rest = line + 8;
        else continue;

        char val[256];
        size_t v = 0;
        for (const char *c = rest; *c && *c != '\n' && v + 1 < sizeof(val); c++) {
            if (*c == '"' || *c == '\'') continue;
            val[v++] = *c;
        }
        val[v] = '\0';

        for (char *tok = strtok(val, " "); tok && !hit; tok = strtok(NULL, " ")) {
            for (int i = 0; ids[i]; i++) {
                if (!strcasecmp(tok, ids[i])) { hit = 1; break; }
            }
        }
    }
    fclose(f);
    return hit;
}

/* Detect the package manager.  PATH search is primary; /etc/os-release
 * is a cross-reference for environments where the PATH binary is real
 * but might be unexpected (e.g. a foreign chroot). */
static const struct pm *pm_detect(void) {
    for (int i = 0; i < ARRLEN(pms); i++) {
        if (!found_in_path(pms[i].probe))
            continue;
        if (!osrel_matches(pms[i].osrel_ids)) {
            /* Non-fatal: trust PATH but make the disagreement visible. */
            out("[*] Note: %s present on PATH but /etc/os-release does not advertise it",
                pms[i].name);
        }
        return &pms[i];
    }
    return NULL;
}

/* Run argv with stdio redirected to /dev/null; returns the child's
 * exit status, or -1 if we could not fork/wait. */
static int run_silent(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, 1);
            dup2(devnull, 2);
            close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Run argv inheriting stdio so the user sees the package manager's
 * own output verbatim. */
static int run_loud(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Returns 1 = installed, 0 = not installed, -1 = query itself failed. */
static int pkg_installed(const struct pm *pm, const char *pkg) {
    char *argv[MAX_ARGV];
    int i = 0;
    for (; pm->query[i] && i + 2 < MAX_ARGV; i++)
        argv[i] = (char *)pm->query[i];
    argv[i++] = (char *)pkg;
    argv[i]   = NULL;

    int rc = run_silent(argv);
    if (rc < 0 || rc == 127) return -1;
    return rc == 0;
}

/* Returns the package manager's exit status, or -1 on spawn failure. */
static int install_all(const struct pm *pm, const char *missing[], int n) {
    char **argv = calloc((size_t)n + 8, sizeof(*argv));
    if (!argv) return -1;
    int i = 0;
    for (; pm->install[i]; i++) argv[i] = (char *)pm->install[i];
    for (int j = 0; j < n; j++)  argv[i++] = (char *)missing[j];
    argv[i] = NULL;
    int rc = run_loud(argv);
    free(argv);
    return rc;
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    (void)argv;
    if (argc > 1) {
        fprintf(stderr, "dependencies: takes no arguments\n");
        return EX_USAGE;
    }

    const struct pm *pm = pm_detect();
    if (!pm) {
        err("no supported package manager detected");
        return EX_NOPM;
    }

    out("[*] Detected package manager: %s", pm->name);
    out("[*] Checking %d required dependencies...", pm->npackages);

    /* Compute a printable width so the em-dash column lines up. */
    int width = 0;
    for (int i = 0; i < pm->npackages; i++) {
        int len = (int)strlen(pm->packages[i]);
        if (len > width) width = len;
    }

    const char *missing[64];
    int nmissing = 0;

    for (int i = 0; i < pm->npackages; i++) {
        const char *p = pm->packages[i];
        int q = pkg_installed(pm, p);
        if (q < 0) {
            err("could not query package state for %s", p);
            return EX_QUERY_FAIL;
        }
        if (q) {
            out("[\xE2\x9C\x93] %-*s \xE2\x80\x94 already installed", width, p);
        } else {
            out("[!] %-*s \xE2\x80\x94 missing", width, p);
            if (nmissing < ARRLEN(missing))
                missing[nmissing++] = p;
        }
    }

    if (nmissing == 0) {
        out("[+] Done. All dependencies satisfied.");
        return EX_OK;
    }

    out("[*] Installing %d packages...", nmissing);
    int rc = install_all(pm, missing, nmissing);
    if (rc < 0) {
        err("failed to invoke %s", pm->install[0]);
        return EX_GENERIC;
    }
    if (rc == 127) {
        /* exec failure inside the child -- usually EACCES or ENOENT. */
        return EX_PERM;
    }
    if (rc != 0) {
        return EX_PM_FAIL;
    }

    out("[+] Done. All dependencies satisfied.");
    return EX_OK;
}
