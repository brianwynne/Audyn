/**
 * Capture State Store
 *
 * Manages audio capture state and configuration.
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useCaptureStore = defineStore('capture', () => {
  // State
  const status = ref({
    state: 'stopped',
    config: null,
    currentFile: null,
    recordingDuration: 0,
    bytesWritten: 0,
    errors: []
  })

  const config = ref({
    sourceType: 'aes67',
    multicastAddr: '239.69.1.1',
    port: 5004,
    sampleRate: 48000,
    channels: 2,
    format: 'wav',
    bitrate: 128000,
    archiveRoot: '/var/lib/audyn',
    archiveLayout: 'dailydir',
    archivePeriod: 3600,
    archiveClock: 'localtime',
    ptpInterface: null
  })

  const sources = ref([])
  const activeSourceId = ref(null)

  const loading = ref(false)
  const error = ref(null)

  // Audio levels from WebSocket
  const levels = ref([
    { name: 'L', level_db: -60, level_linear: 0, peak_db: -60, clipping: false },
    { name: 'R', level_db: -60, level_linear: 0, peak_db: -60, clipping: false }
  ])

  let levelsWebSocket = null

  // Getters
  const isRecording = computed(() => status.value.state === 'recording')
  const isStopped = computed(() => status.value.state === 'stopped')
  const activeSource = computed(() =>
    sources.value.find(s => s.id === activeSourceId.value)
  )

  // Actions
  async function fetchStatus() {
    try {
      const response = await fetch('/api/control/status')
      if (response.ok) {
        const data = await response.json()
        status.value = {
          state: data.state,
          config: data.config,
          currentFile: data.current_file,
          recordingDuration: data.recording_duration,
          bytesWritten: data.bytes_written,
          errors: data.errors
        }
      }
    } catch (err) {
      console.error('Failed to fetch status:', err)
      error.value = 'Failed to fetch capture status'
    }
  }

  async function fetchConfig() {
    try {
      const response = await fetch('/api/control/config')
      if (response.ok) {
        const data = await response.json()
        if (data) {
          config.value = {
            sourceType: data.source_type || 'aes67',
            multicastAddr: data.multicast_addr || '239.69.1.1',
            port: data.port || 5004,
            sampleRate: data.sample_rate || 48000,
            channels: data.channels || 2,
            format: data.format || 'wav',
            bitrate: data.bitrate || 128000,
            archiveRoot: data.archive_root || '/var/lib/audyn',
            archiveLayout: data.archive_layout || 'dailydir',
            archivePeriod: data.archive_period || 3600,
            archiveClock: data.archive_clock || 'localtime',
            ptpInterface: data.ptp_interface || null
          }
        }
      }
    } catch (err) {
      console.error('Failed to fetch config:', err)
    }
  }

  async function fetchSources() {
    try {
      const response = await fetch('/api/sources/')
      if (response.ok) {
        sources.value = await response.json()
      }

      const activeResponse = await fetch('/api/sources/active')
      if (activeResponse.ok) {
        const data = await activeResponse.json()
        activeSourceId.value = data.active_source_id
      }
    } catch (err) {
      console.error('Failed to fetch sources:', err)
      error.value = 'Failed to fetch sources'
    }
  }

  async function startCapture() {
    loading.value = true
    error.value = null

    try {
      // Set config first
      await fetch('/api/control/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          source_type: config.value.sourceType,
          multicast_addr: config.value.multicastAddr,
          port: config.value.port,
          sample_rate: config.value.sampleRate,
          channels: config.value.channels,
          format: config.value.format,
          bitrate: config.value.bitrate,
          archive_root: config.value.archiveRoot,
          archive_layout: config.value.archiveLayout,
          archive_period: config.value.archivePeriod,
          archive_clock: config.value.archiveClock,
          ptp_interface: config.value.ptpInterface
        })
      })

      // Start capture
      const response = await fetch('/api/control/start', { method: 'POST' })

      if (response.ok) {
        await fetchStatus()
        return true
      } else {
        const data = await response.json()
        error.value = data.detail || 'Failed to start capture'
        return false
      }
    } catch (err) {
      console.error('Failed to start capture:', err)
      error.value = 'Failed to start capture'
      return false
    } finally {
      loading.value = false
    }
  }

  async function stopCapture() {
    loading.value = true
    error.value = null

    try {
      const response = await fetch('/api/control/stop', { method: 'POST' })

      if (response.ok) {
        await fetchStatus()
        return true
      } else {
        const data = await response.json()
        error.value = data.detail || 'Failed to stop capture'
        return false
      }
    } catch (err) {
      console.error('Failed to stop capture:', err)
      error.value = 'Failed to stop capture'
      return false
    } finally {
      loading.value = false
    }
  }

  async function switchSource(sourceId) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/api/sources/active/${sourceId}`, {
        method: 'POST'
      })

      if (response.ok) {
        activeSourceId.value = sourceId
        return true
      } else {
        const data = await response.json()
        error.value = data.detail || 'Failed to switch source'
        return false
      }
    } catch (err) {
      console.error('Failed to switch source:', err)
      error.value = 'Failed to switch source'
      return false
    } finally {
      loading.value = false
    }
  }

  function connectLevels() {
    if (levelsWebSocket) {
      levelsWebSocket.close()
    }

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
    const wsUrl = `${protocol}//${window.location.host}/ws/levels`

    levelsWebSocket = new WebSocket(wsUrl)

    levelsWebSocket.onmessage = (event) => {
      const data = JSON.parse(event.data)
      if (data.type === 'levels') {
        levels.value = data.channels
      }
    }

    levelsWebSocket.onclose = () => {
      // Reconnect after delay
      setTimeout(() => {
        if (levelsWebSocket?.readyState === WebSocket.CLOSED) {
          connectLevels()
        }
      }, 2000)
    }

    levelsWebSocket.onerror = (err) => {
      console.error('WebSocket error:', err)
    }
  }

  function disconnectLevels() {
    if (levelsWebSocket) {
      levelsWebSocket.close()
      levelsWebSocket = null
    }
  }

  return {
    // State
    status,
    config,
    sources,
    activeSourceId,
    levels,
    loading,
    error,

    // Getters
    isRecording,
    isStopped,
    activeSource,

    // Actions
    fetchStatus,
    fetchConfig,
    fetchSources,
    startCapture,
    stopCapture,
    switchSource,
    connectLevels,
    disconnectLevels
  }
})
