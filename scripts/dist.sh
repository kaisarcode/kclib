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
CACHE_FILE="$DIST_DIR/.build_state"
readonly EXCLUDED_PROJECTS=("liblibr.c")

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

# Computes a digest of library artifacts and public headers.
# @param proj_dir Projects directory.
# @return 0 on success; the digest is written to stdout.
compute_fingerprint() {
    local proj_dir="$1"
    local hasher project_path project_name name header

    if command -v sha256sum >/dev/null 2>&1; then
        hasher=sha256sum
    else
        hasher=md5sum
    fi

    {
        for project_path in "$proj_dir"/lib*.c/; do
            [ -d "$project_path" ] || continue
            project_name=$(basename "$project_path")
            if is_excluded "$project_name"; then
                continue
            fi

            name=$(capability_name "$project_name")
            if [ -d "$project_path/bin" ]; then
                find "$project_path/bin" -type f                     -name "lib$name.*"                     ! -name "*.sync-conflict-*"                     -exec stat -c "%n-%s-%Y" {} + 2>/dev/null
            fi

            header="$project_path/src/lib$name.h"
            if [ -f "$header" ]; then
                stat -c "%n-%s-%Y" "$header" 2>/dev/null
            fi
        done
    } |
        sort |
        "$hasher" |
        awk '{print $1}'
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

# Copies library artifacts and public headers into the dist directory.
# @param proj_dir Projects directory.
# @param dist_dir Distribution directory.
# @return 0 on success.
collect_artifacts() {
    local proj_dir="$1"
    local dist_dir="$2"
    local project_path project_name project_dist name header target_dir target_header
    local artifact rel_path destination

    for project_path in "$proj_dir"/lib*.c/; do
        [ -d "$project_path" ] || continue
        project_name=$(basename "$project_path")
        if is_excluded "$project_name"; then
            continue
        fi
        if [ ! -d "$project_path/bin" ]; then
            continue
        fi

        echo "Processing $project_name..."
        find "$project_path/bin" -type f -name "*.sync-conflict-*" -delete 2>/dev/null || true
        project_dist="$dist_dir/$project_name"
        mkdir -p "$project_dist"

        name=$(capability_name "$project_name")
        while IFS= read -r -d '' artifact; do
            rel_path="${artifact#"$project_path/bin/"}"
            destination="$project_dist/$rel_path"
            mkdir -p "$(dirname "$destination")"
            cp "$artifact" "$destination"
        done < <(find "$project_path/bin" -type f -name "lib$name.*" -print0)

        header="$project_path/src/lib$name.h"
        if [ ! -f "$header" ]; then
            echo "error: public header not found: $header" >&2
            return 1
        fi

        while IFS= read -r -d '' target_dir; do
            target_header="$target_dir/lib$name.h"
            cp "$header" "$target_header"
        done < <(find "$project_dist" -mindepth 2 -maxdepth 2 -type d -print0)

        echo "    [+] Artifacts collected in $project_dist/"
    done

    return 0
}

# Writes manifest.json and per-project SHA256SUMS files.
# @param dist_dir Distribution directory.
# @return 0 on success.
generate_manifest() {
    local dist_dir="$1"
    local manifest_file first_project first_binary
    local project_dir project_name project_sha_file
    local binary_path filename rel_path arch platform bin_name
    local sha256 filesize updated_at

    manifest_file="$dist_dir/manifest.json"
    first_project=true
    updated_at=$(date -u +'%Y-%m-%dT%H:%M:%SZ')
    {
        echo "{"
        printf '  "updated_at": "%s",\n' "$updated_at"
        echo "  \"timestamp\": $(date -u +%s),"
        echo '  "projects": {'
    } > "$manifest_file"

    for project_dir in "$dist_dir"/*/; do
        [ -d "$project_dir" ] || continue
        project_name=$(basename "$project_dir")
        project_sha_file="$project_dir/SHA256SUMS"
        : > "$project_sha_file"

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
                SHA256SUMS|manifest.json|.build_state) continue ;;
            esac

            rel_path="${binary_path#"$project_dir"}"
            sha256=$(compute_sha256 "$binary_path")
            echo "$sha256  $rel_path" >> "$project_sha_file"

            case "$filename" in
                *.h) continue ;;
            esac

            IFS="/" read -r arch platform bin_name <<< "$rel_path"
            filesize=$(stat -c%s "$binary_path" 2>/dev/null || stat -f%z "$binary_path" 2>/dev/null)

            if [ "$first_binary" = true ]; then
                first_binary=false
            else
                echo "," >> "$manifest_file"
            fi

            cat <<EOF >> "$manifest_file"
      {
        "arch": "$arch",
        "platform": "$platform",
        "binary": "$bin_name",
        "size_bytes": $filesize,
        "sha256": "$sha256",
        "path": "$project_name/$rel_path"
      }
EOF
        done < <(find "$project_dir" -type f             ! -name "SHA256SUMS"             ! -name "manifest.json"             ! -name ".build_state"             -print0)

        printf '\n    ]' >> "$manifest_file"
    done

    printf '\n  }\n}\n' >> "$manifest_file"
}

# Stashes manually maintained files from the dist root before regeneration.
# @param dist_dir Distribution directory.
# @param stash_dir Temporary stash directory.
# @return 0 on success.
stash_root_files() {
    local dist_dir="$1"
    local stash_dir="$2"
    local name

    mkdir -p "$stash_dir"
    [ -d "$dist_dir" ] || return 0

    while IFS= read -r file; do
        name=$(basename "$file")
        cp -p "$file" "$stash_dir/$name"
    done < <(find "$dist_dir" -maxdepth 1 -type f         ! -name "manifest.json"         ! -name ".build_state")
}

# Restores manually maintained files into the dist root after regeneration.
# @param dist_dir Distribution directory.
# @param stash_dir Temporary stash directory.
# @return 0 on success.
restore_root_files() {
    local dist_dir="$1"
    local stash_dir="$2"

    find "$stash_dir" -maxdepth 1 -type f -exec cp -p {} "$dist_dir"/ \;
}

# Rebuilds the distribution directory when library artifacts change.
# @return 0 on success.
main() {
    local current_state prev_state stash_dir

    echo "Checking for artifact changes in $PROJ_DIR/..."
    current_state=$(compute_fingerprint "$PROJ_DIR")

    if [ -f "$CACHE_FILE" ]; then
        prev_state=$(cat "$CACHE_FILE")
        if [ "$current_state" = "$prev_state" ]; then
            echo "No changes detected in artifacts or public headers."
            echo "Done."
            return 0
        fi
    fi

    echo "Changes detected. Rebuilding $DIST_DIR/..."
    stash_dir=$(mktemp -d)
    stash_root_files "$DIST_DIR" "$stash_dir"

    rm -rf "$DIST_DIR"
    mkdir -p "$DIST_DIR"

    collect_artifacts "$PROJ_DIR" "$DIST_DIR"

    echo "Generating manifest.json and checksums..."
    generate_manifest "$DIST_DIR"

    restore_root_files "$DIST_DIR" "$stash_dir"
    rm -rf "$stash_dir"

    echo "$current_state" > "$CACHE_FILE"

    echo "Done. Fresh artifacts, SHA256SUMS and manifest.json ready in $DIST_DIR/"
}

main "$@"
