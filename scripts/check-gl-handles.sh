#!/usr/bin/env bash
# Every GL object in src/ is owned by a gfx/GlHandle.h type (#149). Fail if anything else
# creates or deletes one by hand. glGenerateMipmap is not an object allocation, so it is
# not matched; comments in vendored 3rdparty/ code are skipped.
#
# Second rule: GL is main-thread only. A job (core/JobSystem.h) produces CPU payloads and the
# main thread uploads them, so no gl* call and no GlHandle Create may appear inside the
# argument of a Jobs::Submit(...) call. (GlHandle also asserts this at run time in debug.)
set -euo pipefail
cd "$(dirname "$0")/.."
status=0
pattern='\bgl(Gen(Buffers|VertexArrays|Textures|Framebuffers|Renderbuffers|Queries|Samplers|TransformFeedbacks)|Delete(Buffers|VertexArrays|Textures|Framebuffers|Renderbuffers|Queries|Samplers|TransformFeedbacks|Shader|Program)|Create(Shader|Program))\b'
if hits=$(grep -rnE "$pattern" src --include='*.cpp' --include='*.h' \
        | grep -v '^src/gfx/GlHandle\.h:' \
        | grep -vE '^src/3rdparty/[^:]+:[0-9]+:\s*//'); then
    echo "Raw GL object allocation outside src/gfx/GlHandle.h — use GlBuffer/GlTexture/... instead:" >&2
    echo "$hits" >&2
    status=1
fi

if ! python3 - <<'PY'
import pathlib, re, sys
gl_call = re.compile(r'\bgl[A-Z]\w*\s*\(|\bGl[A-Z]\w*::Create\b')
failed = False
for path in sorted(pathlib.Path('src').rglob('*')):
    if path.suffix not in ('.cpp', '.h') or 'core/JobSystem' in str(path):
        continue
    text = path.read_text(encoding='utf-8', errors='replace')
    for match in re.finditer(r'\bJobs::Submit\s*\(', text):
        depth, i = 1, match.end()
        while i < len(text) and depth:
            depth += {'(': 1, ')': -1}.get(text[i], 0)
            i += 1
        body = text[match.end():i]
        # Ignore // comments inside the job body.
        body = re.sub(r'//[^\n]*', '', body)
        for call in gl_call.finditer(body):
            line = text.count('\n', 0, match.end() + call.start()) + 1
            print(f'{path}:{line}: {call.group(0).strip()} inside Jobs::Submit(...)', file=sys.stderr)
            failed = True
sys.exit(1 if failed else 0)
PY
then
    echo "GL call inside a job — build a payload in the job and upload it on the main thread (assets/UploadQueue.h)" >&2
    status=1
fi

[ "$status" -eq 0 ] && echo "GL handles: OK (no raw glGen*/glDelete* outside gfx/GlHandle.h, no GL inside Jobs::Submit)"
exit "$status"
