#!/bin/bash
# kclib link tool
# Summary: Creates or refreshes symlinks for kclib CLI binaries in a target bin directory.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU General Public License v3.0

set -e

LINKED_NAMES=""
CREATED=0
REPLACED=0
SKIPPED=0
REMOVED=0

# Creates symlinks for every project binary in the destination bin directory.
# @param proj_dir Projects directory.
# @param bin_dir Destination directory.
# @param excluded Space-separated project names to skip.
# @return 0 on success.
link_projects() {
    local proj_dir="$1"
    local bin_dir="$2"
    local excluded="$3"
    local project_dir project_name name source_file target existing

    for project_dir in "$proj_dir"/*/; do
        project_name=$(basename "$project_dir")
        case "$project_name" in
            *.c) ;;
            *) continue ;;
        esac
        if [ -n "$excluded" ] && [[ " $excluded " == *" $project_name "* ]]; then
            continue
        fi
        name="${project_name%.c}"
        source_file="${project_dir%/}/bin/x86_64/linux/$name"
        if [ ! -f "$source_file" ]; then
            echo "    [..] skipped $name: no binary at $source_file" >&2
            SKIPPED=$((SKIPPED + 1))
            continue
        fi
        target="$bin_dir/$name"
        if [ -L "$target" ]; then
            existing=$(readlink "$target")
            case "$existing" in
                "$proj_dir"/*)
                    rm -f "$target"
                    ln -s "$source_file" "$target"
                    REPLACED=$((REPLACED + 1))
                    echo "    [~] replaced $name with $source_file"
                    ;;
                *)
                    echo "    [!!] skipped $name: $target points to $existing" >&2
                    SKIPPED=$((SKIPPED + 1))
                    ;;
            esac
        elif [ -e "$target" ]; then
            echo "    [!!] skipped $name: $target exists and is not a symlink" >&2
            SKIPPED=$((SKIPPED + 1))
        else
            ln -s "$source_file" "$target"
            CREATED=$((CREATED + 1))
            echo "    [+] created $name with $source_file"
        fi
        LINKED_NAMES="$LINKED_NAMES $name"
    done

    return 0
}

# Removes managed symlinks whose project is no longer linked.
# @param proj_dir Projects directory.
# @param bin_dir Destination directory.
# @return 0 on success.
cleanup_stale() {
    local proj_dir="$1"
    local bin_dir="$2"
    local link name target

    for link in "$bin_dir"/*; do
        [ -L "$link" ] || continue
        name=$(basename "$link")
        target=$(readlink "$link")
        case "$target" in
            "$proj_dir"/*) ;;
            *) continue ;;
        esac
        if [[ " $LINKED_NAMES " != *" $name "* ]]; then
            rm -f "$link"
            REMOVED=$((REMOVED + 1))
            echo "    [-] removed stale link $name"
        fi
    done

    return 0
}

# Runs the link and cleanup steps and reports a summary.
# @return 0 on success.
main() {
    local script_dir root_dir proj_dir bin_dir excluded

    script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
    root_dir=$(dirname "$script_dir")
    proj_dir="$root_dir/proj"
    bin_dir="${1:-$HOME/bin}"
    excluded="libr.c"

    echo "Linking kclib CLI binaries into $bin_dir"
    if [ ! -d "$bin_dir" ]; then
        echo "Creating $bin_dir"
        mkdir -p "$bin_dir"
    fi

    link_projects "$proj_dir" "$bin_dir" "$excluded"

    echo "Removing stale kclib links in $bin_dir"
    cleanup_stale "$proj_dir" "$bin_dir"

    echo "Done. created=$CREATED replaced=$REPLACED skipped=$SKIPPED removed=$REMOVED"
}

main "$@"