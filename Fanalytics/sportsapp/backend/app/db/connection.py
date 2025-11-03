import os
from supabase import create_client, Client
from dotenv import load_dotenv

load_dotenv()

SUPABASE_URL = os.getenv("SUPABASE_URL")
SUPABASE_ANON_KEY = os.getenv("SUPABASE_ANON_KEY")
SUPABASE_SERVICE_ROLE_KEY = os.getenv("SUPABASE_SERVICE_ROLE_KEY")

# Public client (for API reads, respects RLS)
supabase_anon: Client = create_client(SUPABASE_URL, SUPABASE_ANON_KEY)

# Service role client (for backend jobs/inserts, bypasses RLS)
supabase_service: Client = create_client(SUPABASE_URL, SUPABASE_SERVICE_ROLE_KEY)

# Optional: Dependency for FastAPI protected routes (we'll use later)
# from fastapi import Depends
# def get_supabase_service() -> Client:
#     return supabase_service 