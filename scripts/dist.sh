#!/bin/bash
# kclib dist tool
# Summary: Publishes all libraries together by platform and architecture.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -euo pipefail

# Resolve the script directory without depending on the caller's cwd.
# @return Absolute source directory.
resolve_script_dir() {
    local source="${BASH_SOURCE[0]}"
    local directory link hops=0
    while [ -L "$source" ]; do
        directory=$(cd -P "$(dirname "$source")" && pwd) || return 1
        link=$(readlink "$source") || return 1
        case "$link" in
            /*) source="$link" ;;
            *) source="$directory/$link" ;;
        esac
        hops=$((hops + 1))
        if [ "$hops" -gt 40 ]; then
            echo "error: too many script symlinks" >&2
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
declare -a PUBLISHED_PATHS=()
declare -A PUBLISHED_SET=()
declare -A SOURCE_BUILD_SHA=()
CHANGED=0
NEED_REFRESH=0

# Print supported commands.
# @return None.
usage() {
    echo "Usage: $0 [all|NAME]" >&2
}

# Return the capability name for one project.
# @param project_name Directory name.
# @return Name on stdout.
capability_name() {
    local project_name="$1"
    if [[ "$project_name" == lib*.c ]]; then
        printf '%s\n' "${project_name#lib}" | sed 's/\.c$//'
    else
        printf '%s\n' "$project_name"
    fi
}

# Determine whether a directory is a supported build platform.
# @param platform Platform name.
# @return 0 if recognized.
is_platform() {
    case "$1" in
        linux|windows|macos|ios|iossim|android|wasm) return 0 ;;
        *) return 1 ;;
    esac
}

# Determine whether a project is present and distributable.
# @param directory Source project.
# @return 0 if valid.
is_project() {
    local directory="$1" excluded
    [ -d "$directory" ] && [ -f "$directory/Makefile" ] &&
        [ -f "$directory/manifest.json" ] || return 1
    for excluded in "${EXCLUDED_PROJECTS[@]}"; do
        [ "$(basename "$directory")" = "$excluded" ] && return 1
    done
    return 0
}

# Find a project by directory or logical capability name.
# @param name User-supplied name.
# @return Project path.
resolve_project() {
    local name="$1" directory
    for directory in "$PROJ_DIR/$name" "$PROJ_DIR/lib$name.c"; do
        if is_project "$directory"; then
            printf '%s\n' "$directory"
            return 0
        fi
    done
    return 1
}

# Compute the portable SHA-256 digest of a file or stdin.
# @param file File path or - for stdin.
# @return Digest on stdout.
compute_sha256() {
    local file="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file" | awk '{print $1}'
    else
        openssl dgst -sha256 "$file" | awk '{print $NF}'
    fi
}

# Return a file's byte size on supported hosts.
# @param file File path.
# @return Size in bytes on stdout.
file_size() {
    stat -c%s "$1" 2>/dev/null || stat -f%z "$1"
}

# Publish one file without removing neighboring libraries' artifacts.
# Shared files must be identical across projects.
# @param source Source path.
# @param destination Published path.
# @param shared 1 for LICENSE, COPYING and package.json.
# @return 0 on success.
publish_file() {
    local source="$1" destination="$2" shared="$3" temporary
    if [ -f "$destination" ]; then
        cmp -s "$source" "$destination" && return 0
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

# Validate a listed build artifact and its source checksum.
# @param target_dir Built target.
# @param expected_sha Expected SHA-256.
# @param filename Artifact basename.
# @param name Capability name.
# @param platform Target platform.
# @return 0 on success.
verify_artifact() {
    local target_dir="$1" expected_sha="$2" filename="$3" name="$4" platform="$5" actual
    if [[ ! "$filename" =~ ^[A-Za-z0-9_.-]+$ ]] ||
        [[ "$filename" == "." || "$filename" == ".." ]]; then
        echo "error: invalid artifact filename: $filename" >&2
        return 1
    fi
    case "$filename" in
        "lib$name."*|"$name"|"$name.exe") ;;
        package.json)
            if [ "$platform" != wasm ]; then
                echo "error: package.json outside WASM target" >&2
                return 1
            fi
            ;;
        *)
            echo "error: unexpected build artifact: $filename" >&2
            return 1
            ;;
    esac
    if [[ ! "$expected_sha" =~ ^[[:xdigit:]]{64}$ ]] ||
        [ ! -f "$target_dir/$filename" ]; then
        echo "error: missing or invalid build artifact: $target_dir/$filename" >&2
        return 1
    fi
    actual=$(compute_sha256 "$target_dir/$filename")
    if [ "$actual" != "$expected_sha" ]; then
        echo "error: build checksum mismatch: $target_dir/$filename" >&2
        echo "run ./scripts/build.sh $name to refresh SHA256SUMS" >&2
        return 1
    fi
}

# Track one project-owned artifact for its per-library manifest.
# @param relative Target-directory path and filename.
# @param build_sha SHA-256 of source build checksum file.
# @return None.
record_artifact() {
    local relative="$1" build_sha="$2"
    PUBLISHED_PATHS+=("$relative")
    PUBLISHED_SET["$relative"]=1
    SOURCE_BUILD_SHA["$relative"]="$build_sha"
}

# Read the owned paths from an existing project manifest.
# @param manifest Published per-library manifest.
# @return One relative path per line.
manifest_paths() {
    local manifest="$1"
    [ -f "$manifest" ] || return 0
    sed -n 's/^[[:space:]]*"path": "\([^"]*\)".*/\1/p' "$manifest"
}

