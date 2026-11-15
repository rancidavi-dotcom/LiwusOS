#!/usr/bin/env python3
"""
LiwusTube Bridge - Servidor Relay Local de Alta Performance para LiwusOS
Permite pesquisar e assistir videos do YouTube no LiwusOS sem bloqueios de IP,
sem sobrecarga de TLS e com streaming progressivo MP4 360p confiavel.
"""

import sys
import os
import re
import json
import shutil
import urllib.request
import urllib.parse
from http.server import HTTPServer, BaseHTTPRequestHandler
import subprocess

PORT = int(os.environ.get("LIWUSTUBE_PORT", sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].isdigit() else 8085))
CACHE_STREAMS = {}
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS_DIR = os.path.join(BASE_DIR, "tools")

def get_yt_dlp_path():
    # 1. tools/yt-dlp
    local_tool = os.path.join(TOOLS_DIR, "yt-dlp")
    if os.path.isfile(local_tool) and os.access(local_tool, os.X_OK):
        return local_tool
    # 2. /tmp/yt-dlp
    tmp_tool = "/tmp/yt-dlp"
    if os.path.isfile(tmp_tool) and os.access(tmp_tool, os.X_OK):
        return tmp_tool
    # 3. system PATH
    sys_path = shutil.which("yt-dlp")
    if sys_path:
        return sys_path
    # 4. Auto-download to tools/yt-dlp
    print("[LiwusTube Bridge] Baixando binario standalone yt-dlp...")
    os.makedirs(TOOLS_DIR, exist_ok=True)
    url = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp"
    try:
        urllib.request.urlretrieve(url, local_tool)
        os.chmod(local_tool, 0o755)
        print(f"[LiwusTube Bridge] yt-dlp instalado com sucesso em {local_tool}")
        return local_tool
    except Exception as e:
        print(f"[LiwusTube Bridge] Erro ao baixar yt-dlp: {e}")
        return None

def get_js_runtime_arg():
    node = shutil.which("node") or "/usr/bin/node"
    if os.path.isfile(node) and os.access(node, os.X_OK):
        return ["--js-runtimes", f"node:{node}"]
    deno = shutil.which("deno")
    if deno:
        return ["--js-runtimes", f"deno:{deno}"]
    return []

def search_youtube(query):
    encoded = urllib.parse.quote_plus(query)
    url = f"https://www.youtube.com/results?search_query={encoded}"
    req = urllib.request.Request(
        url,
        headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}
    )
    try:
        with urllib.request.urlopen(req, timeout=8) as resp:
            html = resp.read().decode("utf-8", errors="replace")
    except Exception as e:
        print(f"[LiwusTube Bridge] Erro na busca: {e}")
        return []

    match = re.search(r"ytInitialData\s*=\s*({.+?});", html)
    if not match:
        return []

    try:
        data = json.loads(match.group(1))
    except Exception:
        return []

    results = []
    def walk(obj):
        if isinstance(obj, dict):
            if "videoRenderer" in obj:
                vr = obj["videoRenderer"]
                vid = vr.get("videoId")
                title_runs = vr.get("title", {}).get("runs", [])
                title = title_runs[0].get("text", "") if title_runs else ""
                owner_runs = vr.get("ownerText", {}).get("runs", [])
                owner = owner_runs[0].get("text", "") if owner_runs else ""
                dur_text = vr.get("lengthText", {}).get("simpleText", "")
                dur_sec = 0
                if dur_text:
                    parts = [int(p) for p in dur_text.split(":") if p.isdigit()]
                    if len(parts) == 3:
                        dur_sec = parts[0] * 3600 + parts[1] * 60 + parts[2]
                    elif len(parts) == 2:
                        dur_sec = parts[0] * 60 + parts[1]
                if vid and title:
                    results.append({
                        "id": vid,
                        "url": f"/watch?v={vid}",
                        "videoId": vid,
                        "title": title,
                        "author": owner,
                        "uploaderName": owner,
                        "duration": dur_sec
                    })
            for v in obj.values():
                walk(v)
        elif isinstance(obj, list):
            for it in obj:
                walk(it)

    walk(data)
    return results

