import os
from pathlib import Path
import pytest
import stat


def test_copy_to_new(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")

    res = run_ncp("source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_copy_into_dir(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("dir").mkdir()

    res = run_ncp("source", "dir")
    assert res.returncode == 0
    assert (Path("dir") / "source").read_text() == "source"


def test_overwrite_file(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")

    res = run_ncp("source", "target")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_overwrite_dir(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("dir").mkdir()
    (Path("dir") / "source").mkdir()

    res = run_ncp("source", "dir")
    assert res.returncode == 3


def test_copy_onto_self(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")

    res = run_ncp("source", "source")
    assert res.returncode == 0
    assert Path("source").read_text() == "source"


def test_copy_hardlink_onto_self(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.link("source", "hardlink")

    res = run_ncp("source", "hardlink")
    assert res.returncode == 0
    assert Path("hardlink").read_text() == "source"


def test_copy_non_extant(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("source", "target")
    assert res.returncode == 3
    assert not Path("target").exists()


def test_multiple_onto_file(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source1").write_text("source1")
    Path("source2").write_text("source2")
    Path("target").write_text("target")

    res = run_ncp("source1", "source2", "target")
    assert res.returncode == 3
