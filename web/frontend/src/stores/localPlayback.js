/**
 * Local Playback Store
 *
 * Manages local folder access for direct file playback using
 * the File System Access API. This provides buffer-free playback
 * for on-air use when files are synced locally via SMB/rsync.
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

// IndexedDB helpers for persisting directory handle
const DB_NAME = 'audyn-local-playback'
const STORE_NAME = 'handles'

async function openDB() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, 1)
    request.onerror = () => reject(request.error)
    request.onsuccess = () => resolve(request.result)
    request.onupgradeneeded = (event) => {
      event.target.result.createObjectStore(STORE_NAME)
    }
  })
}

async function saveHandle(handle) {
  const db = await openDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, 'readwrite')
    const store = tx.objectStore(STORE_NAME)
    const request = store.put(handle, 'directoryHandle')
    request.onerror = () => reject(request.error)
    request.onsuccess = () => resolve()
  })
}

async function loadHandle() {
  const db = await openDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, 'readonly')
    const store = tx.objectStore(STORE_NAME)
    const request = store.get('directoryHandle')
    request.onerror = () => reject(request.error)
    request.onsuccess = () => resolve(request.result)
  })
}

async function clearHandle() {
  const db = await openDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_NAME, 'readwrite')
    const store = tx.objectStore(STORE_NAME)
    const request = store.delete('directoryHandle')
    request.onerror = () => reject(request.error)
    request.onsuccess = () => resolve()
  })
}

export const useLocalPlaybackStore = defineStore('localPlayback', () => {
  // State
  const enabled = ref(false)
  const directoryHandle = ref(null)
  const directoryName = ref('')
  const hasPermission = ref(false)
  const error = ref(null)

  // Check if File System Access API is supported
  const isSupported = computed(() => {
    return 'showDirectoryPicker' in window
  })

  // Check if local playback is available (supported + enabled + has permission)
  const isAvailable = computed(() => {
    return isSupported.value && enabled.value && hasPermission.value && directoryHandle.value
  })

  /**
   * Request access to a local folder
   */
  async function selectFolder() {
    if (!isSupported.value) {
      error.value = 'File System Access API not supported in this browser'
      return false
    }

    try {
      const handle = await window.showDirectoryPicker({
        mode: 'read'
      })

      directoryHandle.value = handle
      directoryName.value = handle.name
      hasPermission.value = true
      enabled.value = true
      error.value = null

      // Persist handle for future sessions
      await saveHandle(handle)

      return true
    } catch (err) {
      if (err.name === 'AbortError') {
        // User cancelled - not an error
        return false
      }
      error.value = err.message
      return false
    }
  }

  /**
   * Re-request permission for previously saved folder
   */
  async function requestPermission() {
    if (!directoryHandle.value) {
      return false
    }

    try {
      const permission = await directoryHandle.value.requestPermission({ mode: 'read' })
      hasPermission.value = permission === 'granted'
      return hasPermission.value
    } catch (err) {
      error.value = err.message
      hasPermission.value = false
      return false
    }
  }

  /**
   * Check if we still have permission (call on app start)
   */
  async function checkPermission() {
    if (!directoryHandle.value) {
      return false
    }

    try {
      const permission = await directoryHandle.value.queryPermission({ mode: 'read' })
      hasPermission.value = permission === 'granted'
      return hasPermission.value
    } catch (err) {
      hasPermission.value = false
      return false
    }
  }

  /**
   * Load saved directory handle from IndexedDB
   */
  async function loadSavedFolder() {
    if (!isSupported.value) {
      return false
    }

    try {
      const handle = await loadHandle()
      if (handle) {
        directoryHandle.value = handle
        directoryName.value = handle.name
        enabled.value = true
        // Check if we still have permission
        await checkPermission()
        return true
      }
    } catch (err) {
      console.error('Failed to load saved folder:', err)
    }
    return false
  }

  /**
   * Disconnect/disable local playback
   */
  async function disconnect() {
    directoryHandle.value = null
    directoryName.value = ''
    hasPermission.value = false
    enabled.value = false
    error.value = null
    await clearHandle()
  }

  /**
   * Find a file in the local folder by path
   * @param {string} filePath - Relative path like "studio-1/2026-01-17/recording.wav"
   * @returns {File|null} - File object or null if not found
   */
  async function getLocalFile(filePath) {
    if (!isAvailable.value) {
      return null
    }

    try {
      // Split path into parts
      const parts = filePath.split('/').filter(p => p)

      // Navigate to the file
      let currentHandle = directoryHandle.value

      // Navigate through directories
      for (let i = 0; i < parts.length - 1; i++) {
        currentHandle = await currentHandle.getDirectoryHandle(parts[i])
      }

      // Get the file
      const fileName = parts[parts.length - 1]
      const fileHandle = await currentHandle.getFileHandle(fileName)
      const file = await fileHandle.getFile()

      return file
    } catch (err) {
      // File not found locally - this is expected, not an error
      if (err.name === 'NotFoundError') {
        return null
      }
      console.warn('Error accessing local file:', err)
      return null
    }
  }

  /**
   * Check if a file exists locally
   * @param {string} filePath - Relative path
   * @returns {boolean}
   */
  async function hasLocalFile(filePath) {
    const file = await getLocalFile(filePath)
    return file !== null
  }

  /**
   * Get a blob URL for local file playback
   * @param {string} filePath - Relative path
   * @returns {string|null} - Blob URL or null
   */
  async function getLocalFileUrl(filePath) {
    const file = await getLocalFile(filePath)
    if (file) {
      return URL.createObjectURL(file)
    }
    return null
  }

  return {
    // State
    enabled,
    directoryHandle,
    directoryName,
    hasPermission,
    error,

    // Computed
    isSupported,
    isAvailable,

    // Actions
    selectFolder,
    requestPermission,
    checkPermission,
    loadSavedFolder,
    disconnect,
    getLocalFile,
    hasLocalFile,
    getLocalFileUrl
  }
})