def resolve_stream(video_id):
    if video_id in CACHE_STREAMS:
        return CACHE_STREAMS[video_id]

    yt_dlp = get_yt_dlp_path()
    if not yt_dlp:
        return None

    cmd = [yt_dlp] + get_js_runtime_arg() + [
        "-j",
        "-f", "18/best[ext=mp4][height<=360]/best[height<=360]/best",
        f"https://www.youtube.com/watch?v={video_id}"
    ]
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=20)
        if proc.returncode != 0:
            print(f"[LiwusTube Bridge] yt-dlp erro: {proc.stderr}")
            return None
        info = json.loads(proc.stdout)
        CACHE_STREAMS[video_id] = info
        return info
    except Exception as e:
        print(f"[LiwusTube Bridge] Excecao ao resolver {video_id}: {e}")
        return None

class TubeHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Log simplificado
        sys.stderr.write(f"[Bridge] {self.address_string()} - {format % args}\n")

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        host_header = self.headers.get("Host", f"127.0.0.1:{PORT}")

        if path == "/ping" or path == "/status":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(b'{"status":"ok","bridge":"LiwusTube Bridge v1.0"}')
            return

        if path.startswith("/search") or path.startswith("/api/v1/search"):
            q = query.get("q", query.get("query", [""]))[0]
            print(f"[Bridge] Pesquisando: '{q}'")
            items = search_youtube(q)
            resp = json.dumps({"items": items}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(resp)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(resp)
            return

        if path.startswith("/streams/") or path.startswith("/api/v1/videos/"):
            vid = path.rstrip("/").split("/")[-1]
            print(f"[Bridge] Resolvendo stream para video ID: '{vid}'")
            info = resolve_stream(vid)
            if not info:
                self.send_response(404)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(b'{"error":"Video ou stream nao encontrada"}')
                return

            video_proxy_url = f"http://{host_header}/video/{vid}"
            direct_url = info.get("url", video_proxy_url)

            resp_data = {
                "title": info.get("title", f"Video {vid}"),
                "uploader": info.get("uploader", info.get("channel", "YouTube")),
                "author": info.get("uploader", info.get("channel", "YouTube")),
                "duration": info.get("duration", 0),
                "lengthSeconds": info.get("duration", 0),
                "videoStreams": [
                    {
                        "url": video_proxy_url,
                        "directUrl": direct_url,
                        "quality": "360p",
                        "format": "mp4",
                        "container": "mp4",
                        "vcodec": "h264",
                        "acodec": "aac",
                        "videoOnly": False
                    }
                ],
                "formatStreams": [
                    {
                        "url": video_proxy_url,
                        "quality": "360p",
                        "container": "mp4",
                        "type": "video/mp4"
                    }
                ]
            }
            resp = json.dumps(resp_data).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(resp)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(resp)
            return

        if path.startswith("/video/"):
            vid = path.rstrip("/").split("/")[-1]
            info = resolve_stream(vid)
            if not info or "url" not in info:
                self.send_response(404)
                self.end_headers()
                return

            stream_url = info["url"]
            range_hdr = self.headers.get("Range")
            fwd_headers = {
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
            }
            if range_hdr:
                fwd_headers["Range"] = range_hdr

            req = urllib.request.Request(stream_url, headers=fwd_headers)
            try:
                with urllib.request.urlopen(req, timeout=15) as upstream:
                    self.send_response(upstream.status)
                    for k, v in upstream.headers.items():
                        if k.lower() in ["content-type", "content-length", "content-range", "accept-ranges"]:
                            self.send_header(k, v)
                    self.end_headers()

                    while True:
                        chunk = upstream.read(64 * 1024)
                        if not chunk:
                            break
                        self.wfile.write(chunk)
            except Exception as e:
                print(f"[Bridge] Erro no streaming de {vid}: {e}")
            return

        self.send_response(404)
        self.end_headers()

def main():
    print(f"============================================================")
    print(f" LiwusTube Bridge Server iniciado em http://0.0.0.0:{PORT}")
    print(f" QEMU Gateway : http://10.0.2.2:{PORT}")
    print(f" Localhost    : http://127.0.0.1:{PORT}")
    print(f"============================================================")
    server = HTTPServer(("0.0.0.0", PORT), TubeHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nEncerrando LiwusTube Bridge...")
        server.server_close()

if __name__ == "__main__":
    main()
