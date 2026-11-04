#!/bin/bash
# kclib dist tool
# Summary: Publishes verified kclibs by platform-architecture, using SHA256SUMS.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -euo pipefail

resolve_script_dir() {
    local source="${BASH_SOURCE[0]}" directory link hops=0
    while [ -L "$source" ]; do
        directory=$(cd -P "$(dirname "$source")" && pwd)
        link=$(readlink "$source")
        case "$link" in
            /*) source="$link" ;;
            *) source="$directory/$link" ;;
        esac
        hops=$((hops + 1))
        if [ "$hops" -gt 40 ]; then
            echo 'error: too many script symlinks' >&2
            return 1
        fi
    done
    cd -P "$(dirname "$source")" && pwd
}

SCRIPT_DIR=$(resolve_script_dir)
ROOT_DIR=$(dirname "$SCRIPT_DIR")
PROJ_DIR="$ROOT_DIR/proj"
DIST_DIR="${KCDIST_DIR:-$ROOT_DIR/dist}"
readonly EXCLUDED_PROJECTS=("liblibr.c")
CHANGED=0

# Resolve the capability name of a kclib project.
# @param project_name Project directory basename.
# @return Capability name.
capability_name() {
    local name="$1"
    if [[ "$name" == lib*.c ]]; then
        name="${name#lib}"
        name="${name%.c}"
    fi
    printf '%s\n' "$name"
}

# Recognize source build platform directories.
# @param platform Platform directory name.
# @return 0 when supported.
is_platform() {
    case "$1" in
        linux|windows|macos|ios|iossim|android|wasm) return 0 ;;
        *) return 1 ;;
    esac
}

# Recognize distributable project directories.
# @param project_dir Project path.
# @return 0 when distributable.
is_project() {
    local project_dir="$1" excluded
    [ -d "$project_dir" ] && [ -f "$project_dir/Makefile" ] && \
        [ -f "$project_dir/manifest.json" ] || return 1
    for excluded in "${EXCLUDED_PROJECTS[@]}"; do
        [ "$(basename "$project_dir")" = "$excluded" ] && return 1
    done
    return 0
}

# Resolve a user-supplied capability or project name.
# @param name Name to resolve.
# @return Full project path.
resolve_project() {
    local name="$1" project
    for project in "$PROJ_DIR/$name" "$PROJ_DIR/lib$name.c"; do
        if is_project "$project"; then
            printf '%s\n' "$project"
            return 0
        fi
    done
    return 1
}

# Compute SHA-256 of a file or standard input (-).
# @param file Path or -.
# @return Digest on stdout.
compute_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        openssl dgst -sha256 "$1" | awk '{print $NF}'
    fi
}

# Publish only changed files. Never overwrite a differing shared file.
# @param source Original file.
# @param destination Published file.
# @param shared 1 when owned by the whole platform/architecture.
# @return 0 on success.
publish_file() {
    local source="$1" destination="$2" shared="$3" temporary
    if [ -f "$destination" ]; then
        if cmp -s "$source" "$destination"; then
            return 0
        fi
        if [ "$shared" -eq 1 ]; then
            echo "error: conflicting shared artifact: $destination" >&2
            return 1
        fi
    fi
    mkdir -p "$(dirname "$destination")"
    temporary=$(mktemp "$(dirname "$destination")/.dist.XXXXXXXX")
    if ! cp "$source" "$temporary" || ! mv -f "$temporary" "$destination"; then
        rm -f "$temporary"
        return 1
    fi
    CHANGED=1
}

# Validate the generated build SHA of an artifact before copying it.
# @param target_dir Project build target.
# @param sha Expected SHA-256.
# @param filename Artifact filename.
# @param name Capability name.
# @param platform Target platform.
# @return 0 when valid.
verify_artifact() {
    local target_dir="$1" sha="$2" filename="$3" name="$4" platform="$5"
    local actual
    [[ "$filename" =~ ^[A-Za-z0-9_.-]+$ ]] || return 1
    case "$filename" in
        "lib$name."*|"$name"|"$name.exe") ;;
        package.json) [ "$platform" = wasm ] || return 1 ;;
        *) echo "error: unexpected build artifact: $filename" >&2; return 1 ;;
    esac
    [ -f "$target_dir/$filename" ] || { echo "error: missing $target_dir/$filename" >&2; return 1; }
    [[ "$sha" =~ ^[[:xdigit:]]{64}$ ]] || return 1
    actual=$(compute_sha256 "$target_dir/$filename")
    [ "$actual" = "$sha" ] || {
        echo "error: build checksum mismatch: $target_dir/$filename" >&2
        return 1
    }
}

# Compare published target records against the build SHA256SUMS and header.
# A valid shared SHA256SUMS is the reference; do not rehash unchanged binaries.
# @param project_dir Source project.
# @param target_dir Built target.
# @param destination Published platform-architecture directory.
# @param name Capability name.
# @return 0 when this target is already published.
target_is_current() {
    local project_dir="$1" target_dir="$2" destination="$3" name="$4"
    local checksum="$target_dir/SHA256SUMS" published="$destination/SHA256SUMS"
    local sha file found=0
    [ -f "$checksum" ] && [ -f "$published" ] || return 1
    cmp -s "$project_dir/LICENSE" "$destination/LICENSE" || return 1
    cmp -s "$project_dir/COPYING" "$destination/COPYING" || return 1
    cmp -s "$project_dir/src/lib$name.h" "$destination/lib$name.h" || return 1
    sha=$(compute_sha256 "$project_dir/src/lib$name.h")
    grep -Fqx "$sha  lib$name.h" "$published" || return 1
    while read -r sha file; do
        [ -n "$sha" ] || continue
        case "$file" in *.h) continue ;; esac
        [[ "$file" =~ ^[A-Za-z0-9_.-]+$ ]] || return 1
        [ -f "$target_dir/$file" ] && [ -f "$destination/$file" ] || return 1
        grep -Fqx "$sha  $file" "$published" || return 1
        found=$((found + 1))
    done < "$checksum"
    [ "$found" -gt 0 ] || return 1
    # No obsolete file may remain for this particular library.
    for file in "$destination/lib$name."* "$destination/$name" "$destination/$name.exe"; do
        [ -f "$file" ] || continue
        case "${file##*/}" in "lib$name.h") continue ;; esac
        grep -Fq "  ${file##*/}" "$checksum" || return 1
    done
    return 0
}

