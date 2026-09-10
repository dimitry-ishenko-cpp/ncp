import os
from pathlib import Path
from subprocess import run

def run_ncp(*args):
    ncp = os.environ["NCP"]
    return run([ncp, *args], capture_output=True, text=True)

def test_blank(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    res = run_ncp()
    assert res.returncode == 1

def test_one(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    res = run_ncp("42")
    assert res.returncode == 1

def test_two(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)

    res = run_ncp("42", "69")
    assert res.returncode == 3
