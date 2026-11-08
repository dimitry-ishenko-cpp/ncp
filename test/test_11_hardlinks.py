import os
from pathlib import Path
import pytest
import stat


def test_hardlink(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    Path("source_dir/file1").write_text("source")
    os.link("source_dir/file1", "source_dir/file2")

    res = run_ncp("--recursive", "--hard-links", "source_dir", "target_dir")
    assert res.returncode == 0

    stat0 = Path("source_dir/file1").stat()
    stat1 = Path("target_dir/file1").stat()
    stat2 = Path("target_dir/file2").stat()
    assert stat1.st_ino == stat2.st_ino
    assert stat1.st_dev == stat2.st_dev
    assert stat1.st_ino != stat0.st_ino


def test_hardlink_move(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    Path("source_dir/file1").write_text("source")
    os.link("source_dir/file1", "source_dir/file2")

    res = run_ncp("--recursive", "--move", "--hard-links", "source_dir", "target_dir")
    assert res.returncode == 0

    stat1 = Path("target_dir/file1").stat()
    stat2 = Path("target_dir/file2").stat()
    assert stat1.st_ino == stat2.st_ino
    assert not Path("source_dir").exists()


def test_hardlink_update(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    Path("source_dir/file1").write_text("source")
    os.link("source_dir/file1", "source_dir/file2")
    Path("target_dir").mkdir()
    Path("target_dir/file1").write_text("source")
    stat = Path("source_dir/file1").stat()
    os.utime("target_dir/file1", (stat.st_atime, stat.st_mtime))

    res = run_ncp("--recursive", "--hard-links", "--update=changed", "source_dir/", "target_dir")
    assert res.returncode == 0

    stat1 = Path("target_dir/file1").stat()
    stat2 = Path("target_dir/file2").stat()
    assert stat1.st_ino == stat2.st_ino
    assert stat1.st_nlink == 2


def test_hardlink_replace(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    Path("source_dir/file1").write_text("source")
    os.link("source_dir/file1", "source_dir/file2")
    Path("target_dir").mkdir()
    Path("target_dir/file2").write_text("targetX")

    res = run_ncp("--recursive", "--hard-links", "source_dir/", "target_dir")
    assert res.returncode == 0

    stat1 = Path("target_dir/file1").stat()
    stat2 = Path("target_dir/file2").stat()
    assert stat1.st_ino == stat2.st_ino
    assert Path("target_dir/file2").read_text() == "source"


def test_hardlink_partial(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    Path("source_dir/file1").write_text("source")
    os.link("source_dir/file1", "source_dir/file2")
    Path("target_dir").mkdir()

    res = run_ncp("--hard-links", "source_dir/file1", "target_dir/")
    assert res.returncode == 0

    assert Path("target_dir/file1").stat().st_nlink == 1