# Remove a project-owned artifact no longer present in its source bin.
# Never remove common license, runtime metadata, or another project's files.
# @param project_name Full source project name.
# @param manifest Former manifest.
# @return None.
prune_project_files() {
    local project_name="$1" manifest="$2" name relative filename target
    name=$(capability_name "$project_name")
    while IFS= read -r relative; do
        [ -n "$relative" ] || continue
        [[ "$relative" =~ ^(linux|windows|macos|ios|iossim|android|wasm)-[A-Za-z0-9_]+/[A-Za-z0-9_.-]+$ ]] || continue
        filename="${relative##*/}"
        case "$filename" in
            "lib$name."*|"$name"|"$name.exe") ;;
            *) continue ;;
        esac
        [ -n "${PUBLISHED_SET[$relative]+x}" ] && continue
        target="$DIST_DIR/$relative"
        if [ -f "$target" ]; then
            rm -f "$target"
            CHANGED=1
        fi
    done < <(manifest_paths "$manifest")
}

# Write a deterministic per-library manifest at the distribution root.
# @param project_dir Source project directory.
# @return None.
write_manifest() {
    local project_dir="$1" source_sha="$2" project_name name manifest temporary metadata_sha
    local relative file target platform arch sha bytes build_sha first
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    manifest="$DIST_DIR/lib$name.json"
    if [ "${#PUBLISHED_PATHS[@]}" -eq 0 ]; then
        if [ -f "$manifest" ]; then
            rm -f "$manifest"
            CHANGED=1
        fi
        return 0
    fi

    metadata_sha=$(compute_sha256 "$project_dir/manifest.json")
    temporary=$(mktemp "$DIST_DIR/.manifest.XXXXXXXX")
    {
        printf '{\n  "metadata": '
        cat "$project_dir/manifest.json"
        printf ',\n  "metadata_sha256": "%s",\n  "source_sha256": "%s",\n  "artifacts": [\n' "$metadata_sha" "$source_sha"
        first=1
        for relative in "${PUBLISHED_PATHS[@]}"; do
            target="$DIST_DIR/$relative"
            file="${relative##*/}"
            platform="${relative%%-*}"
            arch="${relative#*-}"
            arch="${arch%%/*}"
            sha=$(compute_sha256 "$target")
            bytes=$(file_size "$target")
            build_sha="${SOURCE_BUILD_SHA[$relative]}"
            if [ "$first" -eq 0 ]; then
                printf ',\n'
            fi
            first=0
            printf '    {\n'
            printf '      "arch": "%s",\n' "$arch"
            printf '      "platform": "%s",\n' "$platform"
            printf '      "file": "%s",\n' "$file"
            printf '      "size_bytes": %s,\n' "$bytes"
            printf '      "sha256": "%s",\n' "$sha"
            printf '      "build_sha256": "%s",\n' "$build_sha"
            printf '      "path": "%s"\n' "$relative"
            printf '    }'
        done
        printf '\n  ]\n}\n'
    } > "$temporary"
    if [ -f "$manifest" ] && cmp -s "$temporary" "$manifest"; then
        rm -f "$temporary"
    else
        mv -f "$temporary" "$manifest"
        CHANGED=1
    fi
}

