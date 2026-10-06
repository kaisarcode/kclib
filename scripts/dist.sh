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

# Computes the SHA-256 digest of one file.
# @param file File path.
# @return 0 on success; the digest is written to stdout.
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

# Writes the expected file list for one compiled target.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @param output_file Destination list file.
# @return 0 on success.
write_target_file_list() {
    local project_dir="$1"
    local target_dir="$2"
    local output_file="$3"
    local project_name name

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")

    {
        find "$target_dir" -maxdepth 1 -type f \
            -name "lib$name.*" \
            ! -name "*.sync-conflict-*" \
            -printf '%f\n'
        printf 'lib%s.h\n' "$name"
    } | LC_ALL=C sort -u > "$output_file"
}

# Checks whether a compiled target checksum is still current.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 when current, 1 otherwise.
target_checksum_is_current() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name header checksum current_list stored_list

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"
    checksum="$target_dir/SHA256SUMS"

    [ -f "$checksum" ] || return 1
    [ -f "$header" ] || return 1
    [ "$header" -ot "$checksum" ] || return 1

    if find "$target_dir" -maxdepth 1 -type f \
        -name "lib$name.*" \
        ! -name "*.sync-conflict-*" \
        -newer "$checksum" -print -quit | grep -q .; then
        return 1
    fi

    current_list=$(mktemp)
    stored_list=$(mktemp)
    write_target_file_list "$project_dir" "$target_dir" "$current_list"
    awk '{print $2}' "$checksum" | LC_ALL=C sort -u > "$stored_list"

    if cmp -s "$current_list" "$stored_list"; then
        rm -f "$current_list" "$stored_list"
        return 0
    fi

    rm -f "$current_list" "$stored_list"
    return 1
}

# Updates one compiled target checksum only when its inputs changed.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 on success.
update_target_checksum() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name header checksum temporary artifact
    local filename sha256

    if target_checksum_is_current "$project_dir" "$target_dir"; then
        return 0
    fi

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"
    checksum="$target_dir/SHA256SUMS"

    if [ ! -f "$header" ]; then
        echo "error: public header not found: $header" >&2
        return 1
    fi

    temporary=$(mktemp)
    while IFS= read -r -d '' artifact; do
        filename=$(basename "$artifact")
        sha256=$(compute_sha256 "$artifact")
        printf '%s  %s\n' "$sha256" "$filename" >> "$temporary"
    done < <(
        find "$target_dir" -maxdepth 1 -type f \
            -name "lib$name.*" \
            ! -name "*.sync-conflict-*" \
            -print0 | sort -z
    )

    sha256=$(compute_sha256 "$header")
    printf '%s  lib%s.h\n' "$sha256" "$name" >> "$temporary"
    LC_ALL=C sort -u -k2,2 "$temporary" > "$checksum"
    rm -f "$temporary"
}

# Checks whether one distributed target matches its source checksum.
# @param project_name Project directory basename.
# @param target_dir Compiled target directory.
# @return 0 when current, 1 otherwise.
dist_target_is_current() {
    local project_name="$1"
    local target_dir="$2"
    local relative dist_target checksum file

    relative="${target_dir#"$PROJ_DIR/$project_name/bin/"}"
    dist_target="$DIST_DIR/$project_name/$relative"
    checksum="$target_dir/SHA256SUMS"

    [ -f "$dist_target/SHA256SUMS" ] || return 1
    cmp -s "$checksum" "$dist_target/SHA256SUMS" || return 1

    while read -r _ file; do
        [ -f "$dist_target/$file" ] || return 1
    done < "$checksum"

    return 0
}

# Copies one changed compiled target into dist.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 on success.
sync_target() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name header relative dist_target artifact

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"
    relative="${target_dir#"$project_dir/bin/"}"
    dist_target="$DIST_DIR/$project_name/$relative"

    rm -rf "$dist_target"
    mkdir -p "$dist_target"

    while IFS= read -r -d '' artifact; do
        cp "$artifact" "$dist_target/"
    done < <(
        find "$target_dir" -maxdepth 1 -type f \
            -name "lib$name.*" \
            ! -name "*.sync-conflict-*" \
            -print0
    )

    cp "$header" "$dist_target/lib$name.h"
    cp "$target_dir/SHA256SUMS" "$dist_target/SHA256SUMS"
}

