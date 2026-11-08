import os
from pathlib import Path
import pytest
from subprocess import run

@pytest.fixture
def run_ncp():
    ncp = os.environ["NCP"]
    return lambda *args, **kwargs: run([ncp, *args], capture_output=True, text=True, **kwargs)

@pytest.fixture
def run_nmv():
    nmv = Path(os.environ["NCP"]).parent / "nmv"
    if not nmv.exists(): pytest.skip("nmv not found")
    return lambda *args, **kwargs: run([nmv, *args], capture_output=True, text=True, **kwargs)
