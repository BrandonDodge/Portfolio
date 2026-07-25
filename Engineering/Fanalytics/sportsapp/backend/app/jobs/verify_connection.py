"""
Quick verification script to test Supabase connection and API configuration.
Run this to verify your setup before running the full ingest script.
"""
import os
import sys
import asyncio
import httpx
from pathlib import Path
from dotenv import load_dotenv

# Get the sportsapp directory (4 levels up from this file)
# verify_connection.py -> jobs -> app -> backend -> sportsapp
SPORTSAPP_DIR = Path(__file__).resolve().parent.parent.parent.parent
ENV_FILE = SPORTSAPP_DIR / ".env"

# Add the parent directory (containing sportsapp) to sys.path so imports work
# This allows the script to be run directly: python sportsapp/backend/app/jobs/verify_connection.py
PROJECT_ROOT = SPORTSAPP_DIR.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

# Load .env from sportsapp directory
# Use override=False to not overwrite existing env vars (from system)
load_dotenv(dotenv_path=ENV_FILE, override=False)

# Verify env vars are loaded before importing connection
SUPABASE_URL = os.getenv("SUPABASE_URL")
SUPABASE_ANON_KEY = os.getenv("SUPABASE_ANON_KEY")
SUPABASE_SERVICE_ROLE_KEY = os.getenv("SUPABASE_SERVICE_ROLE_KEY")

# Import Supabase clients only if env vars are set
try:
    from sportsapp.backend.app.db.connection import supabase_service, supabase_anon
    SUPABASE_AVAILABLE = True
except Exception as e:
    SUPABASE_AVAILABLE = False
    supabase_service = None
    supabase_anon = None
    # Store exception for debugging
    IMPORT_ERROR = str(e)

def verify_env_vars():
    """Verify all required environment variables are set."""
    print("=" * 60)
    print("1. Checking Environment Variables")
    print("=" * 60)
    
    required_vars = [
        "SUPABASE_URL",
        "SUPABASE_ANON_KEY", 
        "SUPABASE_SERVICE_ROLE_KEY",
        "SPORTS_DATAIO_KEY"
    ]
    
    all_present = True
    for var in required_vars:
        value = os.getenv(var)
        if value:
            # Mask sensitive values
            masked = value[:8] + "..." + value[-4:] if len(value) > 12 else "***"
            print(f"✅ {var}: {masked}")
        else:
            print(f"❌ {var}: NOT SET")
            all_present = False
    
    print()
    return all_present

def verify_supabase_connection():
    """Verify Supabase connection works."""
    print("=" * 60)
    print("2. Testing Supabase Connection")
    print("=" * 60)
    
    if not SUPABASE_AVAILABLE:
        print("❌ Cannot test Supabase connection - Supabase clients failed to initialize")
        if 'IMPORT_ERROR' in globals():
            print(f"   Error: {IMPORT_ERROR}")
        else:
            print("   (Check that environment variables are set correctly)")
        print()
        return False
    
    try:
        # Test service role client (for backend operations)
        print(f"Testing service role client...")
        print(f"Supabase URL: {SUPABASE_URL}")
        
        # Try a simple query to verify connection
        # This will fail if connection is bad, but won't error if tables don't exist
        try:
            result = supabase_service.table("events").select("id").limit(1).execute()
            print(f"✅ Service role client connected successfully")
            print(f"   Sample query returned {len(result.data)} result(s)")
        except Exception as e:
            print(f"⚠️  Service role client created, but query failed:")
            print(f"   {str(e)[:100]}")
            print(f"   (This might be normal if tables don't exist yet)")
        
        # Test anon client
        print(f"\nTesting anon client...")
        try:
            result = supabase_anon.table("events").select("id").limit(1).execute()
            print(f"✅ Anon client connected successfully")
        except Exception as e:
            print(f"⚠️  Anon client created, but query failed:")
            print(f"   {str(e)[:100]}")
        
        print()
        return True
        
    except Exception as e:
        print(f"❌ Supabase connection failed: {e}")
        print()
        return False

