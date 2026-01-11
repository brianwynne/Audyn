"""
Configuration Store Service

File-based persistence for Audyn configuration and settings.
Uses JSON format for human readability and easy editing.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

import json
import os
import logging
import fcntl
from pathlib import Path
from typing import Any, Optional
from datetime import datetime
from copy import deepcopy

logger = logging.getLogger(__name__)

# Default config directory - can be overridden by AUDYN_CONFIG_DIR env var
DEFAULT_CONFIG_DIR = os.path.expanduser("~/.config/audyn")
CONFIG_DIR = os.getenv("AUDYN_CONFIG_DIR", DEFAULT_CONFIG_DIR)

# Config file paths
CONFIG_FILES = {
    "global": "global.json",        # Global capture settings (archive, PTP, etc.)
    "recorders": "recorders.json",  # Recorder configurations
    "studios": "studios.json",      # Studio definitions
    "sources": "sources.json",      # AES67 source configurations
    "auth": "auth.json",            # Authentication settings (Entra ID, breakglass)
    "system": "system.json",        # System settings (hostname, timezone, NTP)
    "ssl": "ssl.json",              # SSL certificate configuration
}


class ConfigStore:
    """
    Thread-safe file-based configuration store.

    Provides atomic read/write operations with file locking.
    Configuration is stored as JSON files in the config directory.
    """

    def __init__(self, config_dir: str = None):
        self.config_dir = Path(config_dir or CONFIG_DIR)
        self._ensure_config_dir()
        self._cache: dict[str, Any] = {}
        self._cache_time: dict[str, datetime] = {}

    def _ensure_config_dir(self):
        """Create config directory if it doesn't exist."""
        try:
            self.config_dir.mkdir(parents=True, exist_ok=True)
            logger.info(f"Config directory: {self.config_dir}")
        except Exception as e:
            logger.error(f"Failed to create config directory {self.config_dir}: {e}")
            raise

    def _get_path(self, config_name: str) -> Path:
        """Get the file path for a configuration."""
        if config_name not in CONFIG_FILES:
            raise ValueError(f"Unknown config: {config_name}. Valid: {list(CONFIG_FILES.keys())}")
        return self.config_dir / CONFIG_FILES[config_name]

    def load(self, config_name: str, default: Any = None) -> Any:
        """
        Load configuration from file.

        Args:
            config_name: Name of the configuration (global, recorders, etc.)
            default: Default value if file doesn't exist

        Returns:
            The configuration data or default value
        """
        path = self._get_path(config_name)

        if not path.exists():
            logger.debug(f"Config file {path} not found, using default")
            return deepcopy(default) if default is not None else None

        try:
            with open(path, 'r') as f:
                # Acquire shared lock for reading
                fcntl.flock(f.fileno(), fcntl.LOCK_SH)
                try:
                    data = json.load(f)
                    self._cache[config_name] = data
                    self._cache_time[config_name] = datetime.now()
                    logger.debug(f"Loaded config: {config_name}")
                    return data
                finally:
                    fcntl.flock(f.fileno(), fcntl.LOCK_UN)
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON in {path}: {e}")
            return deepcopy(default) if default is not None else None
        except Exception as e:
            logger.error(f"Failed to load config {config_name}: {e}")
            return deepcopy(default) if default is not None else None

    def save(self, config_name: str, data: Any) -> bool:
        """
        Save configuration to file atomically.

        Uses write-to-temp-then-rename pattern for atomic updates.

        Args:
            config_name: Name of the configuration
            data: Data to save (must be JSON serializable)

        Returns:
            True if successful, False otherwise
        """
        path = self._get_path(config_name)
        temp_path = path.with_suffix('.tmp')

        try:
            # Write to temp file first
            with open(temp_path, 'w') as f:
                # Acquire exclusive lock for writing
                fcntl.flock(f.fileno(), fcntl.LOCK_EX)
                try:
                    json.dump(data, f, indent=2, default=self._json_serializer)
                    f.flush()
                    os.fsync(f.fileno())
                finally:
                    fcntl.flock(f.fileno(), fcntl.LOCK_UN)

            # Atomic rename
            os.rename(temp_path, path)

            # Update cache
            self._cache[config_name] = data
            self._cache_time[config_name] = datetime.now()

            logger.info(f"Saved config: {config_name}")
            return True

        except Exception as e:
            logger.error(f"Failed to save config {config_name}: {e}")
            # Clean up temp file if it exists
            if temp_path.exists():
                try:
                    temp_path.unlink()
                except:
                    pass
            return False

    def delete(self, config_name: str) -> bool:
        """Delete a configuration file."""
        path = self._get_path(config_name)

        try:
            if path.exists():
                path.unlink()
                logger.info(f"Deleted config: {config_name}")

            # Clear cache
            self._cache.pop(config_name, None)
            self._cache_time.pop(config_name, None)
            return True

        except Exception as e:
            logger.error(f"Failed to delete config {config_name}: {e}")
            return False

    def exists(self, config_name: str) -> bool:
        """Check if a configuration file exists."""
        return self._get_path(config_name).exists()

    def get_cached(self, config_name: str) -> Optional[Any]:
        """Get cached configuration without reading from disk."""
        return self._cache.get(config_name)

    def _json_serializer(self, obj: Any) -> Any:
        """Custom JSON serializer for special types."""
        if isinstance(obj, datetime):
            return obj.isoformat()
        if hasattr(obj, 'value'):  # Enum
            return obj.value
        if hasattr(obj, 'model_dump'):  # Pydantic model
            return obj.model_dump()
        if hasattr(obj, '__dict__'):
            return obj.__dict__
        raise TypeError(f"Object of type {type(obj)} is not JSON serializable")

    def backup(self, config_name: str) -> Optional[Path]:
        """Create a backup of a configuration file."""
        path = self._get_path(config_name)

        if not path.exists():
            return None

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        backup_path = path.with_suffix(f'.{timestamp}.bak')

        try:
            import shutil
            shutil.copy2(path, backup_path)
            logger.info(f"Created backup: {backup_path}")
            return backup_path
        except Exception as e:
            logger.error(f"Failed to backup config {config_name}: {e}")
            return None

    def list_backups(self, config_name: str) -> list[Path]:
        """List all backups for a configuration."""
        path = self._get_path(config_name)
        pattern = f"{path.stem}.*.bak"
        return sorted(self.config_dir.glob(pattern), reverse=True)


