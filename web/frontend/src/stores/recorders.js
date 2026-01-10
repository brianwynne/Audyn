/**
 * Recorders Store
 *
 * Manages multiple recorder instances (1-6).
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useRecordersStore = defineStore('recorders', () => {
  // State
  const recorders = ref([])
  const activeCount = ref(6)
  const maxRecorders = ref(6)
  const loading = ref(false)
  const error = ref(null)

  // WebSocket for real-time levels
  let levelsWebSocket = null
  const connected = ref(false)

  // Getters
  const activeRecorders = computed(() =>
    recorders.value.filter(r => r.id <= activeCount.value)
  )

  const recordingRecorders = computed(() =>
    recorders.value.filter(r => r.state === 'recording')
  )

  const getRecorderById = (id) =>
    recorders.value.find(r => r.id === id)

  const getRecorderByStudio = (studioId) =>
    recorders.value.find(r => r.studio_id === studioId)

  // Actions
  async function fetchRecorders() {
    loading.value = true
    error.value = null

    try {
      const response = await fetch('/api/recorders/')
      if (response.ok) {
        recorders.value = await response.json()
      } else {
        throw new Error('Failed to fetch recorders')
      }
    } catch (err) {
      console.error('Failed to fetch recorders:', err)
      error.value = err.message
    } finally {
      loading.value = false
    }
  }

  async function fetchActiveCount() {
    try {
      const response = await fetch('/api/recorders/active-count')
      if (response.ok) {
        const data = await response.json()
        activeCount.value = data.active_recorders
        maxRecorders.value = data.max_recorders
      }
    } catch (err) {
      console.error('Failed to fetch active count:', err)
    }
  }

  async function setActiveCount(count) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/api/recorders/active-count/${count}`, {
        method: 'PUT'
      })

      if (response.ok) {
        const data = await response.json()
        activeCount.value = data.active_recorders
        await fetchRecorders()
        return true
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to set active count')
      }
    } catch (err) {
      console.error('Failed to set active count:', err)
      error.value = err.message
      return false
    } finally {
      loading.value = false
    }
  }

  async function startRecorder(recorderId) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/api/recorders/${recorderId}/start`, {
        method: 'POST'
      })

      if (response.ok) {
        await fetchRecorders()
        return true
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to start recorder')
      }
    } catch (err) {
      console.error('Failed to start recorder:', err)
      error.value = err.message
      return false
    } finally {
      loading.value = false
    }
  }

  async function stopRecorder(recorderId) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/api/recorders/${recorderId}/stop`, {
        method: 'POST'
      })

      if (response.ok) {
        await fetchRecorders()
        return true
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to stop recorder')
      }
    } catch (err) {
      console.error('Failed to stop recorder:', err)
      error.value = err.message
      return false
    } finally {
      loading.value = false
    }
  }

  async function startAllRecorders() {
    loading.value = true
    error.value = null

    try {
      const response = await fetch('/api/recorders/start-all', {
        method: 'POST'
      })

      if (response.ok) {
        await fetchRecorders()
        return true
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to start all recorders')
      }
    } catch (err) {
      console.error('Failed to start all recorders:', err)
      error.value = err.message
      return false
    } finally {
      loading.value = false
    }
  }

  async function stopAllRecorders() {
    loading.value = true
    error.value = null

    try {
      const response = await fetch('/api/recorders/stop-all', {
        method: 'POST'
      })

      if (response.ok) {
        await fetchRecorders()
        return true
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to stop all recorders')
      }
    } catch (err) {
      console.error('Failed to stop all recorders:', err)
      error.value = err.message
      return false
    } finally {
      loading.value = false
    }
  }

  async function updateRecorderConfig(recorderId, config) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/api/recorders/${recorderId}/config`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
      })

      if (response.ok) {
        await fetchRecorders()
        return true
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to update config')
      }
    } catch (err) {
      console.error('Failed to update recorder config:', err)
      error.value = err.message
      return false
    } finally {
      loading.value = false
    }
  }

  // WebSocket connection for real-time levels
  function connectLevels() {
    if (levelsWebSocket) {
      levelsWebSocket.close()
    }

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
    const wsUrl = `${protocol}//${window.location.host}/ws/levels`

    levelsWebSocket = new WebSocket(wsUrl)

    levelsWebSocket.onopen = () => {
      connected.value = true
      console.log('Levels WebSocket connected')
    }

    levelsWebSocket.onmessage = (event) => {
      const data = JSON.parse(event.data)
      if (data.type === 'all_levels') {
        // Update levels for each recorder
        for (const recorderData of data.recorders) {
          const recorder = recorders.value.find(r => r.id === recorderData.recorder_id)
          if (recorder) {
            recorder.levels = recorderData.channels
            recorder.state = recorderData.state
          }
        }
      }
    }

    levelsWebSocket.onclose = () => {
      connected.value = false
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
    connected.value = false
  }

  return {
    // State
    recorders,
    activeCount,
    maxRecorders,
    loading,
    error,
    connected,

    // Getters
    activeRecorders,
    recordingRecorders,
    getRecorderById,
    getRecorderByStudio,

    // Actions
    fetchRecorders,
    fetchActiveCount,
    setActiveCount,
    startRecorder,
    stopRecorder,
    startAllRecorders,
    stopAllRecorders,
    updateRecorderConfig,
    connectLevels,
    disconnectLevels
  }
})
