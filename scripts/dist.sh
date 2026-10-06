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

# Writes the expected checksum list for one built project.
# @param project_dir Project directory.
# @param output_file Destination checksum-list file.
# @return 0 on success.
write_expected_checksums() {
    local project_dir="$1"
    local output_file="$2"
    local project_name name header artifact rel_path target_dir
    local header_sha artifact_sha temp_file

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"

    if [ ! -f "$header" ]; then
        echo "error: public header not found: $header" >&2
        return 1
    fi

    temp_file=$(mktemp)
    header_sha=$(compute_sha256 "$header")

    while IFS= read -r -d '' artifact; do
        rel_path="${artifact#"$project_dir/bin/"}"
        artifact_sha=$(compute_sha256 "$artifact")
        printf '%s  %s\n' "$artifact_sha" "$rel_path" >> "$temp_file"

        target_dir=$(dirname "$rel_path")
        printf '%s  %s/lib%s.h\n' \
            "$header_sha" "$target_dir" "$name" >> "$temp_file"
    done < <(
        find "$project_dir/bin" -type f \
            -name "lib$name.*" \
            ! -name "*.sync-conflict-*" \
            -print0 | sort -z
    )

    LC_ALL=C sort -u -k2,2 "$temp_file" > "$output_file"
    rm -f "$temp_file"
}

# Writes checksums for the files currently present in one dist project.
# @param project_dist Project distribution directory.
# @param output_file Destination checksum-list file.
# @return 0 on success.
write_actual_checksums() {
    local project_dist="$1"
    local output_file="$2"
    local file rel_path sha256 temp_file

    temp_file=$(mktemp)
    if [ -d "$project_dist" ]; then
        while IFS= read -r -d '' file; do
            rel_path="${file#"$project_dist/"}"
            sha256=$(compute_sha256 "$file")
            printf '%s  %s\n' "$sha256" "$rel_path" >> "$temp_file"
        done < <(
            find "$project_dist" -type f \
                ! -name "SHA256SUMS" \
                -print0 | sort -z
        )
    fi

    LC_ALL=C sort -u -k2,2 "$temp_file" > "$output_file"
    rm -f "$temp_file"
}

# Checks whether one built project is already distributed unchanged.
# @param project_dir Project directory.
# @param expected_file Expected checksum-list file.
# @return 0 when up to date, 1 otherwise.
project_is_current() {
    local project_dir="$1"
    local expected_file="$2"
    local project_dist actual_file

    project_dist="$DIST_DIR/$(basename "$project_dir")"
    [ -d "$project_dist" ] || return 1

    actual_file=$(mktemp)
    write_actual_checksums "$project_dist" "$actual_file"

    if cmp -s "$expected_file" "$actual_file"; then
        rm -f "$actual_file"
        return 0
    fi

    rm -f "$actual_file"
    return 1
}

# Copies one project's artifacts and public header into dist.
# @param project_dir Project directory.
# @return 0 when artifacts changed, 1 when no work was needed.
package_project() {
    local project_dir="$1"
    local project_name project_dist name header target_dir target_header
    local artifact rel_path destination checksum_file expected_file

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    header="$project_dir/src/lib$name.h"

    [ -d "$project_dir/bin" ] || return 1

    expected_file=$(mktemp)
    write_expected_checksums "$project_dir" "$expected_file"

    if project_is_current "$project_dir" "$expected_file"; then
        checksum_file="$DIST_DIR/$project_name/SHA256SUMS"
        if [ ! -f "$checksum_file" ] || \
            ! cmp -s "$expected_file" "$checksum_file"; then
            cp "$expected_file" "$checksum_file"
        fi
        rm -f "$expected_file"
        echo "Processing $project_name..."
        echo "    ninja: no work to do."
        return 1
    fi

    if [ ! -f "$header" ]; then
        rm -f "$expected_file"
        echo "error: public header not found: $header" >&2
        return 2
    fi

    echo "Processing $project_name..."
    find "$project_dir/bin" -type f -name "*.sync-conflict-*" -delete \
        2>/dev/null || true

    project_dist="$DIST_DIR/$project_name"
    rm -rf "$project_dist"
    mkdir -p "$project_dist"

    while IFS= read -r -d '' artifact; do
        rel_path="${artifact#"$project_dir/bin/"}"
        destination="$project_dist/$rel_path"
        mkdir -p "$(dirname "$destination")"
        cp "$artifact" "$destination"
    done < <(
        find "$project_dir/bin" -type f -name "lib$name.*" -print0
    )

    while IFS= read -r -d '' target_dir; do
        target_header="$target_dir/lib$name.h"
        cp "$header" "$target_header"
    done < <(
        find "$project_dist" -mindepth 2 -maxdepth 2 -type d -print0
    )

    checksum_file="$project_dist/SHA256SUMS"
    cp "$expected_file" "$checksum_file"
    rm -f "$expected_file"

    echo "    [+] Artifacts collected in $project_dist/"
    return 0
}

# Removes distributions for project directories that no longer exist.
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
                SHA256SUMS|manifest.json) continue ;;
                *.h) continue ;;
            esac

            rel_path="${binary_path#"$project_dir/"}"
            sha256=$(awk -v path="$rel_path" \
                '$2 == path { print $1; exit }' "$project_sha_file")
            if [ -z "$sha256" ]; then
                sha256=$(compute_sha256 "$binary_path")
            fi

            IFS="/" read -r arch platform bin_name <<< "$rel_path"
            filesize=$(stat -c%s "$binary_path" 2>/dev/null || \
                stat -f%z "$binary_path" 2>/dev/null)

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
                ! -name "manifest.json" \
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
        0)
            changed=1
            ;;
        1)
            changed="$stale_changed"
            ;;
        *)
            exit "$status"
            ;;
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
