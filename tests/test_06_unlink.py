import os
from pathlib import Path
import stat
from time import time


def test_update_none(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")

    res = run_ncp("--update=none", "source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "target"


def test_update_all(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")
    now = time()
    os.utime("source", (now - 100, now - 100))
    os.utime("target", (now, now))

    res = run_ncp("--update=all", "source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_no_update_older(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")
    now = time()
    os.utime("source", (now - 100, now - 100))
    os.utime("target", (now, now))

    res = run_ncp("--update", "--", "source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "target"


def test_update_older(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")
    now = time()
    os.utime("source", (now, now))
    os.utime("target", (now - 100, now - 100))

    res = run_ncp("--update", "--", "source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_no_update_size(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")
    os.utime("target", (0, 0))

    res = run_ncp("--update=size", "source", "target")
    assert res.returncode == 0
    assert Path("target").stat().st_mtime == 0
    assert Path("target").read_text() == "target"


def test_update_size(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("targetX")
    now = time()
    os.utime("source", (now, now))
    os.utime("target", (now, now))

    res = run_ncp("--update=size", "source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_update_changed(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("targetX")

    res = run_ncp("--update=changed", "source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_no_unlink_never(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    os.mkfifo("pipe")
    Path("target").write_text("target")

    res = run_ncp("--special", "--unlink=never", "pipe", "target")
    assert res.returncode == 3
    assert stat.S_ISREG(os.stat("target").st_mode)


def test_unlink_always(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")
    old_ino = Path("target").stat().st_ino

    res = run_ncp("--unlink=always", "source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"
    assert Path("target").stat().st_ino != old_ino


def test_no_unlink_auto(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")
    old_ino = Path("target").stat().st_ino

    res = run_ncp("source", "target")
    assert res.returncode == 0
    assert Path("target").stat().st_ino == old_ino