# Prune only files belonging to a selected library, not other libraries.
# @param project_dir Source project.
# @return None.
prune_project_files() {
    local project_dir="$1" name target_path target platform arch source_target
    local file filename checksum
    name=$(capability_name "$(basename "$project_dir")")
    for target in "$DIST_DIR"/*-*/; do
        [ -d "$target" ] || continue
        target_path="${target%/}"
        target_path="${target_path##*/}"
        platform="${target_path%%-*}"
        arch="${target_path#*-}"
        is_platform "$platform" || continue
        source_target="$project_dir/bin/$platform/$arch"
        checksum="$source_target/SHA256SUMS"
        for file in "$target/lib$name."* "$target/$name" "$target/$name.exe"; do
            [ -f "$file" ] || continue
            filename="${file##*/}"
            if [ "$filename" = "lib$name.h" ]; then
                [ -d "$source_target" ] && continue
            elif [ -f "$checksum" ] && grep -Fq "  $filename" "$checksum"; then
                continue
            fi
            rm -f -- "$file"
            CHANGED=1
            NEED_REFRESH=1
        done
    done
}

# Remove obsolete artifacts of projects deleted from this repository.
# Never prune other repositories when KCDIST_DIR is shared.
# @return None.
prune_stale_projects() {
    local target file filename stem
    [ -z "${KCDIST_DIR:-}" ] || return 0
    for target in "$DIST_DIR"/*-*/; do
        [ -d "$target" ] || continue
        for file in "$target"/lib*.*; do
            [ -f "$file" ] || continue
            filename="${file##*/}"
            stem="${filename%%.*}"
            [[ "$stem" == lib* ]] || continue
            if ! is_project "$PROJ_DIR/$stem.c"; then
                rm -f -- "$file"
                CHANGED=1
            fi
        done
    done
}

