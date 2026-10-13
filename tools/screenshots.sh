#!/bin/sh
# Capture every tile (and edit mode of adjustable ones) from the device as masked PNGs.
# Usage: tools/screenshots.sh <device-ip> [out-dir] [tile-count]
# Needs curl, ImageMagick (magick) and sips/ImageMagick for conversion.
set -e
IP=${1:?usage: screenshots.sh <device-ip> [out-dir] [tile-count]}
OUT=${2:-docs/ui-upgrade/screenshots}
N=${3:-4}
mkdir -p "$OUT"
MASK=$(mktemp -t mask).png
magick -size 390x390 xc:black -fill white -draw "circle 195,195 195,0" "$MASK"

shot() { # name query
    curl -s "http://$IP/tile?$2" >/dev/null
    sleep 2
    curl -s "http://$IP/screenshot.bmp" -o "$OUT/$1.bmp"
    magick "$OUT/$1.bmp" "$MASK" -alpha off -compose multiply -composite "$OUT/$1.png"
    rm "$OUT/$1.bmp"
    echo "$OUT/$1.png"
}

i=0
while [ "$i" -lt "$N" ]; do
    shot "$i" "i=$i"
    # edit-mode capture is ignored by the device for non-adjustable tiles
    shot "${i}_edit" "i=$i&edit=1"
    i=$((i + 1))
done
curl -s "http://$IP/tile?i=0" >/dev/null
rm -f "$MASK"
