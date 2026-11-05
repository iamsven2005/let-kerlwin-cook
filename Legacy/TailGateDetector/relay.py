"""
relay.py - runs on your laptop. The TinyScreen+ sends a plain HTTP request
here, and this script forwards the message to Telegram over HTTPS.

WHY: the WiFi shield can't do HTTPS without a certificate (which won't load on
this board). Your laptop can do HTTPS fine, so it does that part.

SETUP
  1. Fill in BOT_TOKEN and CHAT_ID below.
  2. Make sure your laptop and the device are on the SAME WiFi network.
  3. Run:  python relay.py
     It prints your laptop's IP address, e.g. 192.168.1.42
  4. Put that IP in the sketch's RELAY_HOST, and set RELAY_PORT to 8080.
  5. Keep this script running during the demo.

TEST: open in a browser:  http://YOUR_LAPTOP_IP:8080/?msg=hello
      "hello" should arrive in Telegram.
"""

import socket
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer

# ---- fill these in ----
BOT_TOKEN = "<TELE-BOT-TOKEN>" #@BotFather /newbot
CHAT_ID   = "<TELE-CHAT-ID>" #@userinfobot /start
PORT      = 8080


def send_to_telegram(text):
    url = (
        f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage"
        f"?chat_id={CHAT_ID}&text={urllib.parse.quote(text)}"
    )
    try:
        with urllib.request.urlopen(url, timeout=10) as r:
            print(f"  Telegram: {r.status}")
    except Exception as e:
        print(f"  Telegram error: {e}")


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        params = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        msg = params.get("msg", ["Tailgate alert"])[0]
        print(f"Got: {msg}")
        send_to_telegram(msg)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, *args):
        pass  # keep the console clean


def my_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


if __name__ == "__main__":
    ip = my_ip()
    print("=" * 50)
    print(f"Relay running.  Put this in the sketch:")
    print(f"   RELAY_HOST = \"{ip}\"")
    print(f"   RELAY_PORT = {PORT}")
    print(f"Test in a browser:  http://{ip}:{PORT}/?msg=hello")
    print("Keep this window open during the demo.  Ctrl+C to stop.")
    print("=" * 50)
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