# Compute a deterministic fingerprint of one project's distribution inputs.
# The generated files and their timestamps do not affect this value.
# @param project_dir Source project.
# @return SHA-256 of source metadata, header, licenses and build checksums.
project_source_sha256() {
    local project_dir="$1" project_name name file target_dir relative platform arch
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    {
        for file in manifest.json "src/lib$name.h" LICENSE COPYING; do
            [ -f "$project_dir/$file" ] || return 1
            printf '%s  %s\n' "$(compute_sha256 "$project_dir/$file")" "$file"
        done
        for target_dir in "$project_dir"/bin/*/*; do
            [ -d "$target_dir" ] || continue
            relative="${target_dir#"$project_dir/bin/"}"
            platform="${relative%%/*}"
            arch="${relative#*/}"
            is_platform "$platform" || continue
            [[ "$arch" =~ ^[A-Za-z0-9_]+$ ]] || continue
            file="$target_dir/SHA256SUMS"
            [ -f "$file" ] || return 1
            printf '%s  %s\n' "$(compute_sha256 "$file")" "$relative/SHA256SUMS"
        done
    } | compute_sha256 -
}

# Check the recorded input fingerprint and the integrity of published files.
# @param project_dir Source project.
# @param manifest Existing project manifest.
# @param source_sha Current source input fingerprint.
# @return 0 when nothing needs to be republished.
project_distribution_current() {
    local project_dir="$1" manifest="$2" source_sha="$3"
    local project_name name expected_sha relative filename target_dir target_path
    local current_sha count=0
    local -A checked_targets=()

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    [ -f "$manifest" ] || return 1
    grep -Fq "\"source_sha256\": \"$source_sha\"" "$manifest" || return 1
    [ ! -f "$DIST_DIR/$project_name/manifest.json" ] || return 1

    while IFS=$'\t' read -r expected_sha relative; do
        [ -n "$relative" ] || continue
        [[ "$expected_sha" =~ ^[[:xdigit:]]{64}$ ]] || return 1
        [[ "$relative" =~ ^(linux|windows|macos|ios|iossim|android|wasm)-[A-Za-z0-9_]+/[A-Za-z0-9_.-]+$ ]] || return 1
        filename="${relative##*/}"
        case "$filename" in
            "lib$name."*|"$name"|"$name.exe") ;;
            *) return 1 ;;
        esac
        target_path="$DIST_DIR/$relative"
        [ -f "$target_path" ] || return 1
        current_sha=$(compute_sha256 "$target_path") || return 1
        [ "$current_sha" = "$expected_sha" ] || return 1

        target_dir="${relative%/*}"
        if [ -z "${checked_targets[$target_dir]+x}" ]; then
            cmp -s "$project_dir/LICENSE" "$DIST_DIR/$target_dir/LICENSE" || return 1
            cmp -s "$project_dir/COPYING" "$DIST_DIR/$target_dir/COPYING" || return 1
            [ -f "$DIST_DIR/$target_dir/SHA256SUMS" ] || return 1
            if [[ "$target_dir" == wasm-* ]] &&
                [ -f "$project_dir/bin/wasm/${target_dir#wasm-}/package.json" ]; then
                cmp -s "$project_dir/bin/wasm/${target_dir#wasm-}/package.json" \
                    "$DIST_DIR/$target_dir/package.json" || return 1
            fi
            checked_targets["$target_dir"]=1
        fi
        count=$((count + 1))
    done < <(
        awk '
            /"artifacts": \[/ { inside = 1; next }
            inside && /"sha256":/ {
                sha = $0
                sub(/^.*"sha256": "/, "", sha)
                sub(/".*$/, "", sha)
            }
            inside && /"path":/ {
                path = $0
                sub(/^.*"path": "/, "", path)
                sub(/".*$/, "", path)
                print sha "\t" path
                sha = ""
            }
        ' "$manifest"
    )
    [ "$count" -gt 0 ]
}

# Publish a single library's available targets and public header.
# @param project_dir Source project directory.
# @return None.
package_project() {
    local project_dir="$1" project_name name header manifest target_dir relative
    local platform arch destination checksum build_sha sha file count published=0
    local source_sha previous_changed="$CHANGED"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"
    manifest="$DIST_DIR/lib$name.json"
    PUBLISHED_PATHS=()
    PUBLISHED_SET=()
    SOURCE_BUILD_SHA=()

    echo "Processing $project_name..."
    if [ ! -f "$header" ] || [ ! -f "$project_dir/LICENSE" ] ||
        [ ! -f "$project_dir/COPYING" ]; then
        echo "error: missing public header, LICENSE or COPYING in $project_dir" >&2
        return 1
    fi
    source_sha=$(project_source_sha256 "$project_dir") || return 1
    if project_distribution_current "$project_dir" "$manifest" "$source_sha"; then
        echo "    [=] Distribution current."
        return 0
    fi
    NEED_REFRESH=1

    for target_dir in "$project_dir"/bin/*/*; do
        [ -d "$target_dir" ] || continue
        relative="${target_dir#"$project_dir/bin/"}"
        platform="${relative%%/*}"
        arch="${relative#*/}"
        is_platform "$platform" || continue
        [[ "$arch" =~ ^[A-Za-z0-9_]+$ ]] || continue

        checksum="$target_dir/SHA256SUMS"
        if [ ! -f "$checksum" ]; then
            echo "error: build checksum not found: $checksum" >&2
            echo "run ./scripts/build.sh $name" >&2
            return 1
        fi
        build_sha=$(compute_sha256 "$checksum")
        destination="$DIST_DIR/$platform-$arch"
        mkdir -p "$destination"
        count=0

        # Verify the entire artifact set before publishing this target.
        while read -r sha file; do
            [ -n "$sha" ] || continue
            case "$file" in *.h) continue ;; esac
            verify_artifact "$target_dir" "$sha" "$file" "$name" "$platform"
            [ "$file" = package.json ] || count=$((count + 1))
        done < "$checksum"
        if [ "$count" -eq 0 ]; then
            echo "error: no build artifacts in $checksum" >&2
            return 1
        fi

        # Shared files may be written by any library, but never overwritten
        # with different contents.
        publish_file "$project_dir/LICENSE" "$destination/LICENSE" 1
        publish_file "$project_dir/COPYING" "$destination/COPYING" 1
        while read -r sha file; do
            [ -n "$sha" ] || continue
            case "$file" in *.h) continue ;; esac
            if [ "$file" = package.json ]; then
                publish_file "$target_dir/$file" "$destination/$file" 1
            else
                publish_file "$target_dir/$file" "$destination/$file" 0
                record_artifact "$platform-$arch/$file" "$build_sha"
            fi
        done < "$checksum"
        publish_file "$header" "$destination/lib$name.h" 0
        record_artifact "$platform-$arch/lib$name.h" "$build_sha"
        published=$((published + 1))
    done

    prune_project_files "$project_name" "$manifest"
    write_manifest "$project_dir" "$source_sha"

    # Remove only this project's obsolete layout, also in a shared root.
    if [ -f "$DIST_DIR/$project_name/manifest.json" ]; then
        rm -rf "${DIST_DIR:?}/${project_name:?}"
        CHANGED=1
    fi
    if [ "$published" -eq 0 ]; then
        echo "    [=] No build artifacts."
    elif [ "$CHANGED" -eq "$previous_changed" ]; then
        echo "    [=] Distribution current."
    else
        echo "    [+] Updated $published target(s)."
    fi
}

# Remove manifests and files of projects no longer in the monorepo.
# Only prune repository-owned local distributions, never a shared KCDIST_DIR.
# @return None.
prune_stale_projects() {
    local manifest name project_name
    [ -z "${KCDIST_DIR:-}" ] || return 0
    for manifest in "$DIST_DIR"/lib*.json; do
        [ -f "$manifest" ] || continue
        name=$(basename "$manifest" .json)
        project_name="$name.c"
        if is_project "$PROJ_DIR/$project_name"; then
            continue
        fi
        PUBLISHED_SET=()
        prune_project_files "$project_name" "$manifest"
        rm -f "$manifest"
        CHANGED=1
    done
}

# Clean up legacy per-library directories after migrating local output.
# @return None.
prune_legacy_layout() {
    local directory
    [ -z "${KCDIST_DIR:-}" ] || return 0
    for directory in "$DIST_DIR"/lib*.c/; do
        [ -d "$directory" ] || continue
        [ -f "$directory/manifest.json" ] || continue
        rm -rf "$directory"
        CHANGED=1
    done
    rm -f "$DIST_DIR/.build_state"
}

# Build a single checksum manifest per target, including all libraries.
# Do not leave empty platform directories containing only shared metadata.
# @return None.
refresh_target_checksums() {
    local target_dir basename temporary file filename sha files
    for target_dir in "$DIST_DIR"/*-*/; do
        [ -d "$target_dir" ] || continue
        basename="${target_dir%/}"
        basename="${basename##*/}"
        is_platform "${basename%%-*}" || continue
        files=0
        for file in "$target_dir"/*; do
            [ -f "$file" ] || continue
            case "${file##*/}" in
                LICENSE|COPYING|package.json|SHA256SUMS) ;;
                *) files=$((files + 1)) ;;
            esac
        done
        if [ "$files" -eq 0 ]; then
            rm -f "$target_dir/LICENSE" "$target_dir/COPYING" \
                "$target_dir/package.json" "$target_dir/SHA256SUMS"
            rmdir "$target_dir" 2>/dev/null || true
            CHANGED=1
            continue
        fi

        temporary=$(mktemp "$target_dir/.checksum.XXXXXXXX")
        while IFS= read -r -d '' file; do
            filename="${file##*/}"
            case "$filename" in
                SHA256SUMS|.checksum.*|.dist.*) continue ;;
            esac
            sha=$(compute_sha256 "$file")
            printf '%s  %s\n' "$sha" "$filename"
        done < <(find "$target_dir" -maxdepth 1 -type f -print0 | LC_ALL=C sort -z) > "$temporary"
        if [ -f "$target_dir/SHA256SUMS" ] &&
            cmp -s "$temporary" "$target_dir/SHA256SUMS"; then
            rm -f "$temporary"
        else
            mv -f "$temporary" "$target_dir/SHA256SUMS"
            CHANGED=1
        fi
    done
}

