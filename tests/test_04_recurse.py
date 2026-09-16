import os
from pathlib import Path
import pytest
import stat


def make_tree(root: Path):
    root.mkdir()
    (root / "file1").write_text("source1")
    (root / "subdir").mkdir()
    (root / "subdir" / "file2").write_text("source2")
    (root / "subdir" / "subdir").mkdir()
    (root / "subdir" / "subdir" / "file3").write_text("source3")


def test_non_recurse(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    make_tree(Path("source_dir"))

    res = run_ncp("source_dir", "target_dir")
    assert res.returncode == 0
    assert not Path("target_dir").exists()


def test_recurse(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    make_tree(Path("source_dir"))

    res = run_ncp("--recursive", "source_dir", "target_dir")
    assert res.returncode == 0
    assert Path("target_dir/file1").read_text() == "source1"
    assert Path("target_dir/subdir/file2").read_text() == "source2"
    assert Path("target_dir/subdir/subdir/file3").read_text() == "source3"


def test_recurse_merge(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    make_tree(Path("source_dir"))
    Path("target_dir").mkdir()
    Path("target_dir/target").write_text("target")

    res = run_ncp("--recursive", "source_dir/", "target_dir")
    assert res.returncode == 0
    assert Path("target_dir/file1").read_text() == "source1"
    assert Path("target_dir/target").read_text() == "target"


def test_recurse_unlink_auto(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    make_tree(Path("source_dir"))
    Path("target_dir").write_text("target_dir")

    res = run_ncp("--recursive", "source_dir", "target_dir")
    assert res.returncode == 0
    assert Path("target_dir").is_dir()
    assert Path("target_dir/file1").read_text() == "source1"


def test_recurse_unlink_never(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    make_tree(Path("source_dir"))
    Path("target_dir").write_text("target_dir")

    res = run_ncp("--recursive", "--unlink=never", "source_dir", "target_dir")
    assert res.returncode == 3
    assert Path("target_dir").is_file()
    assert Path("target_dir").read_text() == "target_dir"
