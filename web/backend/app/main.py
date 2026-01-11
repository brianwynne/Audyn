"""
Audyn Web - Control and Asset Management Server

Enterprise web interface for Audyn AES67 audio capture.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import FastAPI, Depends, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from contextlib import asynccontextmanager
import logging

from .auth.entra import get_current_user, router as auth_router
from .api.control import router as control_router
from .api.sources import router as sources_router
from .api.assets import router as assets_router
from .api.stream import router as stream_router
from .api.recorders import router as recorders_router
from .api.studios import router as studios_router
from .api.system import router as system_router
from .websocket.levels import router as ws_router
from .services.audyn import AudynService
from .services.recorder_manager import get_recorder_manager

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Global service instance
audyn_service: AudynService = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan handler."""
    global audyn_service

    logger.info("Starting Audyn Web Service...")
    audyn_service = AudynService()
    await audyn_service.initialize()

    # Initialize recorder manager
    recorder_manager = get_recorder_manager()
    await recorder_manager.initialize()

    yield

    logger.info("Shutting down Audyn Web Service...")
    await recorder_manager.shutdown()
    await audyn_service.shutdown()


app = FastAPI(
    title="Audyn Web",
    description="Enterprise control interface for Audyn AES67 audio capture",
    version="1.0.0",
    lifespan=lifespan
)

# CORS middleware for development
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "http://localhost:3000"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Include routers
app.include_router(auth_router, prefix="/auth", tags=["Authentication"])
app.include_router(control_router, prefix="/api/control", tags=["Control"])
app.include_router(sources_router, prefix="/api/sources", tags=["Sources"])
app.include_router(recorders_router, prefix="/api/recorders", tags=["Recorders"])
app.include_router(studios_router, prefix="/api/studios", tags=["Studios"])
app.include_router(assets_router, prefix="/api/assets", tags=["Assets"])
app.include_router(stream_router, prefix="/api/stream", tags=["Streaming"])
app.include_router(system_router, prefix="/api/system", tags=["System"])
app.include_router(ws_router, prefix="/ws", tags=["WebSocket"])


@app.get("/health")
async def health_check():
    """Health check endpoint."""
    return {"status": "healthy", "service": "audyn-web"}


@app.get("/api/status")
async def get_status(user: dict = Depends(get_current_user)):
    """Get overall system status."""
    global audyn_service
    return await audyn_service.get_status()


def get_audyn_service() -> AudynService:
    """Dependency to get Audyn service instance."""
    global audyn_service
    if audyn_service is None:
        raise HTTPException(status_code=503, detail="Service not initialized")
    return audyn_service
