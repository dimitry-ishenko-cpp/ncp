import os
from pathlib import Path
from subprocess import run

def run_ncp(*args):
    ncp = os.environ["NCP"]
    return run([ncp, *args], capture_output=True, text=True)

def test_file_link_target(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    Path("source").write_text("source")
    os.symlink("source", "source_link")

    Path("target").write_text("target")
    os.symlink("target", "target_link")

    res = run_ncp("source_link", "target_link")
    assert res.returncode == 0

    assert Path("target_link").is_symlink()
    assert Path("target").read_text() == "source"

def test_keep_links_file_link_target(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    Path("source").write_text("source")
    os.symlink("source", "source_link")

    Path("target").write_text("target")
    os.symlink("target", "target_link")

    res = run_ncp("--keep-links", "source_link", "target_link")
    assert res.returncode == 0

    assert Path("target_link").is_symlink()
    assert Path("target").read_text() == "target"

def test_non_dir_target_fail(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    Path("source1").write_text("source1")
    Path("source2").write_text("source2")
    Path("target").write_text("target")

    res = run_ncp("source1", "source2", "target")
    assert res.returncode != 0

def test_dir_link_target(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    Path("source").write_text("source")
    Path("dir").mkdir()
    os.symlink("dir", "dir_link")

    res = run_ncp("source", "dir_link")
    assert res.returncode == 0

    assert (Path("dir") / "source").exists()
    assert (Path("dir") / "source").read_text() == "source"
