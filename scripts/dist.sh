#!/bin/bash
# kclib dist tool
# Summary: Publishes library artifacts into dist with checksums and a manifest.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -e

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(dirname "$SCRIPT_DIR")
PROJ_DIR="$ROOT_DIR/proj"
DIST_DIR="$ROOT_DIR/dist"
readonly EXCLUDED_PROJECTS=("liblibr.c")
declare -A MANIFEST_SHA=()
declare -A MANIFEST_COUNT=()

# Prints command usage information.
# @return 0 on success.
usage() {
    echo "Usage: $0 all|NAME" >&2
}

# Returns the logical capability name for a libNAME.c project.
# @param project_name Project directory basename.
# @return 0 on success; the capability name is written to stdout.
capability_name() {
    local project_name="$1"
    local stem

    stem="${project_name%.c}"
    printf '%s\n' "${stem#lib}"
}

# Checks whether a project is excluded from distribution.
# @param project_name Project directory basename.
# @return 0 when excluded, 1 otherwise.
is_excluded() {
    local project_name="$1"
    local name

    for name in "${EXCLUDED_PROJECTS[@]}"; do
        if [ "$project_name" = "$name" ]; then
            return 0
        fi
    done
    return 1
}

# Checks whether a path is a current kclib project.
# @param project_path Project directory path.
# @return 0 when current, 1 otherwise.
is_project() {
    local project_path="$1"
    local project_name

    [ -d "$project_path" ] || return 1
    [ -f "$project_path/CMakeLists.txt" ] || return 1

    project_name=$(basename "$project_path")
    if is_excluded "$project_name"; then
        return 1
    fi

    return 0
}

# Resolves one logical capability name to its project directory.
# @param proj_dir Projects directory.
# @param name Logical capability name.
# @return 0 on success, 1 when the project does not exist.
resolve_project() {
    local proj_dir="$1"
    local name="$2"
    local project_dir

    project_dir="$proj_dir/lib$name.c"
    if is_project "$project_dir"; then
        printf '%s\n' "$project_dir"
        return 0
    fi

    return 1
}