# Singleton instance
_store: Optional[ConfigStore] = None


def get_config_store() -> ConfigStore:
    """Get the configuration store singleton."""
    global _store
    if _store is None:
        _store = ConfigStore()
    return _store


# Convenience functions for common operations

def load_global_config() -> Optional[dict]:
    """Load global capture configuration."""
    return get_config_store().load("global")


def save_global_config(config: dict) -> bool:
    """Save global capture configuration."""
    return get_config_store().save("global", config)


def load_recorders_config() -> Optional[dict]:
    """Load recorder configurations."""
    return get_config_store().load("recorders")


def save_recorders_config(recorders: dict) -> bool:
    """Save recorder configurations."""
    return get_config_store().save("recorders", recorders)


def load_studios_config() -> Optional[dict]:
    """Load studio configurations."""
    return get_config_store().load("studios")


def save_studios_config(studios: dict) -> bool:
    """Save studio configurations."""
    return get_config_store().save("studios", studios)


def load_sources_config() -> Optional[dict]:
    """Load AES67 source configurations."""
    return get_config_store().load("sources")


def save_sources_config(sources: dict) -> bool:
    """Save AES67 source configurations."""
    return get_config_store().save("sources", sources)


def load_auth_config() -> Optional[dict]:
    """Load authentication configuration."""
    return get_config_store().load("auth")


def save_auth_config(auth: dict) -> bool:
    """Save authentication configuration."""
    return get_config_store().save("auth", auth)


def load_system_config() -> Optional[dict]:
    """Load system configuration."""
    return get_config_store().load("system")


def save_system_config(system: dict) -> bool:
    """Save system configuration."""
    return get_config_store().save("system", system)


def load_ssl_config() -> Optional[dict]:
    """Load SSL certificate configuration."""
    return get_config_store().load("ssl")


def save_ssl_config(ssl: dict) -> bool:
    """Save SSL certificate configuration."""
    return get_config_store().save("ssl", ssl)
