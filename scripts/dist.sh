#!/bin/bash
# kclib dist tool
# Summary: Publishes library artifacts into dist with checksums and a manifest.
# Author:  KaisarCode
# Website: https://kaisarcode.com
# License: GNU Lesser General Public License v3.0

set -e

# Resolves the real script directory, including relative and chained symlinks.
# @return Absolute directory containing the script.
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
DIST_DIR="$ROOT_DIR/dist"
readonly EXCLUDED_PROJECTS=("liblibr.c")
declare -A MANIFEST_SHA=()
declare -A MANIFEST_BUILD_SHA=()

# Prints command usage information.
# @return 0 on success.
usage() {
    echo "Usage: $0 [all|NAME]" >&2
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

# Checks whether a directory name is a supported target platform.
# @param platform Directory name beneath bin/ or dist/libNAME.c/.
# @return 0 when recognized, 1 otherwise.
is_platform() {
    case "$1" in
        linux|windows|macos|ios|iossim|android|wasm) return 0 ;;
        *) return 1 ;;
    esac
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

# Returns the SHA-256 checksum of one file.
# @param file File path.
# @return Checksum on stdout.
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

# Maps one bin/<platform>/<arch> directory to its published ZIP path.
# @param project_dir Project directory.
# @param target_dir Built target directory.
# @return Archive path on stdout.
target_archive_path() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name relative platform arch

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    relative="${target_dir#"$project_dir/bin/"}"
    IFS="/" read -r platform arch <<< "$relative"
    printf '%s/%s/lib%s-%s-%s.zip\n' "$DIST_DIR" "$project_name" \
        "$name" "$platform" "$arch"
}

