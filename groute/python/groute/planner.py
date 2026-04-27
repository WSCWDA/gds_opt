from dataclasses import dataclass
from enum import Enum


class RoutePath(str, Enum):
    POSIX = "posix"
    CUFILE = "cufile"


@dataclass
class PlannerConfig:
    cufile_min_io_size: int = 256 * 1024
    cufile_available: bool = False


@dataclass
class RoutePlan:
    path: RoutePath
    reason: str


class RoutePlanner:
    def __init__(self, cfg: PlannerConfig):
        self.cfg = cfg

    def plan(self, io_size: int, supports_direct_io: bool = True, cache_hot: bool = False) -> RoutePlan:
        if not self.cfg.cufile_available:
            return RoutePlan(RoutePath.POSIX, "cuFile unavailable (stub build)")
        if not supports_direct_io:
            return RoutePlan(RoutePath.POSIX, "filesystem/direct-io unsupported")
        if io_size < self.cfg.cufile_min_io_size:
            return RoutePlan(RoutePath.POSIX, "I/O too small for cuFile path")
        if cache_hot:
            return RoutePlan(RoutePath.POSIX, "page-cache hot")
        return RoutePlan(RoutePath.CUFILE, "large/cold request, prefer cuFile")
