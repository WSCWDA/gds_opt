"""PyTorch Dataset wrapper for route-aware read planning (MVP skeleton)."""

from typing import Any


class GRouteDatasetWrapper:
    """A thin wrapper that can be extended to route file reads via G-Route runtime.

    Current MVP behavior:
    - delegates all indexing/length calls to the wrapped dataset
    - keeps a placeholder `planner` field for future route integration
    """

    def __init__(self, dataset: Any, planner: Any = None):
        self.dataset = dataset
        self.planner = planner

    def __len__(self):
        return len(self.dataset)

    def __getitem__(self, idx):
        return self.dataset[idx]
