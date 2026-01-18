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

  /**
   * Get a FileHandle for a local file (for streaming access)
   * @param {string} filePath - Relative path
   * @returns {FileSystemFileHandle|null}
   */
  async function getLocalFileHandle(filePath) {
    if (!isAvailable.value) {
      return null
    }

    try {
      const parts = filePath.split('/').filter(p => p)
      let currentHandle = directoryHandle.value

      for (let i = 0; i < parts.length - 1; i++) {
        currentHandle = await currentHandle.getDirectoryHandle(parts[i])
      }

      const fileName = parts[parts.length - 1]
      return await currentHandle.getFileHandle(fileName)
    } catch (err) {
      if (err.name === 'NotFoundError') {
        return null
      }
      console.warn('Error getting file handle:', err)
      return null
    }
  }

  /**
   * Create a MediaSource stream for growing Opus files
   * This enables local playback of files still being recorded
   * @param {HTMLAudioElement} audioElement - The audio element to attach to
   * @param {string} filePath - Relative path to the file
   * @returns {Object} - { cleanup: Function, mediaSource: MediaSource } or null
   */
  async function createOpusStream(audioElement, filePath) {
    const fileHandle = await getLocalFileHandle(filePath)
    if (!fileHandle) {
      return null
    }

    // Check MediaSource support for Ogg Opus
    if (!MediaSource.isTypeSupported('audio/ogg; codecs=opus')) {
      console.warn('MediaSource does not support audio/ogg; codecs=opus')
      return null
    }

    const mediaSource = new MediaSource()
    let sourceBuffer = null
    let pollInterval = null
    let lastReadPosition = 0
    let isEnded = false

    const cleanup = () => {
      if (pollInterval) {
        clearInterval(pollInterval)
        pollInterval = null
      }
      if (mediaSource.readyState === 'open') {
        try {
          mediaSource.endOfStream()
        } catch (e) {
          // Ignore errors when ending stream
        }
      }
    }

    mediaSource.addEventListener('sourceopen', async () => {
      try {
        sourceBuffer = mediaSource.addSourceBuffer('audio/ogg; codecs=opus')

        // Initial read
        const file = await fileHandle.getFile()
        const initialData = await file.arrayBuffer()
        if (initialData.byteLength > 0) {
          sourceBuffer.appendBuffer(initialData)
          lastReadPosition = initialData.byteLength
        }

        // Poll for new data every 500ms
        pollInterval = setInterval(async () => {
          if (isEnded || !sourceBuffer || sourceBuffer.updating) {
            return
          }

          try {
            const file = await fileHandle.getFile()
            const currentSize = file.size

            if (currentSize > lastReadPosition) {
              // Read only new data
              const newData = await file.slice(lastReadPosition).arrayBuffer()
              if (newData.byteLength > 0) {
                sourceBuffer.appendBuffer(newData)
                lastReadPosition = currentSize
              }
            }
          } catch (err) {
            console.warn('Error polling for new data:', err)
          }
        }, 500)
      } catch (err) {
        console.error('Error setting up MediaSource:', err)
        cleanup()
      }
    })

    // Create object URL and attach to audio element
    const streamUrl = URL.createObjectURL(mediaSource)
    audioElement.src = streamUrl

    return {
      cleanup: () => {
        cleanup()
        URL.revokeObjectURL(streamUrl)
      },
      mediaSource,
      isOpusStream: true
    }
  }

  /**
   * Check if a file is Ogg Opus format based on extension
   * @param {string} filePath
   * @returns {boolean}
   */
  function isOpusFormat(filePath) {
    const ext = filePath.split('.').pop()?.toLowerCase()
    return ext === 'opus' || ext === 'ogg'
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
    getLocalFileUrl,
    getLocalFileHandle,
    createOpusStream,
    isOpusFormat
  }
})