# Loads artifact checksums from the current distribution manifest.
# @return 0 on success.
load_manifest_state() {
    local manifest sha path prefix

    MANIFEST_SHA=()
    MANIFEST_COUNT=()
    manifest="$DIST_DIR/manifest.json"
    [ -f "$manifest" ] || return 0

    while IFS=$'\t' read -r sha path; do
        [ -n "$sha" ] || continue
        [ -n "$path" ] || continue

        MANIFEST_SHA["$path"]="$sha"
        prefix="${path%/*}"
        MANIFEST_COUNT["$prefix"]=$((
            ${MANIFEST_COUNT["$prefix"]:-0} + 1
        ))
    done < <(
        awk '
            /"sha256":/ {
                sha = $0
                sub(/^.*"sha256": "/, "", sha)
                sub(/".*$/, "", sha)
            }
            /"path":/ {
                path = $0
                sub(/^.*"path": "/, "", path)
                sub(/".*$/, "", path)
                if (sha != "" && path != "") {
                    printf "%s\t%s\n", sha, path
                }
                sha = ""
            }
        ' "$manifest"
    )
}

# Removes checksum files left by the superseded dist layout.
# @return 0 on success.
remove_dist_checksum_files() {
    [ -d "$DIST_DIR" ] || return 0
    find "$DIST_DIR" -type f -name SHA256SUMS -delete
    rm -f "$DIST_DIR/.build_state"
}

# Checks whether one distributed target matches the build checksum.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 when current, 1 when changed, 2 when checksum is missing.
dist_target_is_current() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name relative prefix checksum dist_target
    local sha file path count expected_count

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    relative="${target_dir#"$project_dir/bin/"}"
    prefix="$project_name/$relative"
    checksum="$target_dir/SHA256SUMS"
    dist_target="$DIST_DIR/$prefix"

    if [ ! -f "$checksum" ]; then
        echo "error: build checksum not found: $checksum" >&2
        echo "run ./scripts/build.sh ${project_name#lib}" >&2
        return 2
    fi

    [ -d "$dist_target" ] || return 1
    [ -f "$dist_target/lib$name.h" ] || return 1

    count=0
    while read -r sha file; do
        [ -n "$sha" ] || continue
        [ -n "$file" ] || continue

        path="$prefix/$file"
        [ "${MANIFEST_SHA["$path"]:-}" = "$sha" ] || return 1
        [ -f "$DIST_DIR/$path" ] || return 1
        count=$((count + 1))
    done < "$checksum"

    expected_count=${MANIFEST_COUNT["$prefix"]:-0}
    [ "$count" -eq "$expected_count" ] || return 1
    [ "$count" -gt 0 ] || return 1

    return 0
}

# Copies one changed compiled target into dist.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 on success.
sync_target() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name header relative dist_target sha file

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"
    relative="${target_dir#"$project_dir/bin/"}"
    dist_target="$DIST_DIR/$project_name/$relative"

    if [ ! -f "$header" ]; then
        echo "error: public header not found: $header" >&2
        return 1
    fi

    rm -rf "$dist_target"
    mkdir -p "$dist_target"

    while read -r sha file; do
        [ -n "$sha" ] || continue
        [ -n "$file" ] || continue

        if [ ! -f "$target_dir/$file" ]; then
            echo "error: build artifact not found: $target_dir/$file" >&2
            return 1
        fi
        cp "$target_dir/$file" "$dist_target/$file"
    done < "$target_dir/SHA256SUMS"

    cp "$header" "$dist_target/lib$name.h"
}

# Removes distributed targets that no longer exist in bin.
# @param project_dir Project directory.
# @return 0 when unchanged, 1 when stale targets were removed.
prune_stale_targets() {
    local project_dir="$1"
    local project_name project_dist arch_dir platform_dir relative source_dir
    local changed

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    project_dist="$DIST_DIR/$project_name"
    [ -d "$project_dist" ] || return 0

    changed=0
    for arch_dir in "$project_dist"/*; do
        [ -d "$arch_dir" ] || continue
        for platform_dir in "$arch_dir"/*; do
            [ -d "$platform_dir" ] || continue
            relative="${platform_dir#"$project_dist/"}"
            source_dir="$project_dir/bin/$relative"

            if [ ! -d "$source_dir" ]; then
                rm -rf "$platform_dir"
                changed=1
            fi
        done
    done

    return "$changed"
}

# Synchronizes one project using build-generated target checksums.
# @param project_dir Project directory.
# @return 0 when changed, 1 when no work was needed.
package_project() {
    local project_dir="$1"
    local project_name target_dir status changed stale_changed

    project_dir="${project_dir%/}"
    [ -d "$project_dir/bin" ] || return 1
    project_name=$(basename "$project_dir")
    changed=0

    for target_dir in "$project_dir"/bin/*/*; do
        [ -d "$target_dir" ] || continue

        status=0
        dist_target_is_current "$project_dir" "$target_dir" || status=$?
        case "$status" in
            0)
                continue
                ;;
            1)
                if [ "$changed" -eq 0 ]; then
                    echo "Processing $project_name..."
                fi
                sync_target "$project_dir" "$target_dir"
                changed=1
                ;;
            *)
                return "$status"
                ;;
        esac
    done

    stale_changed=0
    if ! prune_stale_targets "$project_dir"; then
        stale_changed=1
    fi

    if [ "$changed" -eq 1 ] || [ "$stale_changed" -eq 1 ]; then
        echo "    [+] Distribution updated."
        return 0
    fi

    return 1
}

# Removes distributions for projects that no longer exist.
# @return 0 when unchanged, 1 when stale distributions were removed.
prune_stale_projects() {
    local project_dist project_name source_project changed

    changed=0
    for project_dist in "$DIST_DIR"/lib*.c/; do
        [ -d "$project_dist" ] || continue
        project_name=$(basename "$project_dist")
        source_project="$PROJ_DIR/$project_name"

        if ! is_project "$source_project"; then
            echo "Removing stale $project_name..."
            rm -rf "$project_dist"
            changed=1
        fi
    done

    return "$changed"
}

