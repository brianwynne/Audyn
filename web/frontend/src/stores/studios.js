/**
 * Studios Store
 *
 * Manages studio configuration and recorder assignments.
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useStudiosStore = defineStore('studios', () => {
  // State
  const studios = ref([])
  const loading = ref(false)
  const error = ref(null)
  const studioFiles = ref({})  // Files organized by studio ID

  // Getters
  const getStudioById = (id) =>
    studios.value.find(s => s.id === id)

  const studiosWithRecorders = computed(() =>
    studios.value.filter(s => s.recorder_id)
  )

  const studiosWithoutRecorders = computed(() =>
    studios.value.filter(s => !s.recorder_id)
  )

  // Actions
  async function fetchStudios() {
    loading.value = true
    error.value = null

    try {
      const response = await fetch('/api/studios/')
      if (response.ok) {
        studios.value = await response.json()
      } else {
        throw new Error('Failed to fetch studios')
      }
    } catch (err) {
      console.error('Failed to fetch studios:', err)
      error.value = err.message
    } finally {
      loading.value = false
    }
  }

  async function createStudio(studio) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch('/api/studios/', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(studio)
      })

      if (response.ok) {
        const newStudio = await response.json()
        studios.value.push(newStudio)
        return newStudio
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to create studio')
      }
    } catch (err) {
      console.error('Failed to create studio:', err)
      error.value = err.message
      return null
    } finally {
      loading.value = false
    }
  }

  async function updateStudio(studioId, update) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/api/studios/${studioId}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(update)
      })

      if (response.ok) {
        const updatedStudio = await response.json()
        const index = studios.value.findIndex(s => s.id === studioId)
        if (index !== -1) {
          studios.value[index] = updatedStudio
        }
        return updatedStudio
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to update studio')
      }
    } catch (err) {
      console.error('Failed to update studio:', err)
      error.value = err.message
      return null
    } finally {
      loading.value = false
    }
  }

  async function deleteStudio(studioId) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/api/studios/${studioId}`, {
        method: 'DELETE'
      })

      if (response.ok) {
        studios.value = studios.value.filter(s => s.id !== studioId)
        return true
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to delete studio')
      }
    } catch (err) {
      console.error('Failed to delete studio:', err)
      error.value = err.message
      return false
    } finally {
      loading.value = false
    }
  }

  async function assignRecorder(studioId, recorderId) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/api/studios/${studioId}/assign`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ recorder_id: recorderId })
      })

      if (response.ok) {
        // Refresh studios to get updated assignments
        await fetchStudios()
        return true
      } else {
        const data = await response.json()
        throw new Error(data.detail || 'Failed to assign recorder')
      }
    } catch (err) {
      console.error('Failed to assign recorder:', err)
      error.value = err.message
      return false
    } finally {
      loading.value = false
    }
  }

  async function unassignRecorder(studioId) {
    return assignRecorder(studioId, null)
  }

  async function getStudioRecordings(studioId) {
    try {
      const response = await fetch(`/api/studios/${studioId}/recordings`)
      if (response.ok) {
        return await response.json()
      }
      return { recordings: [] }
    } catch (err) {
      console.error('Failed to fetch studio recordings:', err)
      return { recordings: [] }
    }
  }

  async function fetchStudioFiles(studioId) {
    try {
      const response = await fetch(`/api/assets/browse?studio_id=${studioId}`)
      if (response.ok) {
        const data = await response.json()
        studioFiles.value[studioId] = data.files || []
        return data.files || []
      }
      return []
    } catch (err) {
      console.error('Failed to fetch studio files:', err)
      return []
    }
  }

  async function fetchAllStudioFiles() {
    for (const studio of studios.value) {
      await fetchStudioFiles(studio.id)
    }
  }

  function getFilesForStudio(studioId) {
    return studioFiles.value[studioId] || []
  }

  return {
    // State
    studios,
    loading,
    error,
    studioFiles,

    // Getters
    getStudioById,
    studiosWithRecorders,
    studiosWithoutRecorders,

    // Actions
    fetchStudios,
    createStudio,
    updateStudio,
    deleteStudio,
    assignRecorder,
    unassignRecorder,
    getStudioRecordings,
    fetchStudioFiles,
    fetchAllStudioFiles,
    getFilesForStudio
  }
})
