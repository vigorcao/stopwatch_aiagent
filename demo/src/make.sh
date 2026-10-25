#!/bin/zsh
# build: frames.js (sprite data URIs) -> colorfigures build.py -> inline frames.js
set -e
W=${0:A:h}; cd $W
python3 -I - "$W" <<'PY'
import base64, glob, os, re, sys, json
w = sys.argv[1]; fr = {}
for p in sorted(glob.glob(os.path.join(w, 'frames', '*.webp')), key=lambda p: (re.sub(r'_\d+\.webp$', '', p), int(re.search(r'_(\d+)\.webp$', p).group(1)))):
    clip = re.sub(r'_\d+$', '', os.path.basename(p)[:-5])
    fr.setdefault(clip, []).append('data:image/webp;base64,' + base64.b64encode(open(p, 'rb').read()).decode())
open(os.path.join(w, 'frames.js'), 'w').write('const PET_FR = ' + json.dumps(fr) + ';\n')
PY
cp ~/.claude/skills/colorfigures/assets/core.js core.js
python3 -I ~/.claude/skills/colorfigures/scripts/build.py stopwatch.html out.tmp.html
python3 -I - "$W" <<'PY'
import os, sys
w = sys.argv[1]; page = open(os.path.join(w, 'out.tmp.html')).read(); js = open(os.path.join(w, 'frames.js')).read()
tag = '<script src="frames.js"></script>'; assert tag in page
open(os.path.join(w, 'stopwatch-pet.html'), 'w').write(page.replace(tag, '<script>' + js + '</script>'))
PY
rm out.tmp.html; ls -la stopwatch-pet.html
