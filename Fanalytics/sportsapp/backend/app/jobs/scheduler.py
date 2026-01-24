from apscheduler.schedulers.asyncio import AsyncIOScheduler
import sys
from pathlib import Path
import logging

PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from sportsapp.backend.app.jobs.ingest_nba import ingest_nba_teams, ingest_ufc_data, ingest_nfl_teams

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

scheduler = AsyncIOScheduler()

def setup_jobs():
    """Configure background jobs for data ingestion."""
    # NBA: Every 5 minutes (frequent for live scores)
    scheduler.add_job(
        ingest_nba_teams,
        "interval",
        minutes=5,
        id="nba_ingest",
        max_instances=1,
        replace_existing=True
    )
    # UFC: Every 10 minutes (less frequent, event-based)
    scheduler.add_job(
        ingest_ufc_data,
        "interval",
        minutes=10,
        id="ufc_ingest",
        max_instances=1,
        replace_existing=True
    )
    # NFL: Every 15 minutes (less frequent, event-based)
    scheduler.add_job(
        ingest_nfl_teams,
        "interval",
        minutes=15,
        id="nfl_ingest",
        max_instances=1,
        replace_existing=True
    )
    scheduler.start()
    logger.info("Scheduler started: NBA (5min), UFC (10min)")