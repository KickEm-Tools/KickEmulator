#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Gets the fork's Linux arm64 build, KT_Kick-linux-arm64, for faithful mode.
#
#   modes/vm/artifact.sh --build SRC OUTDIR
#       Builds the firmware checkout SRC in a Debian 12 (bookworm) arm64 container (Docker; native
#       on Apple Silicon, emulated by binfmt/QEMU on x86) and packages it as the fork's CI does.
#   modes/vm/artifact.sh --ci REPO RUN OUTDIR
#       Downloads the KT_Kick-linux-arm64 artifact of REPO's (owner/name) CI run RUN (a run id, or a
#       branch name: its latest successful `build` run) with `gh run download`.
#
# Either way OUTDIR/KT_Kick-linux-arm64/ holds KT_Kick, assets/, templates/, demos/,
# shared-libraries.txt and manifest.json (the layout of the fork's .github/workflows/build.yml),
# and OUTDIR/KT_Kick-linux-arm64.tar.gz the same as a tarball.
#
# The guest is Debian 12, glibc 2.36. The fork's CI builds on ubuntu-24.04-arm (glibc 2.39), and a
# binary linked there does not start on bookworm ("GLIBC_2.38 not found"); artifact.sh --ci
# checks this and says so. --build always matches the guest.
#
# Docker objects: the image kickem-vm-builder:bookworm and the volume kickem-vm-build (CPM cache
# with JUCE, and the incremental build tree). Remove them with
#   docker image rm kickem-vm-builder:bookworm && docker volume rm kickem-vm-build
# Containers are run with --rm. CPM downloads are seeded from $CPM_SOURCE_CACHE when it holds JUCE.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
IMAGE=${KICKEM_VM_BUILDER_IMAGE:-kickem-vm-builder:bookworm}
VOLUME=${KICKEM_VM_BUILD_VOLUME:-kickem-vm-build}
PKG=KT_Kick-linux-arm64

say() { printf 'artifact: %s\n' "$*" >&2; }
die() { say "$*"; exit 1; }

check_glibc() {
    # The highest GLIBC_x.y symbol version the binary needs, against the guest's 2.36.
    need=$( (strings "$1" 2>/dev/null || true) | sed -n 's/^GLIBC_\([0-9][0-9]*\.[0-9][0-9]*\)$/\1/p' | sort -t. -k1,1n -k2,2n | tail -1)
    say "KT_Kick needs glibc ${need:-?}; the guest (Debian 12) has 2.36"
    if [ -n "$need" ] && [ "$(printf '%s\n2.36\n' "$need" | sort -t. -k1,1n -k2,2n | tail -1)" != 2.36 ]; then
        say "WARNING: this binary will not start in the Debian 12 guest (it was linked against glibc $need)."
        say "         Build it for the guest with: $0 --build <firmware checkout> <outdir>"
        return 1
    fi
    return 0
}