# Remove only obsolete generated JSON metadata, not source manifests.
# Shared roots are cleaned only for kclibs owned by this repository.
# @return None.
prune_legacy_json() {
    local path project name
    if [ -z "${KCDIST_DIR:-}" ]; then
        for path in "$DIST_DIR"/lib*.json; do
            [ -f "$path" ] || continue
            rm -f -- "$path"
            CHANGED=1
        done
    else
        for project in "$PROJ_DIR"/*/; do
            is_project "$project" || continue
            name=$(capability_name "$(basename "$project")")
            path="$DIST_DIR/lib$name.json"
            [ -f "$path" ] || continue
            rm -f -- "$path"
            CHANGED=1
        done
    fi
}

# Remove former per-library output directories only in the local distribution.
# @return None.
prune_legacy_layout() {
    local directory
    [ -z "${KCDIST_DIR:-}" ] || return 0
    for directory in "$DIST_DIR"/lib*.c/; do
        [ -f "$directory/manifest.json" ] || continue
        rm -rf -- "$directory"
        CHANGED=1
    done
    rm -f "$DIST_DIR/.build_state"
}

# Publish one library across all compiled targets.
# @param project_dir Source project.
# @return None.
package_project() {
    local project_dir="$1" name header target_dir relative platform arch checksum
    local destination sha file count updated=0 start_changes="$CHANGED"
    name=$(capability_name "$(basename "$project_dir")")
    header="$project_dir/src/lib$name.h"
    echo "Processing $(basename "$project_dir")..."
    for file in "$header" "$project_dir/LICENSE" "$project_dir/COPYING"; do
        [ -f "$file" ] || { echo "error: missing file: $file" >&2; return 1; }
    done
    for target_dir in "$project_dir"/bin/*/*; do
        [ -d "$target_dir" ] || continue
        relative="${target_dir#"$project_dir/bin/"}"
        platform="${relative%%/*}"
        arch="${relative#*/}"
        is_platform "$platform" || continue
        [[ "$arch" =~ ^[A-Za-z0-9_]+$ ]] || continue
        checksum="$target_dir/SHA256SUMS"
        [ -f "$checksum" ] || { echo "error: build checksum not found: $checksum" >&2; return 1; }
        destination="$DIST_DIR/$platform-$arch"
        if target_is_current "$project_dir" "$target_dir" "$destination" "$name"; then
            continue
        fi
        count=0
        while read -r sha file; do
            [ -n "$sha" ] || continue
            case "$file" in *.h) continue ;; esac
            verify_artifact "$target_dir" "$sha" "$file" "$name" "$platform"
            [ "$file" = package.json ] || count=$((count + 1))
        done < "$checksum"
        [ "$count" -gt 0 ] || { echo "error: no build artifacts: $checksum" >&2; return 1; }
        mkdir -p "$destination"
        publish_file "$project_dir/LICENSE" "$destination/LICENSE" 1
        publish_file "$project_dir/COPYING" "$destination/COPYING" 1
        while read -r sha file; do
            [ -n "$sha" ] || continue
            case "$file" in *.h) continue ;; esac
            if [ "$file" = package.json ]; then
                publish_file "$target_dir/$file" "$destination/$file" 1
            else
                publish_file "$target_dir/$file" "$destination/$file" 0
            fi
        done < "$checksum"
        publish_file "$header" "$destination/lib$name.h" 0
        updated=$((updated + 1))
        # An out-of-date or missing target SHA256SUMS must be regenerated even
        # if every destination file was already byte-identical.
        NEED_REFRESH=1
    done
    prune_project_files "$project_dir"
    if [ "$updated" -gt 0 ]; then
        echo "    [+] Updated $updated target(s)."
    elif [ "$CHANGED" -ne "$start_changes" ]; then
        echo "    [+] Removed obsolete artifacts."
    else
        echo "    [=] Distribution current."
    fi
}

# Refresh one SHA256SUMS per platform-architecture after actual changes.
# @return None.
refresh_target_checksums() {
    local target base file filename sha count temporary
    for target in "$DIST_DIR"/*-*/; do
        [ -d "$target" ] || continue
        base="${target%/}"
        base="${base##*/}"
        is_platform "${base%%-*}" || continue
        count=0
        for file in "$target"/*; do
            [ -f "$file" ] || continue
            case "${file##*/}" in
                LICENSE|COPYING|package.json|SHA256SUMS) ;;
                *) count=$((count + 1)) ;;
            esac
        done
        if [ "$count" -eq 0 ]; then
            rm -f "$target/LICENSE" "$target/COPYING" "$target/package.json" "$target/SHA256SUMS"
            rmdir "$target" 2>/dev/null || true
            CHANGED=1
            continue
        fi
        temporary=$(mktemp "$target/.checksum.XXXXXXXX")
        while IFS= read -r -d '' file; do
            filename="${file##*/}"
            case "$filename" in SHA256SUMS|.checksum.*|.dist.*) continue ;; esac
            sha=$(compute_sha256 "$file")
            printf '%s  %s\n' "$sha" "$filename"
        done < <(find "$target" -maxdepth 1 -type f -print0 | LC_ALL=C sort -z) > "$temporary"
        if [ -f "$target/SHA256SUMS" ] && cmp -s "$temporary" "$target/SHA256SUMS"; then
            rm -f "$temporary"
        else
            mv -f "$temporary" "$target/SHA256SUMS"
            CHANGED=1
        fi
    done
}

# Distribute all kclibs or a single selected library.
# @param name Optional project or capability name.
# @return None.
main() {
    local target="${1:-all}" project
    [ "$#" -le 1 ] || { echo "Usage: $0 [all|NAME]" >&2; return 1; }
    [ -d "$PROJ_DIR" ] || { echo "error: projects directory not found" >&2; return 1; }
    mkdir -p "$DIST_DIR"
    NEED_REFRESH=0
    case "$target" in
        all)
            for project in "$PROJ_DIR"/*/; do
                is_project "$project" || continue
                package_project "${project%/}"
            done
            prune_stale_projects
            prune_legacy_layout
            ;;
        *)
            [[ "$target" =~ ^[A-Za-z0-9_-]+$ ]] || { echo "error: invalid project name: $target" >&2; return 1; }
            project=$(resolve_project "$target") || { echo "error: project not found: $target" >&2; return 1; }
            package_project "$project"
            ;;
    esac
    prune_legacy_json
    if [ "$NEED_REFRESH" -ne 0 ]; then
        refresh_target_checksums
    fi
    if [ "$CHANGED" -eq 0 ]; then
        echo "Done. No distribution changes."
    else
        echo "Done. Distribution updated in $DIST_DIR/."
    fi
}

main "$@"
