"""End-to-end FTP checks against a running ScanReceiver.

Usage: python test/ftp_test.py <root-dir> [port] [user] [password]
  <root-dir> must be the receive folder the server is using (the script inspects files there).
Normally run through test/run_ftp_test.ps1, which starts the app with a throwaway config.
Exit code 0 = all checks passed.
"""
import ftplib
import io
import os
import sys

ROOT = sys.argv[1]
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 2121
USER = sys.argv[3] if len(sys.argv) > 3 else "scan"
PASSWORD = sys.argv[4] if len(sys.argv) > 4 else "scan1234"
HOST = "127.0.0.1"

data = os.urandom(300_000)
results = []


def check(name, cond):
    results.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name)


def login(encoding="utf-8"):
    f = ftplib.FTP(encoding=encoding)
    f.connect(HOST, PORT, timeout=10)
    f.login(USER, PASSWORD)
    return f


# Wrong password is rejected.
try:
    f = ftplib.FTP()
    f.connect(HOST, PORT, timeout=10)
    f.login(USER, PASSWORD + "-wrong")
    check("bad password rejected", False)
except ftplib.error_perm:
    check("bad password rejected", True)

# UTF-8 client, passive mode.
f = login("utf-8")
f.set_pasv(True)
f.storbinary("STOR 스캔_utf8.pdf", io.BytesIO(data))
p = os.path.join(ROOT, "스캔_utf8.pdf")
check("passive STOR utf8 name", os.path.exists(p) and open(p, "rb").read() == data)
check("SIZE", f.size("스캔_utf8.pdf") == len(data))
f.mkd("sub")
f.cwd("sub")
check("PWD after CWD", f.pwd() == "/sub")
f.storbinary("STOR tmp.part", io.BytesIO(b"hello"))
f.rename("tmp.part", "final.jpg")
check("RNFR/RNTO", os.path.exists(os.path.join(ROOT, "sub", "final.jpg")))
f.cwd("/")
lines = []
f.retrlines("LIST", lines.append)
check("LIST shows file", any("스캔_utf8.pdf" in line for line in lines))
# "../" must never escape the receive folder.
f.storbinary("STOR ../../escape.txt", io.BytesIO(b"x"))
check("traversal clamped to root",
      os.path.exists(os.path.join(ROOT, "escape.txt"))
      and not os.path.exists(os.path.join(os.path.dirname(ROOT), "escape.txt")))
f.delete("escape.txt")
check("DELE", not os.path.exists(os.path.join(ROOT, "escape.txt")))
buf = io.BytesIO()
f.retrbinary("RETR 스캔_utf8.pdf", buf.write)
check("RETR roundtrip", buf.getvalue() == data)
f.set_pasv(False)
f.storbinary("STOR active.bin", io.BytesIO(data))
check("active STOR", open(os.path.join(ROOT, "active.bin"), "rb").read() == data)
f.quit()

# CP949 client (typical Korean multifunction printer).
f = login("cp949")
f.storbinary("STOR 복합기_cp949.pdf", io.BytesIO(b"cp949"))
check("CP949 name decoded", os.path.exists(os.path.join(ROOT, "복합기_cp949.pdf")))
check("NLST in CP949", "복합기_cp949.pdf" in f.nlst())
f.quit()

passed = sum(ok for _, ok in results)
print(f"\n{passed}/{len(results)} passed")
sys.exit(0 if passed == len(results) else 1)
