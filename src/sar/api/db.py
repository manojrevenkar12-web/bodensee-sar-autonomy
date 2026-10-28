"""Missions, the plans proposed for them, approvals and uploads. Approvals and uploads are
append-only: there is no code path that updates or deletes them."""

from __future__ import annotations

import datetime as dt
import os
from collections.abc import Iterator
from contextlib import contextmanager

from sqlalchemy import JSON, DateTime, Float, ForeignKey, Integer, String, Text, create_engine, event
from sqlalchemy.engine import Engine
from sqlalchemy.orm import DeclarativeBase, Mapped, Session, mapped_column, sessionmaker


def now() -> dt.datetime:
    return dt.datetime.now(dt.UTC)


class Base(DeclarativeBase):
    pass


class Mission(Base):
    __tablename__ = "missions"
    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    scenario_id: Mapped[str] = mapped_column(String(64))
    spec: Mapped[dict] = mapped_column(JSON)
    seed: Mapped[int] = mapped_column(Integer)
    created_at: Mapped[dt.datetime] = mapped_column(DateTime(timezone=True), default=now)
    start: Mapped[dict] = mapped_column(JSON)  # belief at the start of the search, grid, UAVs


class Plan(Base):
    __tablename__ = "plans"
    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    mission_id: Mapped[int] = mapped_column(ForeignKey("missions.id"), index=True)
    planner: Mapped[str] = mapped_column(String(32))
    expected_success: Mapped[float] = mapped_column(Float)
    pos_curve: Mapped[list] = mapped_column(JSON)
    frames: Mapped[list] = mapped_column(JSON)
    tracks: Mapped[list] = mapped_column(JSON)


class Approval(Base):
    __tablename__ = "approvals"
    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    mission_id: Mapped[int] = mapped_column(ForeignKey("missions.id"), index=True)
    planner: Mapped[str] = mapped_column(String(32))
    approver: Mapped[str] = mapped_column(String(80))
    note: Mapped[str] = mapped_column(Text, default="")
    created_at: Mapped[dt.datetime] = mapped_column(DateTime(timezone=True), default=now)


class Upload(Base):
    __tablename__ = "uploads"
    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    approval_id: Mapped[int] = mapped_column(ForeignKey("approvals.id"), index=True)
    uav: Mapped[int] = mapped_column(Integer)
    vehicle: Mapped[str] = mapped_column(String(64))
    ok: Mapped[int] = mapped_column(Integer)
    items: Mapped[int] = mapped_column(Integer)
    retransmissions: Mapped[int] = mapped_column(Integer)
    error: Mapped[str] = mapped_column(Text, default="")
    created_at: Mapped[dt.datetime] = mapped_column(DateTime(timezone=True), default=now)


_engine: Engine | None = None
_session: sessionmaker[Session] | None = None


def engine() -> Engine:
    global _engine, _session
    if _engine is None:
        url = os.environ.get("DATABASE_URL", "sqlite:///./sar.db")
        _engine = create_engine(url, future=True)
        if url.startswith("sqlite"):

            @event.listens_for(_engine, "connect")
            def _fk(dbapi_conn, _rec):  # type: ignore[no-untyped-def]
                dbapi_conn.execute("PRAGMA foreign_keys=ON")

        Base.metadata.create_all(_engine)
        _session = sessionmaker(_engine, expire_on_commit=False)
    return _engine


def reset_engine() -> None:
    global _engine, _session
    if _engine is not None:
        _engine.dispose()
    _engine = None
    _session = None


@contextmanager
def session() -> Iterator[Session]:
    engine()
    assert _session is not None
    with _session() as s:
        yield s
        s.commit()