# Removes distributed targets that no longer exist in bin.
# @param project_dir Project directory.
# @return 0 when unchanged, 1 when stale targets were removed.
prune_stale_targets() {
    local project_dir="$1"
    local project_name project_dist arch_dir platform_dir relative source_dir
    local changed

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

# Writes the aggregate checksum list for one distributed project.
# @param project_dir Project directory.
# @return 0 on success.
generate_project_checksums() {
    local project_dir="$1"
    local project_name project_dist output checksum relative sha file

    project_name=$(basename "$project_dir")
    project_dist="$DIST_DIR/$project_name"
    output="$project_dist/SHA256SUMS"
    mkdir -p "$project_dist"
    : > "$output"

    while IFS= read -r -d '' checksum; do
        relative="${checksum#"$project_dist/"}"
        relative="${relative%/SHA256SUMS}"
        while read -r sha file; do
            printf '%s  %s/%s\n' "$sha" "$relative" "$file" >> "$output"
        done < "$checksum"
    done < <(
        find "$project_dist" -mindepth 3 -maxdepth 3 \
            -type f -name SHA256SUMS -print0 | sort -z
    )
}

# Synchronizes one project using per-target source checksums.
# @param project_dir Project directory.
# @return 0 when changed, 1 when no work was needed.
package_project() {
    local project_dir="$1"
    local project_name target_dir changed stale_changed

    [ -d "$project_dir/bin" ] || return 1
    project_name=$(basename "$project_dir")
    changed=0

    for target_dir in "$project_dir/bin"/*/*; do
        [ -d "$target_dir" ] || continue

        update_target_checksum "$project_dir" "$target_dir"
        if dist_target_is_current "$project_name" "$target_dir"; then
            continue
        fi

        if [ "$changed" -eq 0 ]; then
            echo "Processing $project_name..."
        fi
        sync_target "$project_dir" "$target_dir"
        changed=1
    done

    stale_changed=0
    if ! prune_stale_targets "$project_dir"; then
        stale_changed=1
    fi

    if [ "$changed" -eq 1 ] || [ "$stale_changed" -eq 1 ]; then
        generate_project_checksums "$project_dir"
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

# Writes manifest.json from the currently distributed projects.
# @return 0 on success.
generate_manifest() {
    local manifest_file first_project first_binary
    local project_dir project_name project_sha_file
    local binary_path filename rel_path arch platform bin_name
    local sha256 filesize updated_at

    manifest_file="$DIST_DIR/manifest.json"
    first_project=true
    updated_at=$(date -u +'%Y-%m-%dT%H:%M:%SZ')
    {
        echo "{"
        printf '  "updated_at": "%s",\n' "$updated_at"
        echo "  \"timestamp\": $(date -u +%s),"
        echo '  "projects": {'
    } > "$manifest_file"

    for project_dir in "$DIST_DIR"/lib*.c/; do
        [ -d "$project_dir" ] || continue
        project_name=$(basename "$project_dir")
        project_sha_file="$project_dir/SHA256SUMS"

        if [ "$first_project" = true ]; then
            first_project=false
        else
            echo "," >> "$manifest_file"
        fi

        echo "    \"$project_name\": [" >> "$manifest_file"
        first_binary=true

        while IFS= read -r -d '' binary_path; do
            filename=$(basename "$binary_path")
            case "$filename" in
                SHA256SUMS|*.h) continue ;;
            esac

            rel_path="${binary_path#"$project_dir/"}"
            sha256=$(awk -v path="$rel_path" \
                '$2 == path { print $1; exit }' "$project_sha_file")
            filesize=$(stat -c%s "$binary_path" 2>/dev/null || \
                stat -f%z "$binary_path" 2>/dev/null)

            IFS="/" read -r arch platform bin_name <<< "$rel_path"
            if [ "$first_binary" = true ]; then
                first_binary=false
            else
                echo "," >> "$manifest_file"
            fi

            cat <<MANIFEST_ENTRY >> "$manifest_file"
      {
        "arch": "$arch",
        "platform": "$platform",
        "binary": "$bin_name",
        "size_bytes": $filesize,
        "sha256": "$sha256",
        "path": "$project_name/$rel_path"
      }
MANIFEST_ENTRY
        done < <(
            find "$project_dir" -type f \
                ! -name "SHA256SUMS" \
                ! -name "*.h" \
                -print0 | sort -z
        )

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
    rm -f "$DIST_DIR/.build_state"

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