async def verify_api_connection():
    """Verify API connection works."""
    print("=" * 60)
    print("3. Testing SportsData.io API Connection")
    print("=" * 60)
    
    API_KEY = os.getenv("SPORTS_DATAIO_KEY")
    if not API_KEY:
        print("❌ SPORTS_DATAIO_KEY not set")
        print()
        return False
    
    # Debug: Show API key (masked)
    masked_key = API_KEY[:8] + "..." + API_KEY[-4:] if len(API_KEY) > 12 else "***"
    print(f"API Key: {masked_key}")
    print()
    
    # Use correct URL format: base_url + endpoint
    base_url = "https://api.sportsdata.io/v3/nfl"
    endpoint = "scores/json/Teams"
    full_url = f"{base_url}/{endpoint}?key={API_KEY}"
    headers = {"Ocp-Apim-Subscription-Key": API_KEY}
    
    print(f"Base URL: {base_url}")
    print(f"Endpoint: {endpoint}")
    print(f"Full URL: {full_url}")
    print()
    
    try:
        async with httpx.AsyncClient(timeout=10.0) as client:
            print(f"Making API request...")
            resp = await client.get(full_url, headers=headers)
            
            print(f"Response Status: {resp.status_code}")
            print(f"Response Headers: {dict(resp.headers)}")
            
            if resp.status_code == 200:
                try:
                    data = resp.json()
                    print(f"✅ API connection successful!")
                    print(f"   Received {len(data)} team(s)")
                    if data:
                        sample = data[0]
                        print(f"   Sample team: {sample.get('Key', 'N/A')} - {sample.get('FullName', 'N/A')}")
                except Exception as json_err:
                    print(f"⚠️  Got 200 response but JSON parsing failed: {json_err}")
                    print(f"   Response text (first 200 chars): {resp.text[:200]}")
            elif resp.status_code == 401:
                print(f"❌ API authentication failed (401 Unauthorized)")
                print(f"   Check that your API key is correct and active")
                print(f"   Response: {resp.text[:200]}")
                return False
            elif resp.status_code == 403:
                print(f"❌ API access forbidden (403)")
                print(f"   Your API key may not have access to this endpoint")
                print(f"   Response: {resp.text[:200]}")
                return False
            elif resp.status_code == 404:
                print(f"❌ API endpoint not found (404)")
                print(f"   The endpoint URL may be incorrect")
                print(f"   Response: {resp.text[:200]}")
                print(f"   Try checking the SportsData.io API documentation")
                return False
            else:
                print(f"❌ API returned error status: {resp.status_code}")
                print(f"   Response: {resp.text[:500]}")
                return False
            
        print()
        return True
        
    except httpx.TimeoutException:
        print(f"❌ API request timed out after 10 seconds")
        print(f"   Check your internet connection")
        print()
        return False
    except httpx.ConnectError as e:
        print(f"❌ Connection error: {e}")
        print(f"   Could not reach the API server")
        print()
        return False
    except Exception as e:
        print(f"❌ API connection failed: {type(e).__name__}: {e}")
        print()
        return False

async def main():
    """Run all verification checks."""
    print("\n" + "=" * 60)
    print("VERIFICATION TEST")
    print("=" * 60 + "\n")
    
    # Run checks
    env_ok = verify_env_vars()
    supabase_ok = verify_supabase_connection()
    api_ok = await verify_api_connection()
    
    # Summary
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"Environment Variables: {'✅ PASS' if env_ok else '❌ FAIL'}")
    print(f"Supabase Connection:   {'✅ PASS' if supabase_ok else '❌ FAIL'}")
    print(f"API Connection:        {'✅ PASS' if api_ok else '❌ FAIL'}")
    print()
    
    if env_ok and supabase_ok and api_ok:
        print("🎉 All checks passed! Your setup looks good.")
    else:
        print("⚠️  Some checks failed. Please review the errors above.")
    print()

if __name__ == "__main__":
    asyncio.run(main())

