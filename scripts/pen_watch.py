#!/usr/bin/env python3
"""Watches the pen/ drop-zone folder and hot-plugs the virtual pendrive
inside QEMU every time its contents change.

How it works:
  1. The pen/ folder is hashed (names/sizes/mtimes).
  2. On change: scripts/pack_pen.sh repacks pendrive.img as FAT32.
  3. Via the QMP socket the SCSI disk is unplugged, rebuilt, and re-plugged.
     (device_del -> blockdev-del node 'pen' -> blockdev-add -> device_add)
The guest polls the SCSI bus, so it sees the mount/unmount live.

Requires: QEMU started with the SCSI controller + -qmp unix:/tmp/liwus_qmp.sock
"""
import json
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PEN_DIR = os.path.join(ROOT, "pen")
PACK_SH = os.path.join(ROOT, "scripts", "pack_pen.sh")
IMG = os.path.join(ROOT, "pendrive.img")
QMP_PATH = "/tmp/liwus_qmp.sock"


def folder_sig():
    sig = {}
    try:
        for name in os.listdir(PEN_DIR):
            p = os.path.join(PEN_DIR, name)
            if not os.path.isfile(p):
                continue
            st = os.stat(p)
            sig[name] = (st.st_size, int(st.st_mtime))
    except OSError as e:
        print("folder_sig error:", e)
    return json.dumps(sig, sort_keys=True)


class QMP:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.s.settimeout(3)
        self.file = self.s.makefile("rb")
        while True:
            o = self._one()
            if "QMP" in o:
                break
        self.cmd("qmp_capabilities")

    def _one(self):
        line = self.file.readline()
        if not line:
            raise RuntimeError("qmp closed")
        return json.loads(line)

    def cmd(self, name, **kw):
        self.s.sendall((json.dumps({"execute": name, "arguments": kw}) + "\n").encode())
        while True:
            o = self._one()
            if "event" in o:
                continue
            return o


def replug(q, imgpath):
    try:
        r = q.cmd("device_del", id="pendrive_disk")
        print("  device_del:", "ok" if "return" in r else r)
    except Exception as e:
        print("  device_del failed:", e)
    time.sleep(0.3)
    try:
        r = q.cmd("blockdev-del", node_name="pen")
        print("  blockdev-del:", "ok" if "return" in r else r)
    except Exception as e:
        print("  blockdev-del failed:", e)
    time.sleep(0.3)
    r = q.cmd(
        "blockdev-add",
        driver="raw",
        node_name="pen",
        file={"driver": "file", "filename": imgpath},
    )
    print("  blockdev-add:", "ok" if "return" in r else r)
    r = q.cmd(
        "device_add",
        driver="scsi-hd",
        id="pendrive_disk",
        drive="pen",
        bus="scsi0.0",
    )
    print("  device_add:", "ok" if "return" in r else r)
    print("replug done at", time.strftime("%H:%M:%S"))


def main():
    last = None
    connected = False
    try:
        while True:
            if not connected:
                try:
                    q = QMP(QMP_PATH)
                    connected = True
                    print("qmp connected")
                except (socket.error, RuntimeError, ValueError) as e:
                    print("waiting for qmp:", e)
                    q = None
                    time.sleep(1)
                    continue
            sig = folder_sig()
            if sig != last:
                print("pen folder changed -> repack + replug")
                subprocess.run(["bash", PACK_SH], check=True,
                               cwd=ROOT)
                if not os.path.exists(IMG):
                    print("no pendrive.img produced")
                    last = sig
                    time.sleep(1)
                    continue
                replug(q, os.path.abspath(IMG))
                last = sig
            time.sleep(1)
    except KeyboardInterrupt:
        print("watcher stopped")


main()