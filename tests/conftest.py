import os
import pytest
from subprocess import run

@pytest.fixture
def run_ncp():
    ncp = os.environ["NCP"]
    return lambda *args, **kwargs: run([ncp, *args], capture_output=True, text=True, **kwargs)
