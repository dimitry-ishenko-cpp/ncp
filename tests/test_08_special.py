import os
from pathlib import Path
import pytest
import socket
import stat
import threading


def test_no_fifo(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "source").write_text("source")
    os.mkfifo("source_dir/pipe")

    res = run_ncp("--recursive", "source_dir", "target_dir")
    assert res.returncode == 0
    assert Path("target_dir/source").read_text() == "source"
    assert not Path("target_dir/pipe").exists()


def test_fifo(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    os.mkfifo("source_dir/pipe")

    res = run_ncp("--recursive", "--special", "source_dir", "target_dir")
    assert res.returncode == 0
    assert stat.S_ISFIFO(os.stat("target_dir/pipe").st_mode)


def test_no_socket(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind("source_dir/sock")
    s.close()

    res = run_ncp("-r", "source_dir", "target_dir")
    assert res.returncode == 0
    assert not Path("target_dir/sock").exists()


def test_socket(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind("source_dir/sock")
    s.close()

    res = run_ncp("-r", "--special", "source_dir", "target_dir")
    assert res.returncode == 0
    assert stat.S_ISSOCK(os.stat("target_dir/sock").st_mode)


def test_no_socket_top_level(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind("sock")
    s.close()

    res = run_ncp("sock", "target")
    assert res.returncode == 3
    assert not Path("target").exists()


def test_dev_null(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    if not Path("/dev/null").exists(): pytest.skip("no /dev/null")

    res = run_ncp("/dev/null", "target")
    assert res.returncode == 0
    assert Path("target").read_bytes() == b""


def test_fifo_top(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    os.mkfifo("pipe")
    payload = b"payload"

    def writer():
        with open("pipe", "wb") as fp:
            fp.write(payload)

    th = threading.Thread(target=writer)
    th.start()

    try:
        res = run_ncp("pipe", "target", timeout=10)
    finally:
        th.join(timeout=10)

    assert res.returncode == 0
    assert Path("target").read_bytes() == payload


@pytest.mark.skipif(os.geteuid() != 0, reason="requires root")
def test_block_dev(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    source = Path("/dev/loop-control")
    if not source.exists(): pytest.skip("no /dev/loop-control")

    st = source.stat()
    os.mknod("source_node", mode=stat.S_IFCHR | 0o600, device=st.st_rdev)

    res = run_ncp("--devices", "source_node", "target")
    assert res.returncode == 0
    assert stat.S_ISCHR(Path("target").stat().st_mode)
