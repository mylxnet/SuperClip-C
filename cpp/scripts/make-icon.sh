#!/usr/bin/env bash
# SuperClip C++ · 应用图标产物生成（agent.md 三.5 / 七.6：打包素材与生成过程要可复现）
# 在 WSL 项目专属发行版 superclip 内运行：wsl -d superclip -- bash /mnt/e/qcode/superclip/cpp/scripts/make-icon.sh
# 依赖：ImageMagick（apt: imagemagick）。中间文件一律落 /tmp，不污染本地（agent.md 二.5）。
#
# 输入：cpp/src/res/icon-1024-source.png —— 1024×1024 底图（AI 生成，右下角带「Qoder AI 生成」水印）。
# 输出：cpp/src/res/SuperClip.ico —— 16/24/32/48/256 多尺寸（256 档为 PNG 压缩），供 app.rc 的 `101 ICON` 使用。
# 换图流程：替换 icon-1024-source.png → 跑本脚本 → 若水印位置变了，改 PATCH_* 四个数覆盖到水印外。
set -euo pipefail
cd "$(dirname "$0")/.."
RES=src/res
SRC=$RES/icon-1024-source.png
W=/tmp/sc-icon
# 覆盖水印的补丁：取左下角 240×180@0,844 水平镜像后贴到右下角（画面对称，接缝不可见）
PATCH_W=240; PATCH_H=180; PATCH_Y=844; PATCH_X_TO=784
# 方幅裁切：圆角白底板在 1024 画布上约占 x55..970 / y50..965
CROP=916x916+54+50

[[ -f $SRC ]] || { echo "缺底图 $SRC"; exit 127; }
command -v convert >/dev/null || { echo "缺 ImageMagick（apt-get install -y imagemagick）"; exit 127; }

rm -rf "$W"; mkdir -p "$W"
convert "$SRC" \( +clone -crop ${PATCH_W}x${PATCH_H}+0+${PATCH_Y} +repage -flop \) \
        -geometry +${PATCH_X_TO}+${PATCH_Y} -composite "$W/patched.png"
convert "$W/patched.png" -crop "$CROP" +repage "$W/base.png"
convert "$W/base.png" -define icon:auto-resize=256,48,32,24,16 -colorspace sRGB "$RES/SuperClip.ico"

identify "$RES/SuperClip.ico"
ls -l "$RES/SuperClip.ico"
# 小尺寸自检：把 16/24/32/48 各放大 6 倍拼一张图，肉眼核对轮廓是否还认得出来
for s in 16 24 32 48; do
  convert "$W/base.png" -resize "${s}x${s}" -filter Point -resize 600% -bordercolor red -border 1 "$W/sheet_$s.png"
done
montage "$W"/sheet_16.png "$W"/sheet_24.png "$W"/sheet_32.png "$W"/sheet_48.png \
        -tile 4x1 -geometry +6+6 -background gray "$W/legibility.png"
echo "小尺寸可读性图：$W/legibility.png（看完即弃；16 px 档已知偏糊，见整改清单 §6.4）"
echo "ICO_DONE"
