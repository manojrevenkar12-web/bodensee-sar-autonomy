import os

import pytest

os.environ.setdefault("MLFLOW_DISABLE_AGENT_HINT", "1")
os.environ.setdefault("RAY_AUTH_MODE", "disabled")

from sar import conf
from sar.spec import load_dir


@pytest.fixture(scope="session")
def dev_specs():
    return load_dir(conf.path("scenarios", "dev"))


@pytest.fixture(scope="session")
def spec(dev_specs):
    return next(s for s in dev_specs if s.id == "immenstaad-swimmer-calm")
