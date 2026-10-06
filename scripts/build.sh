#!/bin/bash
# kclib build tool
# Summary: Builds one or all kclib projects with make all.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -e

# Prints command usage information.
# @return 0 on success.
usage() {
    echo "Usage: $0 all|NAME" >&2
}

# Resolves one logical kclib name to its project directory.
# @param proj_dir Projects directory.
# @param name Logical kclib name.
# @return 0 on success, 1 when the project does not exist.
resolve_project() {
    local proj_dir="$1"
    local name="$2"

    if [ -d "$proj_dir/lib$name.c" ]; then
        printf '%s\n' "$proj_dir/lib$name.c"
        return 0
    fi

    return 1
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

# Writes the artifact file list for one compiled target.
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

    find "$target_dir" -maxdepth 1 -type f \
        -name "lib$name.*" \
        ! -name "*.sync-conflict-*" \
        -printf '%f\n' | LC_ALL=C sort -u > "$output_file"
}

# Checks whether one target checksum is still current.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 when current, 1 otherwise.
target_checksum_is_current() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name checksum current_list stored_list

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    checksum="$target_dir/SHA256SUMS"

    [ -f "$checksum" ] || return 1

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

# Updates one target checksum only when its artifacts changed.
# @param project_dir Project directory.
# @param target_dir Compiled target directory.
# @return 0 on success.
update_target_checksum() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name checksum temporary artifact
    local filename sha256

    if target_checksum_is_current "$project_dir" "$target_dir"; then
        return 0
    fi

    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    checksum="$target_dir/SHA256SUMS"
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

    LC_ALL=C sort -u -k2,2 "$temporary" > "$checksum"
    rm -f "$temporary"
}

# Updates checksums for all compiled targets of one project.
# @param project_dir Project directory.
# @return 0 on success.
update_project_checksums() {
    local project_dir="$1"
    local target_dir

    project_dir="${project_dir%/}"
    [ -d "$project_dir/bin" ] || return 0

    for target_dir in "$project_dir"/bin/*/*; do
        [ -d "$target_dir" ] || continue
        update_target_checksum "$project_dir" "$target_dir"
    done
}

# Builds one kclib project for every configured target.
# @param project_dir Project directory.
# @return 0 on success.
build_project() {
    local project_dir="$1"
    local project_name status

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    echo "Building $project_name..."

    status=0
    (
        cd "$project_dir"
        make all
    ) || status=$?

    if [ "$status" -ne 0 ]; then
        return "$status"
    fi

    update_project_checksums "$project_dir"
}

# Dispatches one project build or all project builds.
# @param target Logical kclib name or all.
# @return 0 on success.
main() {
    local script_dir root_dir proj_dir target project_dir project_name
    local failed_projects

    [ "$#" -eq 1 ] || {
        usage
        exit 1
    }

    script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
    root_dir=$(dirname "$script_dir")
    proj_dir="$root_dir/proj"
    target="$1"

    [ -d "$proj_dir" ] || {
        echo "error: projects directory not found: $proj_dir" >&2
        exit 1
    }

    if [ "$target" = "all" ]; then
        failed_projects=""
        for project_dir in "$proj_dir"/lib*.c; do
            [ -d "$project_dir" ] || continue
            if ! build_project "$project_dir"; then
                project_name=$(basename "$project_dir")
                failed_projects="$failed_projects $project_name"
            fi
        done

        if [ -n "$failed_projects" ]; then
            echo "error: failed projects:$failed_projects" >&2
            return 1
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

    project_dir=$(resolve_project "$proj_dir" "$target") || {
        echo "error: project not found: $target" >&2
        exit 1
    }

    build_project "$project_dir"
}

main "$@"
