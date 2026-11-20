"""Legacy HTTP-to-Telegram relay. Forwarding requires explicit opt-in."""
import os
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer


def configuration():
    return {
        "host": os.environ.get("TAILGATE_BIND", "127.0.0.1"),
        "port": int(os.environ.get("TAILGATE_PORT", "8080")),
        "enabled": os.environ.get("TAILGATE_FORWARD", "0") == "1",
        "token": os.environ.get("TELEGRAM_BOT_TOKEN", ""),
        "chat": os.environ.get("TELEGRAM_CHAT_ID", ""),
    }


def forward(text, config):
    if not config["enabled"]:
        return
    if not config["token"] or not config["chat"]:
        raise ValueError("Telegram configuration missing")
    url = "https://api.telegram.org/bot" + config["token"] + "/sendMessage"
    body = urllib.parse.urlencode({"chat_id": config["chat"], "text": text}).encode()
    request = urllib.request.Request(url, data=body, method="POST")
    with urllib.request.urlopen(request, timeout=10) as response:
        if response.status != 200:
            raise RuntimeError("Telegram rejected message")


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        params = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
        messages = params.get("msg", [])
        if parsed.path != "/" or len(messages) != 1 or not messages[0] or len(messages[0]) > 512:
            self.send_error(400, "Expected one nonempty msg (max 512 characters)")
            return
        try:
            forward(messages[0], self.server.config)
        except Exception:
            # Never expose provider errors: they may contain the secret URL.
            self.send_error(502, "Forwarding failed")
            return
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"OK\n")

    def log_message(self, *args):
        pass  # URLs can contain message content


def main():
    config = configuration()
    if config["enabled"] and (not config["token"] or not config["chat"]):
        raise SystemExit("Forwarding enabled but Telegram configuration missing")
    server = HTTPServer((config["host"], config["port"]), Handler)
    server.config = config
    print("Tailgate relay listening; forwarding " + ("enabled" if config["enabled"] else "disabled"), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
