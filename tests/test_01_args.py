import os
from pathlib import Path
import pytest


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
 
 
def test_follow_links_conflict(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    res = run_ncp("--follow-links", "--keep-links", "source", "target")
    assert res.returncode == 1
 
 
def test_target_option_redirects_extra_positional(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source1").write_text("source1")
    Path("source2").write_text("source2")
    Path("target").mkdir()
 
    res = run_ncp("--target", "target", "source1", "source2")
    assert res.returncode == 0
    assert Path("target/source1").read_text() == "source1"
    assert Path("target/source2").read_text() == "source2"
