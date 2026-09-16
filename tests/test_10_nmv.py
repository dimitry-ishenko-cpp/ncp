import os
from pathlib import Path
import pytest
import stat


def test_nmv_move(tmp_path, monkeypatch, run_nmv):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
 
    res = run_nmv("source", "target")
    assert res.returncode == 0
    assert not Path("source").exists()
    assert Path("target").read_text() == "source"
 
 
def test_nmv_help(tmp_path, monkeypatch, run_nmv):
    monkeypatch.chdir(tmp_path)
    res = run_nmv("--help")
    assert res.returncode == 0
    assert res.stdout