# Distribute one project or every discovered project.
# @param target all or capability name.
# @return 0 on success.
main() {
    local target="${1:-all}" project_dir
    [ "$#" -le 1 ] || { usage; return 1; }
    [ -d "$PROJ_DIR" ] || {
        echo "error: projects directory not found: $PROJ_DIR" >&2
        return 1
    }
    case "$target" in
        all)
            mkdir -p "$DIST_DIR"
            prune_stale_projects
            for project_dir in "$PROJ_DIR"/*/; do
                is_project "$project_dir" || continue
                package_project "${project_dir%/}"
            done
            prune_legacy_layout
            ;;
        *)
            [[ "$target" =~ ^[A-Za-z0-9_-]+$ ]] || {
                echo "error: invalid project name: $target" >&2
                usage
                return 1
            }
            project_dir=$(resolve_project "$target") || {
                echo "error: project not found: $target" >&2
                return 1
            }
            mkdir -p "$DIST_DIR"
            package_project "$project_dir"
            ;;
    esac
    if [ "$CHANGED" -ne 0 ] || [ "$NEED_REFRESH" -ne 0 ]; then
        refresh_target_checksums
    fi
    if [ "$CHANGED" -eq 0 ]; then
        echo "Done. No distribution changes."
    else
        echo "Done. Distribution updated in $DIST_DIR/."
    fi
}

main "$@"
