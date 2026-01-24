from fastapi import APIRouter, Query
from sportsapp.backend.app.db.connection import supabase_anon
from typing import Optional
import logging

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/players", tags=["players"])

@router.get("/")
async def list_players(
    sport: Optional[str] = Query(None, description="Filter by sport (e.g., NBA, UFC)"),
    team_id: Optional[int] = Query(None, description="Filter by team ID"),
    name: Optional[str] = Query(None, description="Filter by player name (partial match)"),
    limit: int = Query(50, ge=1, le=100, description="Max rows to return")
):
    """List players with optional filters."""
    try:
        query = supabase_anon.table("players").select("id, sport, first_name, last_name, position, status, team_id")
        if sport:
            query = query.eq("sport", sport)
        if team_id:
            query = query.eq("team_id", team_id)
        if name:
            query = query.ilike("last_name", f"%{name}%")
        query = query.limit(limit)
        res = query.execute()
        logger.info(f"Fetched {len(res.data)} players (sport={sport}, team_id={team_id}, name={name})")
        return res.data
    except Exception as e:
        logger.error(f"Supabase query failed: {e}")
        return {"error": "Failed to fetch players"}