# Loads previous archive and build checksums for incremental publication.
# @return 0 on success.
load_manifest_state() {
    local manifest sha build_sha path

    MANIFEST_SHA=()
    MANIFEST_BUILD_SHA=()
    manifest="$DIST_DIR/manifest.json"
    [ -f "$manifest" ] || return 0

    while IFS=$'\t' read -r sha build_sha path; do
        [ -n "$sha" ] && [ -n "$path" ] || continue
        MANIFEST_SHA["$path"]="$sha"
        MANIFEST_BUILD_SHA["$path"]="$build_sha"
    done < <(
        awk '
            /"sha256":/ {
                sha = $0
                sub(/^.*"sha256": "/, "", sha)
                sub(/".*$/, "", sha)
            }
            /"build_sha256":/ {
                build_sha = $0
                sub(/^.*"build_sha256": "/, "", build_sha)
                sub(/".*$/, "", build_sha)
            }
            /"path":/ {
                path = $0
                sub(/^.*"path": "/, "", path)
                sub(/".*$/, "", path)
                if (sha != "" && path != "") {
                    printf "%s\t%s\t%s\n", sha, build_sha, path
                }
                sha = ""
                build_sha = ""
            }
        ' "$manifest"
    )
}

# Removes metadata from the former, unpacked distribution layout.
# @return 0 on success.
remove_legacy_metadata() {
    [ -d "$DIST_DIR" ] || return 0
    rm -f "$DIST_DIR/.build_state"
}

# Checks whether the existing ZIP matches both its source build and manifest.
# @param project_dir Project directory.
# @param target_dir Built target directory.
# @return 0 when current, 1 when changed, 2 when checksum is missing.
dist_target_is_current() {
    local project_dir="$1"
    local target_dir="$2"
    local archive key checksum build_sha archive_sha

    project_dir="${project_dir%/}"
    checksum="$target_dir/SHA256SUMS"
    if [ ! -f "$checksum" ]; then
        echo "error: build checksum not found: $checksum" >&2
        echo "run ./scripts/build.sh $(capability_name "$(basename "$project_dir")")" >&2
        return 2
    fi

    archive=$(target_archive_path "$project_dir" "$target_dir")
    key="${archive#"$DIST_DIR/"}"
    [ -s "$archive" ] || return 1

    build_sha=$(compute_sha256 "$checksum") || return 2
    [ "${MANIFEST_BUILD_SHA["$key"]:-}" = "$build_sha" ] || return 1

    archive_sha=$(compute_sha256 "$archive") || return 2
    [ "${MANIFEST_SHA["$key"]:-}" = "$archive_sha" ]
}

# Packages one target as libNAME-<platform>-<arch>.zip containing libNAME/.
# @param project_dir Project directory.
# @param target_dir Built target directory.
# @return 0 on success, 1 on failure.
sync_target() {
    local project_dir="$1"
    local target_dir="$2"
    local project_name name archive staging stage_dir file sha count

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    archive=$(target_archive_path "$project_dir" "$target_dir")
    mkdir -p "$(dirname "$archive")" || return 1
    staging=$(mktemp -d "$(dirname "$archive")/.dist.XXXXXXXX") || return 1
    stage_dir="$staging/lib$name"
    if ! mkdir -p "$stage_dir"; then
        rm -rf "$staging"
        return 1
    fi

    count=0
    : > "$stage_dir/SHA256SUMS"
    while read -r sha file; do
        [ -n "$sha" ] && [ -n "$file" ] || continue
        case "$file" in
            *.h) continue ;;
            "lib$name."*|"$name"|"$name.exe") ;;
            *)
                echo "error: unexpected build artifact: $file" >&2
                rm -rf "$staging"
                return 1
                ;;
        esac
        if [ ! -f "$target_dir/$file" ]; then
            echo "error: build artifact not found: $target_dir/$file" >&2
            rm -rf "$staging"
            return 1
        fi
        if ! cp "$target_dir/$file" "$stage_dir/$file"; then
            rm -rf "$staging"
            return 1
        fi
        printf '%s  %s\n' "$sha" "$file" >> "$stage_dir/SHA256SUMS"
        count=$((count + 1))
    done < "$target_dir/SHA256SUMS"

    if [ "$count" -eq 0 ]; then
        echo "error: no build artifacts to package: $target_dir" >&2
        rm -rf "$staging"
        return 1
    fi

    if ! (cd "$staging" && zip -q -X -r package.zip "lib$name"); then
        rm -rf "$staging"
        return 1
    fi
    if ! mv -f "$staging/package.zip" "$archive"; then
        rm -rf "$staging"
        return 1
    fi
    rm -rf "$staging"
    echo "    [+] $(basename "$archive")"
}

# Removes unpacked distribution directories and ZIPs with no source target.
# @param project_dir Project directory.
# @return 0 when unchanged, 1 when stale targets were removed.
prune_stale_targets() {
    local project_dir="$1"
    local project_name name project_dist old_dir archive tail platform arch
    local changed

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")
    name=$(capability_name "$project_name")
    project_dist="$DIST_DIR/$project_name"
    [ -d "$project_dist" ] || return 0

    changed=0
    for old_dir in "$project_dist"/*/; do
        [ -d "$old_dir" ] || continue
        rm -rf "$old_dir"
        changed=1
    done

    for archive in "$project_dist"/lib"$name"-*.zip; do
        [ -f "$archive" ] || continue
        tail="${archive##*/}"
        tail="${tail#lib$name-}"
        tail="${tail%.zip}"
        platform="${tail%%-*}"
        arch="${tail#*-}"
        if ! is_platform "$platform" ||
            [ "$arch" = "$tail" ] ||
            [ ! -d "$project_dir/bin/$platform/$arch" ]; then
            rm -f "$archive"
            changed=1
        fi
    done

    return "$changed"
}

# Synchronizes one project using build-generated artifact checksums.
# @param project_dir Project directory.
# @return 0 when changed, 1 when no work was needed.
package_project() {
    local project_dir="$1"
    local project_name target_dir relative platform status changed stale_changed

    project_dir="${project_dir%/}"
    project_name=$(basename "$project_dir")

    echo "Processing $project_name..."
    changed=0

    if [ -d "$project_dir/bin" ]; then
        for target_dir in "$project_dir"/bin/*/*; do
            [ -d "$target_dir" ] || continue
            relative="${target_dir#"$project_dir/bin/"}"
            platform="${relative%%/*}"
            is_platform "$platform" || continue

            status=0
            dist_target_is_current "$project_dir" "$target_dir" || status=$?
            case "$status" in
                0) continue ;;
                1)
                    sync_target "$project_dir" "$target_dir" || return $?
                    changed=1
                    ;;
                *) return "$status" ;;
            esac
        done
    else
        echo "    [=] No build artifacts."
    fi

    stale_changed=0
    if ! prune_stale_targets "$project_dir"; then
        stale_changed=1
    fi

    if [ "$changed" -eq 1 ] || [ "$stale_changed" -eq 1 ]; then
        echo "    [+] Distribution updated."
        return 0
    fi

    echo "    [=] Distribution current."
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

# Builds the manifest from actual ZIP archives (one entry per platform/arch).
# @return 0 on success.
generate_manifest() {
    local manifest_file first_project first_archive updated_at
    local project_dir project_name project_dist name archive archive_name
    local tail platform arch target_dir checksum build_sha zip_sha filesize

    manifest_file=$(mktemp "$DIST_DIR/.manifest.XXXXXXXX") || return 1
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
        name=$(capability_name "$project_name")
        project_dist="$DIST_DIR/$project_name"
        [ -d "$project_dist" ] || continue

        if [ "$first_project" = true ]; then
            first_project=false
        else
            echo "," >> "$manifest_file"
        fi
        echo "    \"$project_name\": [" >> "$manifest_file"
        first_archive=true

        for archive in "$project_dist"/lib"$name"-*.zip; do
            [ -f "$archive" ] || continue
            archive_name=$(basename "$archive")
            tail="${archive_name#lib$name-}"
            tail="${tail%.zip}"
            platform="${tail%%-*}"
            arch="${tail#*-}"
            is_platform "$platform" || continue
            target_dir="$project_dir/bin/$platform/$arch"
            [ -d "$target_dir" ] || continue
            checksum="$target_dir/SHA256SUMS"
            if [ ! -f "$checksum" ]; then
                echo "error: build checksum not found: $checksum" >&2
                rm -f "$manifest_file"
                return 1
            fi

            build_sha=$(compute_sha256 "$checksum") || {
                rm -f "$manifest_file"
                return 1
            }
            zip_sha=$(compute_sha256 "$archive") || {
                rm -f "$manifest_file"
                return 1
            }
            filesize=$(stat -c%s "$archive" 2>/dev/null ||
                stat -f%z "$archive" 2>/dev/null) || {
                rm -f "$manifest_file"
                return 1
            }

            if [ "$first_archive" = true ]; then
                first_archive=false
            else
                echo "," >> "$manifest_file"
            fi
            cat <<MANIFEST_ENTRY >> "$manifest_file"
      {
        "arch": "$arch",
        "platform": "$platform",
        "archive": "$archive_name",
        "size_bytes": $filesize,
        "sha256": "$zip_sha",
        "build_sha256": "$build_sha",
        "path": "$project_name/$archive_name"
      }
MANIFEST_ENTRY
        done

        printf '\n    ]' >> "$manifest_file"
    done

    printf '\n  }\n}\n' >> "$manifest_file"
    mv -f "$manifest_file" "$DIST_DIR/manifest.json"
}

# Dispatches one project distribution or all project distributions.
# @param target Logical capability name or all.
# @return 0 on success.
main() {
    local target project_dir status changed stale_changed

    [ "$#" -le 1 ] || {
        usage
        exit 1
    }

    target="${1:-all}"
    [ -d "$PROJ_DIR" ] || {
        echo "error: projects directory not found: $PROJ_DIR" >&2
        exit 1
    }

    mkdir -p "$DIST_DIR"
    if ! command -v zip >/dev/null 2>&1; then
        echo "error: zip command not found (install zip)" >&2
        return 1
    fi

    remove_legacy_metadata
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
