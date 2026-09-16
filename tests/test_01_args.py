import os
from pathlib import Path
import pytest
import stat
from time import time


def test_blank(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp()
    assert res.returncode == 1


def test_one(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("42")
    assert res.returncode == 1


def test_two(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("42", "69")
    assert res.returncode == 3


def test_help(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("--help")
    assert res.returncode == 0
    assert res.stdout


def test_version(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("--version")
    assert res.returncode == 0
    assert res.stdout
 
 
def test_bad_option(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("--bad-option", "source", "target")
    assert res.returncode == 1
 

@pytest.mark.parametrize("value", ["0", "17", "bogus", "-1"])
def test_bad_jobs_value(tmp_path, monkeypatch, value, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("--jobs", value, "source", "target")
    assert res.returncode == 1
 

def test_bad_unlink_value(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("--unlink=bogus", "source", "target")
    assert res.returncode == 1
 

def test_bad_update_value(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("--update=bogus", "source", "target")
    assert res.returncode == 1
 
 
def test_follow_keep_links(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("--follow-links", "--keep-links", "source", "target")
    assert res.returncode == 1
 
 
def test_target(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source1").write_text("source1")
    Path("source2").write_text("source2")
    Path("target").mkdir()
 
    res = run_ncp("--target", "target", "source1", "source2")
    assert res.returncode == 0
    assert Path("target/source1").read_text() == "source1"
    assert Path("target/source2").read_text() == "source2"


def test_jobs_2(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
 
    res = run_ncp("--jobs=2", "source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_rv(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "file").write_text("source")
 
    res = run_ncp("-rv", "source_dir", "target_dir")
    assert res.returncode == 0
    assert Path("target_dir/file").read_text() == "source"


def test_double_dash(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("-source").write_text("source")
 
    res = run_ncp("--", "-source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_triple_dash(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")

    res = run_ncp("---bogus", "source", "target")
    assert res.returncode == 1


def test_Dfmort(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "file").write_text("source")
    os.chmod("source_dir/file", 0o640)
    old = time() - 100
    os.utime("source_dir/file", (old, old))
    os.symlink("file", "source_dir/link")
 
    res = run_ncp("-Dfmort", "source_dir", "target_dir")
    assert res.returncode == 0
    assert stat.S_IMODE(Path("target_dir/file").stat().st_mode) == 0o640
    assert Path("target_dir/file").stat().st_mtime == old
    assert Path("target_dir/link").is_symlink()
    assert Path("target_dir/file").stat().st_uid == Path("source_dir/file").stat().st_uid
    assert Path("target_dir/file").stat().st_gid == Path("source_dir/file").stat().st_gid
