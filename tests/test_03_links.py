import os
from pathlib import Path
import pytest
import stat


def test_copy_thru_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.symlink("source", "source_link")
    Path("target").write_text("target")
    os.symlink("target", "target_link")

    res = run_ncp("source_link", "target_link")
    assert res.returncode == 0
    assert Path("target_link").is_symlink()
    assert Path("target").read_text() == "source"


def test_keep_links_copy_thru_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.symlink("source", "source_link")
    Path("target").write_text("target")
    os.symlink("target", "target_link")

    res = run_ncp("--keep-links", "source_link", "target_link")
    assert res.returncode == 0
    assert Path("target_link").is_symlink()
    assert Path("target").read_text() == "target"
    assert os.readlink("target_link") == "source"


def test_copy_thru_dir_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("dir").mkdir()
    os.symlink("dir", "dir_link")

    res = run_ncp("source", "dir_link")
    assert res.returncode == 0
    assert (Path("dir") / "source").exists()
    assert (Path("dir") / "source").read_text() == "source"


def test_dead_target_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.symlink("dead", "dead_link")

    res = run_ncp("source", "dead_link")
    assert res.returncode == 0
    assert Path("dead_link").is_symlink()
    assert os.readlink("dead_link") == "dead"
    assert Path("dead").read_text() == "source"


def test_dead_source_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    os.symlink("dead", "dead_link")

    res = run_ncp("dead_link", "target")
    assert res.returncode == 3
    assert not Path("target").exists()


def test_self_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    os.symlink("loop_link", "loop_link")

    res = run_ncp("loop_link", "target", timeout=5)
    assert res.returncode != 0
    assert not Path("target").exists()


def test_recurse_keep_links(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "file").write_text("source")
    os.symlink("file", "source_dir/link")

    res = run_ncp("--recursive", "source_dir", "target_dir")
    assert res.returncode == 0
    assert Path("target_dir/link").is_symlink()
    assert os.readlink("target_dir/link") == "file"


def test_recurse_follow_links(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "file").write_text("source")
    os.symlink("file", "source_dir/link")

    res = run_ncp("--recursive", "--follow-links", "source_dir", "target_dir")
    assert res.returncode == 0
    assert not Path("target_dir/link").is_symlink()
    assert Path("target_dir/link").read_text() == "source"


def test_relative_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    Path("source").write_text("source")
    os.symlink("../source", "source_dir/link")

    res = run_ncp("source_dir/link", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"