case "${1:-}" in
--build)
    [ $# -eq 3 ] || die "usage: $0 --build SRC OUTDIR"
    SRC=$(cd "$2" && pwd)
    mkdir -p "$3"
    OUT=$(cd "$3" && pwd)
    [ -f "$SRC/Kick/CMakeLists.txt" ] || die "$SRC is not a 0700-AAM checkout (no Kick/CMakeLists.txt)"
    command -v docker >/dev/null || die "docker is not installed"
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        say "building the builder image $IMAGE (Debian 12 arm64 and the app's -dev packages)"
        docker build --platform linux/arm64 -t "$IMAGE" "$HERE/builder" >&2
    fi
    docker volume inspect "$VOLUME" >/dev/null 2>&1 || docker volume create "$VOLUME" >/dev/null
    SEED=""
    if [ -n "${CPM_SOURCE_CACHE:-}" ] && [ -d "$CPM_SOURCE_CACHE/juce" ]; then
        SEED="-v $CPM_SOURCE_CACHE:/seed:ro"
    fi
    COMMIT=$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)
    say "building $SRC ($COMMIT) for linux/arm64 in $IMAGE; build tree and CPM cache in volume $VOLUME"
    T0=$(date +%s)
    # shellcheck disable=SC2086
    docker run --rm --platform linux/arm64 -v "$SRC:/src:ro" -v "$VOLUME:/work" -v "$OUT:/out" $SEED \
        -e COMMIT="$COMMIT" -e PKG="$PKG" -e JOBS="${KICKEM_VM_BUILD_JOBS:-}" "$IMAGE" sh -euc '
        git config --global --add safe.directory "*"
        if [ -d /seed/juce ] && [ ! -d /work/cpm/juce ]; then
            echo "seeding the CPM cache from the host"; mkdir -p /work/cpm; cp -a /seed/. /work/cpm/
        fi
        if [ ! -f /work/build/CMakeCache.txt ]; then
            cmake -S /src/Kick -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTIMESTAMP=OFF \
                  -DCPM_SOURCE_CACHE=/work/cpm
        fi
        # About 2.5 GB per compiler with -O3 -flto: more jobs than that run out of memory in Docker
        # Desktop'"'"'s default 8 GB VM ("Killed signal terminated program cc1plus").
        mem=$(awk "/MemTotal/ {print int(\$2 / 2500000)}" /proc/meminfo)
        jobs=${JOBS:-$(nproc)}; [ "$mem" -ge 1 ] && [ "$mem" -lt "$jobs" ] && jobs=$mem
        echo "building with $jobs jobs"
        cmake --build /work/build --target Kick --parallel "$jobs"
        bin=/work/build/apps/kick/Kick_artefacts/Release/Kick
        file "$bin"
        rm -rf "/out/$PKG" "/out/$PKG.tar.gz"
        mkdir -p "/out/$PKG"
        cp "$bin" "/out/$PKG/KT_Kick"
        cp -R /src/Kick/assets /src/Kick/templates /src/Kick/demos "/out/$PKG/"
        ldd "/out/$PKG/KT_Kick" | awk "{print \$1}" | sort > "/out/$PKG/shared-libraries.txt"
        python3 - "/out/$PKG/manifest.json" <<PY
import json, os, platform, subprocess, sys, datetime
def run(*c): return subprocess.run(c, capture_output=True, text=True).stdout.strip()
out = sys.argv[1]
json.dump({
    "name": "KT_Kick",
    "description": "Kick app built natively for Linux arm64 in Debian 12 (KickEmulator modes/vm/artifact.sh --build; not a Buildroot SDK build)",
    "git_sha": os.environ["COMMIT"],
    "build_date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "build_type": "Release",
    "compiler": run("c++", "--version").splitlines()[0],
    "cmake": run("cmake", "--version").splitlines()[0],
    "os": run("sh", "-c", ". /etc/os-release && echo \$PRETTY_NAME"),
    "glibc": platform.libc_ver()[1],
    "arch": platform.machine(),
    "install": {"KT_Kick": "/usr/bin/KT_Kick", "assets": "/app/assets", "templates": "/app/templates", "demos": "/app/demos"},
    "files": sorted(os.listdir(os.path.dirname(out))),
}, open(out, "w"), indent=2)
PY
        tar -C /out -czf "/out/$PKG.tar.gz" "$PKG"
    ' >&2
    say "built in $(( $(date +%s) - T0 )) s: $OUT/$PKG"
    check_glibc "$OUT/$PKG/KT_Kick" || true
    ;;
--ci)
    [ $# -eq 4 ] || die "usage: $0 --ci OWNER/REPO RUN_ID|BRANCH OUTDIR"
    REPO=$2 RUN=$3
    mkdir -p "$4"
    OUT=$(cd "$4" && pwd)
    command -v gh >/dev/null || die "gh (GitHub CLI) is not installed"
    case "$RUN" in
        *[!0-9]*)
            ID=$(gh run list -R "$REPO" --branch "$RUN" --workflow build --status success --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || true)
            [ -n "$ID" ] || die "no successful build run of $REPO on branch $RUN (build it with --build)"
            RUN=$ID ;;
    esac
    say "downloading $PKG from $REPO run $RUN"
    TMP=$(mktemp -d "$OUT/.dl.XXXXXX")
    gh run download "$RUN" -R "$REPO" -n "$PKG" -D "$TMP" || { rm -rf "$TMP"; die "run $RUN has no $PKG artifact"; }
    rm -rf "$OUT/$PKG"
    tar -C "$OUT" -xzf "$TMP/$PKG.tar.gz"
    mv "$TMP/$PKG.tar.gz" "$OUT/$PKG.tar.gz"
    rm -rf "$TMP"
    say "unpacked into $OUT/$PKG"
    check_glibc "$OUT/$PKG/KT_Kick" || exit 5
    ;;
*)
    sed -n '4,25p' "$0" >&2
    exit 2 ;;
esac
