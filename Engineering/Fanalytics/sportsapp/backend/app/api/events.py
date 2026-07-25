from fastapi import APIRouter, Query
from sportsapp.backend.app.db.connection import supabase_anon
from typing import Optional
import logging

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/events", tags=["events"])

@router.get("/")
async def list_events(
    sport: Optional[str] = Query(None, description="Filter by sport (e.g., NBA, UFC)"),
    status: Optional[str] = Query(None, description="Filter by status (e.g., Scheduled, InProgress)"),
    limit: int = Query(50, ge=1, le=100, description="Max rows to return")
):
    """List events with optional filters."""
    try:
        query = supabase_anon.table("events").select("id, sport, season, start_time, venue, status, home_team_id, away_team_id")
        if sport:
            query = query.eq("sport", sport)
        if status:
            query = query.eq("status", status)
        query = query.limit(limit)
        res = query.execute()
        logger.info(f"Fetched {len(res.data)} events (sport={sport}, status={status})")
        return res.data
    except Exception as e:
        logger.error(f"Supabase query failed: {e}")
        return {"error": "Failed to fetch events"}