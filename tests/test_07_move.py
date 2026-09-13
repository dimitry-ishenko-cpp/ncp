import os
from pathlib import Path
import stat


def test_move_file(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")

    res = run_ncp("--move", "source", "target")
    assert res.returncode == 0
    assert not Path("source").exists()
    assert Path("target").read_text() == "source"


def test_move_tree(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "file1").write_text("source1")
    (Path("source_dir") / "subdir").mkdir()
    (Path("source_dir") / "subdir" / "file2").write_text("source2")

    res = run_ncp("--recursive", "--move", "source_dir", "target_dir")
    assert res.returncode == 0
    assert not Path("source_dir").exists()
    assert Path("target_dir/file1").read_text() == "source1"
    assert Path("target_dir/subdir/file2").read_text() == "source2"


def test_no_move(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    os.mkfifo("pipe")
    Path("target").write_text("target")

    res = run_ncp("--special", "--move", "--unlink=never", "pipe", "target")
    assert res.returncode == 3
    assert Path("pipe").exists()
    assert stat.S_ISREG(os.stat("target").st_mode)


def test_move_keep_links(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.symlink("source", "source_link")

    res = run_ncp("--move", "--keep-links", "source_link", "target_link")
    assert res.returncode == 0
    assert not Path("source_link").exists()
    assert Path("target_link").is_symlink()
    assert os.readlink("target_link") == "source"
    assert Path("source").read_text() == "source"


def test_move_onto_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("target").write_text("target")
    os.symlink("target", "target_link")

    res = run_ncp("--move", "source", "target_link")
    assert res.returncode == 0
    assert not Path("source").exists()
    assert not Path("target_link").is_symlink()
    assert Path("target_link").read_text() == "source"
    assert Path("target").read_text() == "target"


def test_move_to_link_dir(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    Path("dir").mkdir()
    os.symlink("dir", "dir_link")

    res = run_ncp("--move", "source", "dir_link")
    assert res.returncode == 0
    assert not Path("source").exists()
    assert Path("dir_link").is_symlink()
    assert Path("dir/source").exists()


def test_move_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source").write_text("source")
    os.symlink("source", "source_link")

    res = run_ncp("--move", "source_link", "target")
    assert res.returncode == 0
    assert Path("target").is_symlink()
    assert Path("target").read_text() == "source"
    assert Path("source").exists()


def test_move_dir_link(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("dir").mkdir()
    (Path("dir") / "source").write_text("source")
    os.symlink("dir", "dir_link")

    res = run_ncp("--move", "--recursive", "dir_link", "target")
    assert res.returncode == 0
    assert Path("target").is_symlink()
    assert (Path("target") / "source").read_text() == "source"
    assert not Path("dir_link").exists()
    assert Path("dir").exists()


def test_move_dir(tmp_path, monkeypatch, run_ncp):
    monkeypatch.chdir(tmp_path)
    Path("source_dir").mkdir()
    (Path("source_dir") / "source").write_text("source")
    Path("target_dir").mkdir()
    os.symlink("target_dir", "target_link")

    res = run_ncp("--recursive", "--move", "source_dir/", "target_link")
    assert res.returncode == 0
    assert Path("target_link").is_symlink()
    assert Path("target_dir/source").read_text() == "source"
    assert not Path("source_dir").exists()
