"""
mikrotik_client.py - RouterOS binary API client.

Ported from this project's ros_api_test.py, which mirrors firmware/
mikrotik_api.h's exact wire format (plaintext login, RouterOS >= 6.43:
length-prefixed words, "=key=value" attributes, !re/!done/!trap replies)
and was validated against a real MikroTik CHR 7.16.2 instance.

Includes the run() fix found during that validation: RouterOS always
sends a trailing !done after a !trap, so run() keeps reading through a
trap (remembering the failure) instead of returning immediately - an
early return leaves that !done unread and desyncs every reply after it
for the rest of the connection. The same applies to !empty (RouterOS
7.18+ sends it, followed by !done, for a print that matched nothing).
"""
import socket


class RouterOSError(Exception):
    pass


class MikrotikClient:
    # 15s, not 6: a busy hAP lite (32 MB RAM) sometimes takes several
    # seconds to answer a print, which surfaced as a bare TimeoutError.
    def __init__(self, host, port=8728, timeout=15):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self._user = None
        self._password = None

    def connect(self, user, password):
        self._user, self._password = user, password
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.settimeout(self.timeout)
        ok, _, error = self.run(["/login", f"=name={user}", f"=password={password}"])
        if not ok:
            self.close()
            raise RouterOSError(f"Login rejected - check username/password ({error})")
        return True

    def reconnect(self, attempts=10, delay=3):
        """Re-opens the session with the same credentials - used after a
        step that can briefly drop the link (a port moving into a bridge
        changes which interface answers the management IP)."""
        import time
        self.close()
        last = None
        for _ in range(attempts):
            try:
                return self.connect(self._user, self._password)
            except (RouterOSError, OSError) as exc:
                last = exc
                time.sleep(delay)
        raise RouterOSError(f"Could not reconnect: {last}")

    def local_ip(self):
        try:
            return self.sock.getsockname()[0]
        except (OSError, AttributeError):
            return None

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    def _write_len(self, length):
        if length < 0x80:
            self.sock.sendall(bytes([length]))
        elif length < 0x4000:
            length |= 0x8000
            self.sock.sendall(bytes([(length >> 8) & 0xFF, length & 0xFF]))
        elif length < 0x200000:
            length |= 0xC00000
            self.sock.sendall(bytes([(length >> 16) & 0xFF, (length >> 8) & 0xFF, length & 0xFF]))
        else:
            length |= 0xE0000000
            self.sock.sendall(bytes([(length >> 24) & 0xFF, (length >> 16) & 0xFF,
                                      (length >> 8) & 0xFF, length & 0xFF]))

    def _send_word(self, word):
        data = word.encode("utf-8")
        self._write_len(len(data))
        if data:
            self.sock.sendall(data)

    def _send_sentence(self, words):
        for w in words:
            self._send_word(w)
        self._write_len(0)

    def _read_byte(self):
        b = self.sock.recv(1)
        if not b:
            raise RouterOSError("connection closed")
        return b[0]

    def _read_len(self):
        c1 = self._read_byte()
        if (c1 & 0x80) == 0x00:
            return c1
        elif (c1 & 0xC0) == 0x80:
            return ((c1 & 0x3F) << 8) | self._read_byte()
        elif (c1 & 0xE0) == 0xC0:
            c2 = self._read_byte()
            c3 = self._read_byte()
            return ((c1 & 0x1F) << 16) | (c2 << 8) | c3
        elif (c1 & 0xF0) == 0xE0:
            c2 = self._read_byte()
            c3 = self._read_byte()
            c4 = self._read_byte()
            return ((c1 & 0x0F) << 24) | (c2 << 16) | (c3 << 8) | c4
        raise RouterOSError("5-byte length word not supported")

    def _read_word(self):
        length = self._read_len()
        if length == 0:
            return ""
        out = b""
        while len(out) < length:
            chunk = self.sock.recv(length - len(out))
            if not chunk:
                raise RouterOSError("connection closed mid-word")
            out += chunk
        return out.decode("utf-8", errors="replace")

    def _read_sentence(self):
        words = []
        while True:
            w = self._read_word()
            if w == "":
                break
            words.append(w)
        return words

    def _read_reply(self):
        words = self._read_sentence()
        kind = words[0].lstrip("!") if words else "error"
        attrs = {}
        for w in words[1:]:
            if w.startswith("="):
                eq = w.find("=", 1)
                if eq > 0:
                    attrs[w[1:eq]] = w[eq + 1:]
        return kind, attrs

    def run(self, words):
        """Sends a sentence, collects !re rows, and keeps reading through
        !trap/!empty so the connection never desyncs. Returns
        (ok, rows, error) - error is RouterOS's own !trap message (e.g.
        "already have such interface") on failure, None on success."""
        self._send_sentence(words)
        rows = []
        trapped = False
        error = None
        while True:
            kind, attrs = self._read_reply()
            if kind == "re":
                rows.append(attrs)
            elif kind == "trap":
                trapped = True
                error = attrs.get("message", str(attrs))
            elif kind == "empty":
                continue
            elif kind == "done":
                return (not trapped), rows, error
            elif kind == "fatal":
                raise RouterOSError(f"router closed the session: {attrs or words}")
            else:
                raise RouterOSError(f"unexpected reply '{kind}'")

    def print(self, path, filters=None):
        """Returns the rows of `<path>/print`, optionally narrowed by
        exact-match `?field=value` queries from `filters` (RouterOS field
        names as-is, e.g. {"mac-address": ...}). Raises RouterOSError on
        a trap."""
        words = [f"{path}/print"]
        words += [f"?{k}={v}" for k, v in (filters or {}).items()]
        ok, rows, error = self.run(words)
        if not ok:
            raise RouterOSError(error or f"{path}/print failed")
        return rows

    def find_id(self, path, field, value):
        try:
            rows = self.print(path, {field: value})
        except RouterOSError:
            return None
        return rows[0].get(".id") if rows else None
