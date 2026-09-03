#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source_dir=$(cd "$script_dir/.." && pwd)
output_root="$source_dir/dist"
container_engine=${CONTAINER_ENGINE:-}
container_cache=${CONTAINER_CACHE:-}
flatpak_builder_state_dir=${FLATPAK_BUILDER_STATE_DIR:-}
use_mirrorimage=${USE_MIRRORIMAGE:-""}

usage() {
  echo "Usage: $0 {debian|ubuntu|fedora|arch|flatpak|all}" >&2
  exit 2
}

target=${1:-}
case "$target" in
  debian|ubuntu|fedora|arch|flatpak|all) ;;
  *) usage ;;
esac

if ! git -C "$source_dir" diff --quiet ||
   ! git -C "$source_dir" diff --cached --quiet; then
  echo "Refusing to package tracked changes that are not committed." >&2
  exit 1
fi

base_version=$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" "$source_dir/meson.build")
if [[ -z "$base_version" ]]; then
  echo "Unable to read the project version from meson.build." >&2
  exit 1
fi

short_commit=$(git -C "$source_dir" rev-parse --short=8 HEAD)
commit_date=$(TZ=UTC git -C "$source_dir" show -s --format=%cd --date=format:%Y%m%d HEAD)
release_tag=$(git -C "$source_dir" describe --tags --exact-match HEAD 2>/dev/null || true)

if [[ -n "$release_tag" ]]; then
  package_version=${release_tag#v}
  if [[ ! "$package_version" =~ ^[0-9][0-9A-Za-z.+~]*$ ]]; then
    echo "Tag '$release_tag' is not a supported package version." >&2
    exit 1
  fi

  build_label=$package_version
  flatpak_version=$package_version
  debian_version="${package_version}-1"
  ubuntu_version="${package_version}-1~ubuntu26.04.1"
  rpm_release=1
  arch_version=$package_version
else
  latest_tag=$(git -C "$source_dir" describe --tags --abbrev=0 2>/dev/null || true)
  if [[ -n "$latest_tag" ]]; then
    revision_count=$(git -C "$source_dir" rev-list --count "$latest_tag..HEAD")
  else
    revision_count=$(git -C "$source_dir" rev-list --count HEAD)
  fi

  snapshot="git${commit_date}.${short_commit}"
  package_version=$base_version
  build_label=$snapshot
  flatpak_version="${base_version}+${snapshot}"
  debian_version="${base_version}+${snapshot}-1"
  ubuntu_version="${base_version}+${snapshot}-1~ubuntu26.04.1"
  rpm_release="0.${snapshot}"
  arch_version="${base_version}.r${revision_count}.g${short_commit}"
fi

mkdir -p "$output_root"
work_dir=$(mktemp -d "$output_root/.packaging.XXXXXXXX")
cleanup() {
  rm -rf -- "$work_dir"
}
trap cleanup EXIT

mkdir -p "$work_dir/source"
git -C "$source_dir" archive --format=tar HEAD | tar -xf - -C "$work_dir/source"

if [[ -n "$release_tag" ]]; then
  sed -i \
    "0,/^[[:space:]]*version: '[^']*',\$/s//          version: '${package_version}',/" \
    "$work_dir/source/meson.build"
  archived_version=$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" \
    "$work_dir/source/meson.build")
  if [[ "$archived_version" != "$package_version" ]]; then
    echo "Unable to set archived project version to '$package_version'." >&2
    exit 1
  fi
fi

build_native() {
  local distro=$1
  local export_target="export-${distro}"
  local output_dir="$output_root/$distro/$build_label"
  local -a build_command
  local -a cache_args=()

  if [[ -z "$container_engine" ]]; then
    if command -v docker >/dev/null 2>&1; then
      container_engine=docker
    elif command -v podman >/dev/null 2>&1; then
      container_engine=podman
    else
      echo "Docker or Podman is required to build the ${distro} package." >&2
      exit 1
    fi
  elif [[ "$container_engine" != docker && "$container_engine" != podman ]]; then
    echo "CONTAINER_ENGINE must be either 'docker' or 'podman'." >&2
    exit 2
  elif ! command -v "$container_engine" >/dev/null 2>&1; then
    echo "The requested container engine '$container_engine' was not found." >&2
    exit 1
  fi

  build_command=("$container_engine" build)
  case "$container_cache" in
    "") ;;
    gha)
      if [[ "$container_engine" != docker ]] ||
         ! docker buildx version >/dev/null 2>&1; then
        echo "CONTAINER_CACHE=gha requires Docker Buildx." >&2
        exit 1
      fi
      build_command=(docker buildx build)
      cache_args=(
        --cache-from "type=gha,scope=ptyxis-${distro}"
        --cache-to "type=gha,mode=max,scope=ptyxis-${distro}"
      )
      ;;
    *)
      echo "Unsupported CONTAINER_CACHE value '$container_cache'." >&2
      exit 2
      ;;
  esac

  if [[ -n $use_mirrorimage ]]; then
    dockerfile="Dockerfile.ustclug"
  else
    dockerfile="Dockerfile"
  fi

  mkdir -p "$output_dir"
  "${build_command[@]}" \
    "${cache_args[@]}" \
    --file "$work_dir/source/packaging/$dockerfile" \
    --target "$export_target" \
    --build-arg "BASE_VERSION=$package_version" \
    --build-arg "DEBIAN_VERSION=$debian_version" \
    --build-arg "UBUNTU_VERSION=$ubuntu_version" \
    --build-arg "RPM_RELEASE=$rpm_release" \
    --build-arg "ARCH_VERSION=$arch_version" \
    --output "type=local,dest=$output_dir" \
    "$work_dir/source"
}

build_flatpak() {
  local flatpak_arch
  local bundle
  local debug_bundle
  local repo="$output_root/flatpak/repo"
  local -a state_args=()

  if ! flatpak info org.flatpak.Builder >/dev/null 2>&1 ||
     ! command -v flatpak >/dev/null 2>&1; then
    echo "flatpak and org.flatpak.Builder are required for the Flatpak bundle." >&2
    exit 1
  fi

  flatpak_arch=$(flatpak --default-arch)
  mkdir -p "$output_root/flatpak"
  bundle="$output_root/flatpak/ptyxis-${flatpak_version}-${flatpak_arch}.flatpak"
  debug_bundle="$output_root/flatpak/ptyxis-debug-${flatpak_version}-${flatpak_arch}.flatpak"

  if [[ -n "$flatpak_builder_state_dir" ]]; then
    mkdir -p "$flatpak_builder_state_dir"
    state_args+=(--state-dir="$flatpak_builder_state_dir")
  fi

  flatpak run org.flatpak.Builder \
    --user \
    --force-clean \
    --install-deps-from=flathub \
    --repo="$repo" \
    "${state_args[@]}" \
    "$work_dir/flatpak-build" \
    "$work_dir/source/packaging/flatpak/org.gnome.Ptyxis.json"
  flatpak build-bundle \
    "$repo" \
    "$bundle" \
    org.gnome.Ptyxis
  flatpak build-bundle \
    --runtime \
    "$repo" \
    "$debug_bundle" \
    org.gnome.Ptyxis.Debug
}

case "$target" in
  debian|ubuntu|fedora|arch)
    build_native "$target"
    ;;
  flatpak)
    build_flatpak
    ;;
  all)
    build_native debian
    build_native ubuntu
    build_native fedora
    build_native arch
    build_flatpak
    ;;
esac

echo "Artifacts written under $output_root"
