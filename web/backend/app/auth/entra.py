"""
Entra ID (Azure AD) Authentication Module

Handles SSO authentication via Microsoft Entra ID using OAuth2/OIDC.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, Depends, HTTPException, status, Request
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from fastapi.responses import RedirectResponse
from pydantic import BaseModel
from typing import Optional
import httpx
import jwt
import logging
from datetime import datetime, timedelta
import os

from ..services.config_store import load_auth_config, save_auth_config

logger = logging.getLogger(__name__)

router = APIRouter()
security = HTTPBearer(auto_error=False)


# Entra ID Configuration (placeholders - replace with actual values)
class EntraConfig:
    TENANT_ID: str = os.getenv("ENTRA_TENANT_ID", "YOUR_TENANT_ID")
    CLIENT_ID: str = os.getenv("ENTRA_CLIENT_ID", "YOUR_CLIENT_ID")
    CLIENT_SECRET: str = os.getenv("ENTRA_CLIENT_SECRET", "YOUR_CLIENT_SECRET")
    REDIRECT_URI: str = os.getenv("ENTRA_REDIRECT_URI", "http://localhost:8000/auth/callback")
    SCOPE: str = "openid profile email"

    @property
    def authority(self) -> str:
        return f"https://login.microsoftonline.com/{self.TENANT_ID}"

    @property
    def authorize_url(self) -> str:
        return f"{self.authority}/oauth2/v2.0/authorize"

    @property
    def token_url(self) -> str:
        return f"{self.authority}/oauth2/v2.0/token"

    @property
    def jwks_url(self) -> str:
        return f"https://login.microsoftonline.com/{self.TENANT_ID}/discovery/v2.0/keys"


config = EntraConfig()


# Import User model
from ..models import User, UserRole


class TokenResponse(BaseModel):
    access_token: str
    token_type: str
    expires_in: int
    user: User


# Development mode flag
DEV_MODE = os.getenv("AUDYN_DEV_MODE", "true").lower() == "true"

# Dev mode user type: "admin" or "user" (set via query param or env)
DEV_USER_TYPE = os.getenv("AUDYN_DEV_USER_TYPE", "admin")

# Development users for testing
DEV_ADMIN = User(
    id="dev-user-001",
    email="admin@audyn.local",
    name="Admin User",
    role=UserRole.ADMIN,
    roles=["admin"]
)

DEV_STUDIO_USER = User(
    id="dev-user-002",
    email="user@audyn.local",
    name="Studio User",
    role=UserRole.STUDIO,
    studio_id="studio-a",
    roles=[]
)

# Current dev user (can be switched)
_current_dev_user_type = DEV_USER_TYPE


def get_dev_user() -> User:
    """Get the current dev mode user."""
    global _current_dev_user_type
    if _current_dev_user_type == "user":
        return DEV_STUDIO_USER
    return DEV_ADMIN


async def get_current_user(
    credentials: Optional[HTTPAuthorizationCredentials] = Depends(security)
) -> User:
    """
    Validate JWT token and return current user.
    In dev mode, returns a test user without authentication.
    """
    if DEV_MODE:
        logger.debug(f"Dev mode: returning {_current_dev_user_type} user")
        return get_dev_user()

    if credentials is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Not authenticated",
            headers={"WWW-Authenticate": "Bearer"},
        )

    token = credentials.credentials

    try:
        # In production, validate JWT against Entra ID
        payload = await validate_token(token)
        return User(
            id=payload.get("oid", payload.get("sub")),
            email=payload.get("email", payload.get("preferred_username", "")),
            name=payload.get("name", "Unknown"),
            roles=payload.get("roles", [])
        )
    except Exception as e:
        logger.error(f"Token validation failed: {e}")
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or expired token",
            headers={"WWW-Authenticate": "Bearer"},
        )


async def validate_token(token: str) -> dict:
    """Validate JWT token against Entra ID."""
    # Fetch JWKS keys
    async with httpx.AsyncClient() as client:
        resp = await client.get(config.jwks_url)
        jwks = resp.json()

    # Decode and validate token
    try:
        unverified_header = jwt.get_unverified_header(token)
        key = None
        for k in jwks.get("keys", []):
            if k["kid"] == unverified_header["kid"]:
                key = jwt.algorithms.RSAAlgorithm.from_jwk(k)
                break

        if key is None:
            raise ValueError("Key not found")

        payload = jwt.decode(
            token,
            key,
            algorithms=["RS256"],
            audience=config.CLIENT_ID,
            issuer=f"https://login.microsoftonline.com/{config.TENANT_ID}/v2.0"
        )
        return payload
    except jwt.ExpiredSignatureError:
        raise HTTPException(status_code=401, detail="Token expired")
    except jwt.InvalidTokenError as e:
        raise HTTPException(status_code=401, detail=f"Invalid token: {e}")


@router.get("/login")
async def login():
    """Redirect to Entra ID login."""
    if DEV_MODE:
        # In dev mode, return a mock token
        return {
            "dev_mode": True,
            "message": "Development mode - no login required",
            "user": get_dev_user().model_dump()
        }

    params = {
        "client_id": config.CLIENT_ID,
        "response_type": "code",
        "redirect_uri": config.REDIRECT_URI,
        "scope": config.SCOPE,
        "response_mode": "query"
    }
    auth_url = f"{config.authorize_url}?{'&'.join(f'{k}={v}' for k, v in params.items())}"
    return RedirectResponse(url=auth_url)


@router.get("/callback")
async def auth_callback(code: str):
    """Handle OAuth callback from Entra ID."""
    if DEV_MODE:
        return {"error": "Callback not used in dev mode"}

    async with httpx.AsyncClient() as client:
        resp = await client.post(
            config.token_url,
            data={
                "client_id": config.CLIENT_ID,
                "client_secret": config.CLIENT_SECRET,
                "code": code,
                "redirect_uri": config.REDIRECT_URI,
                "grant_type": "authorization_code",
                "scope": config.SCOPE
            }
        )

        if resp.status_code != 200:
            raise HTTPException(status_code=400, detail="Token exchange failed")

        token_data = resp.json()

        # Validate and extract user info
        user = await get_current_user(
            HTTPAuthorizationCredentials(
                scheme="Bearer",
                credentials=token_data["access_token"]
            )
        )

        return TokenResponse(
            access_token=token_data["access_token"],
            token_type="Bearer",
            expires_in=token_data.get("expires_in", 3600),
            user=user
        )


@router.get("/me")
async def get_me(user: User = Depends(get_current_user)):
    """Get current user info."""
    return user


@router.post("/logout")
async def logout():
    """Logout endpoint."""
    if DEV_MODE:
        return {"message": "Logged out (dev mode)"}

    logout_url = f"{config.authority}/oauth2/v2.0/logout"
    return {"logout_url": logout_url}


@router.post("/dev/switch-user")
async def switch_dev_user(user_type: str = "admin"):
    """Switch between admin and regular user in dev mode."""
    if not DEV_MODE:
        raise HTTPException(status_code=403, detail="Only available in dev mode")

    global _current_dev_user_type
    if user_type not in ["admin", "user"]:
        raise HTTPException(status_code=400, detail="user_type must be 'admin' or 'user'")

    _current_dev_user_type = user_type
    logger.info(f"Dev mode: switched to {user_type} user")

    return {
        "message": f"Switched to {user_type} user",
        "user": get_dev_user().model_dump()
    }


@router.get("/dev/current-user-type")
async def get_dev_user_type():
    """Get current dev mode user type."""
    if not DEV_MODE:
        raise HTTPException(status_code=403, detail="Only available in dev mode")

    return {
        "user_type": _current_dev_user_type,
        "user": get_dev_user().model_dump()
    }


# Auth configuration storage (in production, use secure storage like encrypted file or vault)
import bcrypt
import secrets

# Default auth config (from environment)
_auth_config = {
    "entra_tenant_id": config.TENANT_ID,
    "entra_client_id": config.CLIENT_ID,
    "entra_redirect_uri": config.REDIRECT_URI,
    "breakglass_password_hash": None  # bcrypt hashed password (includes salt)
}


def _load_auth_config_from_store():
    """Load auth config from persistent storage."""
    global _auth_config, config

    saved = load_auth_config()
    if saved:
        try:
            if saved.get("entra_tenant_id"):
                _auth_config["entra_tenant_id"] = saved["entra_tenant_id"]
                config.TENANT_ID = saved["entra_tenant_id"]
            if saved.get("entra_client_id"):
                _auth_config["entra_client_id"] = saved["entra_client_id"]
                config.CLIENT_ID = saved["entra_client_id"]
            if saved.get("entra_redirect_uri"):
                _auth_config["entra_redirect_uri"] = saved["entra_redirect_uri"]
                config.REDIRECT_URI = saved["entra_redirect_uri"]
            if saved.get("breakglass_password_hash"):
                _auth_config["breakglass_password_hash"] = saved["breakglass_password_hash"]

            logger.info("Loaded auth config from storage")
        except Exception as e:
            logger.error(f"Failed to parse saved auth config: {e}")


def _save_auth_config():
    """Save auth config to persistent storage."""
    # Only save non-sensitive data (no client_secret)
    data = {
        "entra_tenant_id": _auth_config.get("entra_tenant_id"),
        "entra_client_id": _auth_config.get("entra_client_id"),
        "entra_redirect_uri": _auth_config.get("entra_redirect_uri"),
        "breakglass_password_hash": _auth_config.get("breakglass_password_hash")
    }
    if not save_auth_config(data):
        logger.warning("Failed to persist auth config")


# Load config on module init
_load_auth_config_from_store()


def hash_password(password: str) -> str:
    """Hash password using bcrypt with automatic salt generation."""
    salt = bcrypt.gensalt(rounds=12)  # 12 rounds is secure and reasonably fast
    return bcrypt.hashpw(password.encode('utf-8'), salt).decode('utf-8')


def verify_password(password: str, hashed: str) -> bool:
    """Verify password against bcrypt hash."""
    try:
        return bcrypt.checkpw(password.encode('utf-8'), hashed.encode('utf-8'))
    except Exception:
        return False


class AuthConfigUpdate(BaseModel):
    """Auth configuration update request."""
    entra_tenant_id: Optional[str] = None
    entra_client_id: Optional[str] = None
    entra_client_secret: Optional[str] = None
    entra_redirect_uri: Optional[str] = None
    breakglass_password: Optional[str] = None


@router.get("/config")
async def get_auth_config(user: User = Depends(get_current_user)):
    """Get current auth configuration (admin only)."""
    if not user.is_admin:
        raise HTTPException(status_code=403, detail="Admin access required")

    return {
        "entra_tenant_id": _auth_config.get("entra_tenant_id", ""),
        "entra_client_id": _auth_config.get("entra_client_id", ""),
        "entra_redirect_uri": _auth_config.get("entra_redirect_uri", ""),
        "breakglass_configured": _auth_config.get("breakglass_password_hash") is not None
    }


@router.post("/config")
async def update_auth_config(
    update: AuthConfigUpdate,
    user: User = Depends(get_current_user)
):
    """Update auth configuration (admin only)."""
    if not user.is_admin:
        raise HTTPException(status_code=403, detail="Admin access required")

    global config

    if update.entra_tenant_id:
        _auth_config["entra_tenant_id"] = update.entra_tenant_id
        config.TENANT_ID = update.entra_tenant_id

    if update.entra_client_id:
        _auth_config["entra_client_id"] = update.entra_client_id
        config.CLIENT_ID = update.entra_client_id

    if update.entra_client_secret:
        config.CLIENT_SECRET = update.entra_client_secret

    if update.entra_redirect_uri:
        _auth_config["entra_redirect_uri"] = update.entra_redirect_uri
        config.REDIRECT_URI = update.entra_redirect_uri

    if update.breakglass_password:
        # Hash the breakglass password using bcrypt (includes automatic salt)
        _auth_config["breakglass_password_hash"] = hash_password(update.breakglass_password)

    # Persist changes (excluding client_secret which stays in env)
    _save_auth_config()
    logger.info(f"Auth config updated by {user.email}")

    return {"message": "Auth configuration updated"}


class BreakglassLogin(BaseModel):
    """Breakglass login request."""
    password: str


@router.post("/breakglass")
async def breakglass_login(request: BreakglassLogin):
    """Emergency login with breakglass password."""
    if not _auth_config.get("breakglass_password_hash"):
        raise HTTPException(status_code=403, detail="Breakglass password not configured")

    # Use constant-time comparison via bcrypt to prevent timing attacks
    if not verify_password(request.password, _auth_config["breakglass_password_hash"]):
        logger.warning("Failed breakglass login attempt")
        raise HTTPException(status_code=401, detail="Invalid breakglass password")

    logger.info("Successful breakglass login")

    # Return admin user for breakglass access
    return {
        "message": "Breakglass login successful",
        "user": DEV_ADMIN.model_dump()
    }
