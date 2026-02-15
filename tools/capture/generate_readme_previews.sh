#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${ROOT_DIR}/assets/previews"
DEMO_BIN="${ROOT_DIR}/build/engine/vividvision_demo"

SCREEN_INDEX="${1:-4}"
DURATION_SEC="${2:-5}"
FPS_CAPTURE=24
FPS_GIF=14
CROP_W=1800
CROP_H=1200
GIF_W=900

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg not found"
  exit 1
fi

if [[ ! -x "${DEMO_BIN}" ]]; then
  echo "demo binary not found: ${DEMO_BIN}"
  echo "build first: cmake --build build -j"
  exit 1
fi

mkdir -p "${OUT_DIR}"

capture_one() {
  local model_path="$1"
  local out_name="$2"
  local mp4_path="${OUT_DIR}/${out_name}.mp4"
  local gif_path="${OUT_DIR}/${out_name}.gif"
  local palette_path="${OUT_DIR}/${out_name}_palette.png"

  echo "[capture] ${model_path} -> ${gif_path}"

  "${DEMO_BIN}" "${model_path}" >/dev/null 2>&1 &
  local demo_pid=$!
  sleep 2

  ffmpeg -y -f avfoundation -framerate "${FPS_CAPTURE}" -i "${SCREEN_INDEX}:none" -t "${DURATION_SEC}" "${mp4_path}" >/dev/null 2>&1

  kill "${demo_pid}" >/dev/null 2>&1 || true
  wait "${demo_pid}" 2>/dev/null || true

  ffmpeg -y -i "${mp4_path}" \
    -vf "fps=${FPS_GIF},crop=${CROP_W}:${CROP_H}:(in_w-${CROP_W})/2:(in_h-${CROP_H})/2,scale=${GIF_W}:-1:flags=lanczos,palettegen=max_colors=128" \
    "${palette_path}" >/dev/null 2>&1

  ffmpeg -y -i "${mp4_path}" -i "${palette_path}" \
    -lavfi "fps=${FPS_GIF},crop=${CROP_W}:${CROP_H}:(in_w-${CROP_W})/2:(in_h-${CROP_H})/2,scale=${GIF_W}:-1:flags=lanczos[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=5" \
    "${gif_path}" >/dev/null 2>&1

  rm -f "${palette_path}" "${mp4_path}"
}

capture_one "${ROOT_DIR}/assets/fbx/Hip_Hop_Dancing.fbx" "hip_hop_dancing"
capture_one "${ROOT_DIR}/assets/fbx/Taunt.fbx" "taunt"

echo "[done] generated:"
ls -lh "${OUT_DIR}/hip_hop_dancing.gif" "${OUT_DIR}/taunt.gif"