# Writes manifest.json from build-generated target checksums.
# @return 0 on success.
generate_manifest() {
    local manifest_file first_project first_binary updated_at
    local project_dir project_name project_dist target_dir checksum
    local relative arch platform sha file dist_file filesize

    manifest_file="$DIST_DIR/manifest.json"
    first_project=true
    updated_at=$(date -u +'%Y-%m-%dT%H:%M:%SZ')

    {
        echo "{"
        printf '  "updated_at": "%s",\n' "$updated_at"
        echo "  \"timestamp\": $(date -u +%s),"
        echo '  "projects": {'
    } > "$manifest_file"

    for project_dir in "$PROJ_DIR"/lib*.c/; do
        is_project "$project_dir" || continue
        project_dir="${project_dir%/}"
        project_name=$(basename "$project_dir")
        project_dist="$DIST_DIR/$project_name"
        [ -d "$project_dist" ] || continue

        if [ "$first_project" = true ]; then
            first_project=false
        else
            echo "," >> "$manifest_file"
        fi

        echo "    \"$project_name\": [" >> "$manifest_file"
        first_binary=true

        for target_dir in "$project_dir"/bin/*/*; do
            [ -d "$target_dir" ] || continue
            checksum="$target_dir/SHA256SUMS"
            if [ ! -f "$checksum" ]; then
                echo "error: build checksum not found: $checksum" >&2
                return 1
            fi

            relative="${target_dir#"$project_dir/bin/"}"
            IFS="/" read -r arch platform <<< "$relative"

            while read -r sha file; do
                [ -n "$sha" ] || continue
                [ -n "$file" ] || continue

                dist_file="$project_dist/$relative/$file"
                [ -f "$dist_file" ] || continue
                filesize=$(stat -c%s "$dist_file" 2>/dev/null || \
                    stat -f%z "$dist_file" 2>/dev/null)

                if [ "$first_binary" = true ]; then
                    first_binary=false
                else
                    echo "," >> "$manifest_file"
                fi

                cat <<MANIFEST_ENTRY >> "$manifest_file"
      {
        "arch": "$arch",
        "platform": "$platform",
        "binary": "$file",
        "size_bytes": $filesize,
        "sha256": "$sha",
        "path": "$project_name/$relative/$file"
      }
MANIFEST_ENTRY
            done < "$checksum"
        done

        printf '\n    ]' >> "$manifest_file"
    done

    printf '\n  }\n}\n' >> "$manifest_file"
}

# Dispatches one project distribution or all project distributions.
# @param target Logical capability name or all.
# @return 0 on success.
main() {
    local target project_dir status changed stale_changed

    [ "$#" -eq 1 ] || {
        usage
        exit 1
    }

    target="$1"
    [ -d "$PROJ_DIR" ] || {
        echo "error: projects directory not found: $PROJ_DIR" >&2
        exit 1
    }

    mkdir -p "$DIST_DIR"
    remove_dist_checksum_files
    load_manifest_state

    stale_changed=0
    if ! prune_stale_projects; then
        stale_changed=1
    fi

    if [ "$target" = "all" ]; then
        changed="$stale_changed"

        for project_dir in "$PROJ_DIR"/lib*.c/; do
            is_project "$project_dir" || continue

            status=0
            package_project "$project_dir" || status=$?
            case "$status" in
                0) changed=1 ;;
                1) ;;
                *) exit "$status" ;;
            esac
        done

        if [ "$changed" -eq 1 ] || [ ! -f "$DIST_DIR/manifest.json" ]; then
            echo "Generating manifest.json..."
            generate_manifest
            echo "Done. Distribution updated in $DIST_DIR/."
        else
            echo "Done. No distribution changes."
        fi
        return 0
    fi

    case "$target" in
        *[!A-Za-z0-9_-]*|'')
            echo "error: invalid project name: $target" >&2
            usage
            exit 1
            ;;
    esac

    project_dir=$(resolve_project "$PROJ_DIR" "$target") || {
        echo "error: project not found: $target" >&2
        exit 1
    }

    status=0
    package_project "$project_dir" || status=$?
    case "$status" in
        0) changed=1 ;;
        1) changed="$stale_changed" ;;
        *) exit "$status" ;;
    esac

    if [ "$changed" -eq 1 ] || [ ! -f "$DIST_DIR/manifest.json" ]; then
        echo "Generating manifest.json..."
        generate_manifest
        echo "Done. Distribution updated in" \
            "$DIST_DIR/$(basename "$project_dir")/."
    else
        echo "Done. No distribution changes."
    fi
}

main "$@"
