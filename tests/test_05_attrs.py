import os
from pathlib import Path
import pytest
import stat
from time import time


def test_no_time(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    old = time() - 100
    os.utime("source", (old, old))

    res = run_ncp("source", "target")
    assert res.returncode == 0
    assert Path("target").stat().st_mtime != old


def test_time(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    old = time() - 100
    os.utime("source", (old, old))

    res = run_ncp("--time", "source", "target")
    assert res.returncode == 0
    assert Path("target").stat().st_mtime == old


def test_no_mode(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.chmod("source", 0o741)

    res = run_ncp("source", "target")
    assert res.returncode == 0
    assert stat.S_IMODE(Path("target").stat().st_mode) != 0o741


def test_mode(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.chmod("source", 0o741)

    res = run_ncp("--mode", "source", "target")
    assert res.returncode == 0
    assert stat.S_IMODE(Path("target").stat().st_mode) == 0o741


def test_no_owner(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")

    res = run_ncp("--user", "--group", "source", "target")
    assert res.returncode == 0


@pytest.mark.skipif(os.geteuid() != 0, reason="requires root")
def test_owner(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.chown(Path("source"), 42, 69)

    res = run_ncp("--user", "--group", "source", "target")
    assert res.returncode == 0
    assert Path("target").stat().st_uid == 42
    assert Path("target").stat().st_gid == 69


def test_archive(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "file").write_text("source")
    os.chmod("source_dir/file", 0o640)
    old = time() - 100
    os.utime("source_dir/file", (old, old))
    os.symlink("file", "source_dir/link")

    res = run_ncp("--archive", "source_dir", "target_dir")
    assert res.returncode == 0
    assert stat.S_IMODE(Path("target_dir/file").stat().st_mode) == 0o640
    assert Path("target_dir/file").stat().st_mtime == old
    assert Path("target_dir/link").is_symlink()
