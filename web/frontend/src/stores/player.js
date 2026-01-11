/**
 * Audio Player Store
 *
 * Manages audio playback state across the application.
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const usePlayerStore = defineStore('player', () => {
  // State
  const currentFile = ref(null)
  const isPlaying = ref(false)
  const currentTime = ref(0)
  const duration = ref(0)
  const volume = ref(100)

  // Getters
  const hasFile = computed(() => !!currentFile.value)

  const progress = computed(() => {
    if (!duration.value) return 0
    return (currentTime.value / duration.value) * 100
  })

  // Actions
  function loadFile(file) {
    currentFile.value = file
    currentTime.value = 0
    duration.value = 0
    isPlaying.value = false
  }

  function clearFile() {
    currentFile.value = null
    currentTime.value = 0
    duration.value = 0
    isPlaying.value = false
  }

  function play() {
    if (currentFile.value) {
      isPlaying.value = true
    }
  }

  function pause() {
    isPlaying.value = false
  }

  function setPlaying(playing) {
    isPlaying.value = playing
  }

  function setCurrentTime(time) {
    currentTime.value = time
  }

  function setDuration(dur) {
    duration.value = dur
  }

  function setVolume(vol) {
    volume.value = Math.max(0, Math.min(100, vol))
  }

  return {
    // State
    currentFile,
    isPlaying,
    currentTime,
    duration,
    volume,

    // Getters
    hasFile,
    progress,

    // Actions
    loadFile,
    clearFile,
    play,
    pause,
    setPlaying,
    setCurrentTime,
    setDuration,
    setVolume
  }
})
