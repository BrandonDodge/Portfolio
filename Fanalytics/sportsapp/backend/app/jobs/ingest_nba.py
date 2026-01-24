import httpx
import os
import sys
from datetime import datetime
from pathlib import Path
from dotenv import load_dotenv
from supabase import Client

# Get the sportsapp directory (4 levels up from this file)
# ingest_nba.py -> jobs -> app -> backend -> sportsapp
SPORTSAPP_DIR = Path(__file__).resolve().parent.parent.parent.parent
ENV_FILE = SPORTSAPP_DIR / ".env"

# Add the parent directory (containing sportsapp) to sys.path so imports work
# This allows the script to be run directly: python sportsapp/backend/app/jobs/ingest_nba.py
PROJECT_ROOT = SPORTSAPP_DIR.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

# Load .env from sportsapp directory
load_dotenv(dotenv_path=ENV_FILE)

# Import Supabase client after path setup
from sportsapp.backend.app.db.connection import supabase_service

API_KEY = os.getenv("SPORTS_DATAIO_KEY")

async def ingest_ufc_data():
    """Fetch UFC events, fighters, and stats; insert to Supabase."""
    base_url = "https://api.sportsdata.io/v3/mma"
    headers = {"Ocp-Apim-Subscription-Key": API_KEY}
    
#https://api.sportsdata.io/v3/mma/scores/json/FightersBasic?key=8402dba692fc47e1b7ce0c514a2f6c84

    db = supabase_service
    
    async with httpx.AsyncClient() as client:
        # 1. Fetch upcoming/recent events (fights)
        resp = await client.get(f"{base_url}/scores/json/FightersBasic?key={API_KEY}", headers=headers)
        if resp.status_code != 200:
            print(f"API Error: {resp.status_code} - {resp.text}")
            return
        fighter_data = resp.json()  # List of fight events
    
    print(f"Fetched {len(fighter_data)} UFC Fighters (scrambled trial data).")
    
    # Insert fighters as players (only use fields that exist in schema)
    # Schema: sport, ext_ref, team_id, first_name, last_name, position, status, market, nickname
    success_count = 0
    for fighter in fighter_data[:10]:  # Limit for test
        player_data = {
            "sport": "UFC",
            "ext_ref": str(fighter.get("FighterID", fighter.get("FighterKey", ""))),  # Use FighterID or FighterKey as unique ref
            "team_id": None,  # UFC: No teams
            "first_name": fighter.get("FirstName", ""),
            "last_name": fighter.get("LastName", ""),
            "position": fighter.get("WeightClass", ""),  # e.g., "Welterweight"
            "status": fighter.get("Status", "Active"),
            "market": fighter.get("WeightClass", ""),  # Weight class as market
            "nickname": fighter.get("Nickname", ""),  # Optional nickname
        }
        
        try:
            res = db.table("players").upsert(player_data, on_conflict="ext_ref").execute()
            success_count += 1
            print(f"✅ Fighter upserted: {player_data['first_name']} {player_data['last_name']} ({player_data['ext_ref']})")
        except Exception as e:
            print(f"❌ Failed to upsert fighter {player_data.get('first_name', 'Unknown')}: {e}")
    
    print(f"\n✅ Successfully processed {success_count}/{min(10, len(fighter_data))} fighters")
        
async def ingest_nba_teams():
    """Fetch NBA teams from API and insert/update in Supabase."""
    base_url = "https://api.sportsdata.io/v3/nba"
    headers = {"Ocp-Apim-Subscription-Key": API_KEY}
    
    db = supabase_service
    
    async with httpx.AsyncClient() as client:
        # Fetch all NBA teams
        resp = await client.get(f"{base_url}/scores/json/Teams?key={API_KEY}", headers=headers)
        if resp.status_code != 200:
            print(f"API Error: {resp.status_code} - {resp.text}")
            return
        teams_data = resp.json()
    
    print(f"Fetched {len(teams_data)} NBA teams from API.")
    
    # Insert/update teams in database
    # Schema: sport, ext_ref, name, short_name, market
    # IMPORTANT: Make ext_ref unique per sport to avoid conflicts between NFL/NBA/etc
    success_count = 0
    for team in teams_data:
        team_data = {
            "sport": "NBA",
            "ext_ref": f"NBA_{team.get('TeamID', '')}",  # Prepend sport to make unique across sports
            "name": team.get("FullName", ""),  # Full name: "Los Angeles Lakers"
            "short_name": team.get("Key", ""),  # Short abbreviation: "LAL"
            "market": f"{team.get('Conference', '')} {team.get('Division', '')}".strip()  # e.g., "Western Pacific"
        }
        try:
            res = db.table("teams").upsert(team_data, on_conflict="ext_ref").execute()
            success_count += 1
            print(f"✅ Team upserted: {team_data['name']} ({team_data['short_name']}) - {team_data['market']}")
        except Exception as e:
            print(f"❌ Failed to upsert team {team_data.get('name', 'Unknown')}: {e}")
    
    print(f"\n✅ Successfully processed {success_count}/{len(teams_data)} teams")
        

async def ingest_nfl_teams():
    """Fetch NFL teams from API and insert/update in Supabase."""
    base_url = "https://api.sportsdata.io/v3/nfl"
    headers = {"Ocp-Apim-Subscription-Key": API_KEY}
    
    db = supabase_service
    
    async with httpx.AsyncClient() as client:
        # Fetch all NFL teams
        resp = await client.get(f"{base_url}/scores/json/Teams?key={API_KEY}", headers=headers)
        if resp.status_code != 200:
            print(f"API Error: {resp.status_code} - {resp.text}")
            return
        teams_data = resp.json()
    
    print(f"Fetched {len(teams_data)} NFL teams from API.")
    
    # Insert/update teams in database
    # Schema: sport, ext_ref, name, short_name, market
    # IMPORTANT: Make ext_ref unique per sport to avoid conflicts between NFL/NBA/etc
    success_count = 0
    for team in teams_data:
        team_data = {
            "sport": "NFL",
            "ext_ref": f"NFL_{team.get('TeamID', '')}",  # Prepend sport to make unique across sports
            "name": team.get("FullName", ""),  # Full name: "Arizona Cardinals"
            "short_name": team.get("Key", ""),  # Short abbreviation: "ARI"
            "market": f"{team.get('Conference', '')} {team.get('Division', '')}".strip()  # e.g., "NFC West"
        }
        
        try:
            res = db.table("teams").upsert(team_data, on_conflict="ext_ref").execute()
            success_count += 1
            print(f"✅ Team upserted: {team_data['name']} ({team_data['short_name']}) - {team_data['market']}")
        except Exception as e:
            print(f"❌ Failed to upsert team {team_data.get('name', 'Unknown')}: {e}")
    
    print(f"\n✅ Successfully processed {success_count}/{len(teams_data)} teams")

# Run the test
if __name__ == "__main__":
    import asyncio
    # Choose which function to run
    asyncio.run(ingest_ufc_data())
    asyncio.run(ingest_nfl_teams())
    asyncio.run(ingest_nba_teams())