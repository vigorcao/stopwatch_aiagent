#!/bin/sh
# Claude Code / Codex hook -> M5 Voice Code watch status.
# Usage: voicecode-hook.sh <state> [claude|gpt]
# state: thinking | working | permission | done | end (session closed)
#
# Reports "<state>\t<detail>\t<agent>\t<session>\t<reply>" to the voicecode helper over
# /tmp/voicecode.sock; the helper merges sessions, times them and keeps the
# watch updated. If the helper isn't running, writes to the watch directly.
# Never blocks or fails the agent.
export VOICECODE_STATE="$1"
export VOICECODE_AGENT="${2:-claude}"
export VOICECODE_DIR="$(dirname "$0")"
/usr/bin/python3 -c '
import json, os, socket, subprocess, sys
state, agent = os.environ["VOICECODE_STATE"], os.environ["VOICECODE_AGENT"]
try:
    hook = json.load(sys.stdin)
except Exception:
    hook = {}
detail = str(hook.get("tool_name") or "")[:40] if state in ("working", "permission") else ""
session = str(hook.get("session_id") or "default")
# What the watch shows on its details page (B): the reply when a turn ends,
# the question when the agent needs you. Tabs are the field separator.
reply = ""
def last_reply(path):
    # Fallback: the final assistant text in the session transcript (JSONL).
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 400000))
            lines = f.read().decode(errors="ignore").splitlines()
    except (OSError, TypeError):
        return ""
    for line in reversed(lines):
        try:
            d = json.loads(line)
        except ValueError:
            continue
        if d.get("type") != "assistant":
            continue
        content = d.get("message", {}).get("content", [])
        text = "".join(c.get("text", "") for c in content if isinstance(c, dict) and c.get("type") == "text")
        if text.strip():
            return text
    return ""
if state == "done":
    reply = hook.get("last_assistant_message") or last_reply(hook.get("transcript_path"))
    try:  # last Stop payload, for debugging the details page
        json.dump(hook, open("/tmp/voicecode-last-stop.json", "w"), ensure_ascii=False, indent=1)
    except OSError:
        pass
elif state == "permission":
    reply = hook.get("message") or ("Allow " + detail + "?" if detail else "")
reply = str(reply).replace("\t", " ").encode()[:1500].decode(errors="ignore")
msg = "\t".join([state, detail, agent, session, reply]).encode()
try:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    s.sendto(msg, "/tmp/voicecode.sock")
except OSError:
    if state == "end":
        sys.exit(0)
    subprocess.Popen([os.path.join(os.environ["VOICECODE_DIR"], "voicecode-status"), state, detail],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
' 2>/dev/null
exit 0
