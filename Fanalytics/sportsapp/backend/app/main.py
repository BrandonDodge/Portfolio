from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from apscheduler.schedulers.asyncio import AsyncIOScheduler
import sys
from pathlib import Path
import logging

# Configure module path and logging
PROJECT_ROOT = Path(__file__).resolve().parents[2]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from sportsapp.backend.app.jobs.scheduler import setup_jobs
from sportsapp.backend.app.api import events, teams, players

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Initialize FastAPI app
app = FastAPI(title="Sports Stats API", description="API for sports analytics and news", version="1.0.0")

# Add CORS middleware for frontend (React Native/PWA)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # Adjust to specific origins (e.g., http://localhost:3000) in production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(events.router)
app.include_router(teams.router)
app.include_router(players.router)

# Health check endpoint
@app.get("/health")
async def health_check():
    logger.info("Health check requested")
    return {"status": "ok"}

# Start scheduler on app startup
@app.on_event("startup")
async def startup_event():
    logger.info("Starting FastAPI app and scheduler")
    setup_jobs()

# Placeholder for routers (to be added later)
# app.include_router(events.router, prefix="/events", tags=["events"])