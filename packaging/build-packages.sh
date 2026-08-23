#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
source_dir=$(cd "$script_dir/.." && pwd)
output_root="$source_dir/dist"
container_engine=${CONTAINER_ENGINE:-}

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
latest_tag=$(git -C "$source_dir" describe --tags --abbrev=0 2>/dev/null || true)
if [[ -n "$latest_tag" ]]; then
  revision_count=$(git -C "$source_dir" rev-list --count "$latest_tag..HEAD")
else
  revision_count=$(git -C "$source_dir" rev-list --count HEAD)
fi

snapshot="git${commit_date}.${short_commit}"
debian_version="${base_version}+${snapshot}-1"
ubuntu_version="${base_version}+${snapshot}-1~ubuntu26.04.1"
rpm_release="0.${snapshot}"
arch_version="${base_version}.r${revision_count}.g${short_commit}"

work_dir=$(mktemp -d -t ptyxis-packaging.XXXXXXXX)
cleanup() {
  rm -rf -- "$work_dir"
}
trap cleanup EXIT

mkdir -p "$work_dir/source" "$output_root"
git -C "$source_dir" archive --format=tar HEAD | tar -xf - -C "$work_dir/source"

build_native() {
  local distro=$1
  local export_target="export-${distro}"
  local output_dir="$output_root/$distro/$snapshot"

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

  mkdir -p "$output_dir"
  "$container_engine" build \
    --file "$work_dir/source/packaging/Dockerfile" \
    --target "$export_target" \
    --build-arg "BASE_VERSION=$base_version" \
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
  local repo="$output_root/flatpak/repo"

  if ! command -v flatpak-builder >/dev/null 2>&1 ||
     ! command -v flatpak >/dev/null 2>&1; then
    echo "flatpak and flatpak-builder are required for the Flatpak bundle." >&2
    exit 1
  fi

  flatpak_arch=$(flatpak --default-arch)
  mkdir -p "$output_root/flatpak"
  bundle="$output_root/flatpak/ptyxis-${base_version}+${snapshot}-${flatpak_arch}.flatpak"

  flatpak-builder \
    --force-clean \
    --install-deps-from=flathub \
    --repo="$repo" \
    "$work_dir/flatpak-build" \
    "$work_dir/source/packaging/flatpak/org.gnome.Ptyxis.json"
  flatpak build-bundle \
    "$repo" \
    "$bundle" \
    org.gnome.Ptyxis
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
