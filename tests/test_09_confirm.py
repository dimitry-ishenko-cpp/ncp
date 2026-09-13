from pathlib import Path


def test_confirm_yes(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")

    res = run_ncp("--interactive", "source", "target", input="y\n")
    assert res.returncode == 0
    assert Path("target").read_text() == "source"


def test_confirm_no(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")

    res = run_ncp("--interactive", "source", "target", input="n\n")
    assert res.returncode == 0
    assert Path("target").read_text() == "target"


def test_confirm_all(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "1").write_text("source1")
    (Path("source_dir") / "2").write_text("source2")
    Path("target_dir").mkdir()
    (Path("target_dir") / "1").write_text("target1")
    (Path("target_dir") / "2").write_text("target2")

    res = run_ncp("--recursive", "--interactive", "source_dir/", "target_dir", input="a\n")
    assert res.returncode == 0
    assert Path("target_dir/1").read_text() == "source1"
    assert Path("target_dir/2").read_text() == "source2"


def test_confirm_skip(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "1").write_text("source1")
    (Path("source_dir") / "2").write_text("source2")
    Path("target_dir").mkdir()
    (Path("target_dir") / "1").write_text("target1")
    (Path("target_dir") / "2").write_text("target2")

    res = run_ncp("--recursive", "--interactive", "source_dir/", "target_dir", input="s\n")
    assert res.returncode == 0
    assert Path("target_dir/1").read_text() == "target1"
    assert Path("target_dir/2").read_text() == "target2"


def test_interactive_quit_stops_early_without_error_exit_code(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "file").write_text("source")
    Path("target_dir").mkdir()
    (Path("target_dir") / "file").write_text("target")

    res = run_ncp("--recursive", "--interactive", "source_dir/", "target_dir", input="q\n")
    assert res.returncode == 0
    assert Path("target_dir/file").read_text() == "target"
