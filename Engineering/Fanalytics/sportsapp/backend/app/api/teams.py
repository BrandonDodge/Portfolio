from fastapi import APIRouter, Query
from sportsapp.backend.app.db.connection import supabase_anon
from typing import Optional
import logging

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/teams", tags=["teams"])

@router.get("/")
async def list_teams(
    sport: Optional[str] = Query(None, description="Filter by sport (e.g., NBA, UFC)"),
    name: Optional[str] = Query(None, description="Filter by team name (partial match)"),
    limit: int = Query(50, ge=1, le=100, description="Max rows to return")
):
    """List teams with optional filters."""
    try:
        query = supabase_anon.table("teams").select("id, sport, name, short_name, market")
        if sport:
            query = query.eq("sport", sport)
        if name:
            query = query.ilike("name", f"%{name}%")
        query = query.limit(limit)
        res = query.execute()
        logger.info(f"Fetched {len(res.data)} teams (sport={sport}, name={name})")
        return res.data
    except Exception as e:
        logger.error(f"Supabase query failed: {e}")
        return {"error": "Failed to fetch teams"}