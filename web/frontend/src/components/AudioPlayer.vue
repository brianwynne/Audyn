<template>
  <v-card class="audio-player" elevation="8">
    <v-card-text class="pa-3">
      <div class="d-flex align-center">
        <!-- File Info -->
        <div class="file-info mr-4" style="min-width: 200px; max-width: 300px">
          <div class="text-subtitle-2 text-truncate">
            <v-icon
              :icon="getFileIcon(playerStore.currentFile?.format)"
              :color="getFileColor(playerStore.currentFile?.format)"
              size="small"
              class="mr-1"
            />
            {{ playerStore.currentFile?.name || 'No file loaded' }}
            <v-chip
              v-if="isLocalPlayback"
              size="x-small"
              color="success"
              variant="flat"
              class="ml-1"
              title="Playing from local folder (buffer-free)"
            >
              <v-icon size="x-small" start>mdi-folder</v-icon>
              LOCAL
            </v-chip>
          </div>
          <div class="text-caption text-medium-emphasis">
            {{ formatTime(currentTime) }} / {{ formatTime(effectiveDuration) }}
          </div>
        </div>

        <!-- Controls -->
        <div class="controls d-flex align-center mr-4">
          <!-- Recue Button -->
          <v-btn
            icon
            variant="text"
            size="small"
            title="Recue (Restart from beginning) [R]"
            @click="recue"
          >
            <v-icon>mdi-skip-backward</v-icon>
          </v-btn>

          <!-- Play/Pause Button -->
          <v-btn
            icon
            variant="flat"
            color="primary"
            size="large"
            class="mx-2"
            title="Play/Pause [Space]"
            @click="togglePlay"
          >
            <v-icon size="28">
              {{ isPlaying ? 'mdi-pause' : 'mdi-play' }}
            </v-icon>
          </v-btn>

          <!-- Stop Button -->
          <v-btn
            icon
            variant="text"
            size="small"
            title="Stop"
            @click="stop"
          >
            <v-icon>mdi-stop</v-icon>
          </v-btn>
        </div>

        <!-- Progress Bar -->
        <div class="progress-container flex-grow-1 mx-4">
          <v-slider
            v-model="currentTime"
            :max="effectiveDuration || 100"
            :disabled="!playerStore.currentFile || isGrowing"
            hide-details
            color="primary"
            track-color="grey-lighten-2"
            @start="onSeekStart"
            @end="onSeekEnd"
          />
          <div v-if="isGrowing" class="text-caption text-error d-flex align-center">
            <v-icon size="x-small" class="mr-1" color="error">mdi-record-circle</v-icon>
            LIVE - Recording in progress
          </div>
        </div>

        <!-- Volume Control -->
        <div class="volume-control d-flex align-center" style="width: 150px">
          <v-btn
            icon
            variant="text"
            size="small"
            @click="toggleMute"
          >
            <v-icon>{{ volumeIcon }}</v-icon>
          </v-btn>
          <v-slider
            v-model="volume"
            :min="0"
            :max="100"
            hide-details
            density="compact"
            class="ml-2"
            @update:model-value="setVolume"
          />
        </div>

        <!-- Close Button -->
        <v-btn
          icon
          variant="text"
          size="small"
          class="ml-2"
          @click="close"
        >
          <v-icon>mdi-close</v-icon>
        </v-btn>
      </div>
    </v-card-text>

    <!-- Hidden Audio Element -->
    <audio
      ref="audioElement"
      :src="audioSrc"
      @loadedmetadata="onLoadedMetadata"
      @timeupdate="onTimeUpdate"
      @ended="onEnded"
      @play="onPlay"
      @pause="onPause"
    />
  </v-card>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { usePlayerStore } from '@/stores/player'
import { useLocalPlaybackStore } from '@/stores/localPlayback'

const playerStore = usePlayerStore()
const localPlaybackStore = useLocalPlaybackStore()

const audioElement = ref(null)
const currentTime = ref(0)
const duration = ref(0)
const fileDuration = ref(0)  // Duration from file metadata (reliable)
const volume = ref(100)
const isMuted = ref(false)
const isPlaying = ref(false)
const isGrowing = ref(false)  // Whether file is still being recorded
const seekPosition = ref(0)  // For seeking in completed files
const isSeeking = ref(false)
const localBlobUrl = ref(null)  // Blob URL for local file playback
const isLocalPlayback = ref(false)  // Whether currently using local file

// Computed
const audioSrc = computed(() => {
  if (!playerStore.currentFile?.path) return ''

  // Use local file if available (buffer-free playback)
  if (localBlobUrl.value) {
    return localBlobUrl.value
  }

  // Fall back to streaming from server
  const base = `/api/stream/preview/${playerStore.currentFile.path}`
  if (!isGrowing.value && seekPosition.value > 0) {
    return `${base}?start=${seekPosition.value}&follow=false`
  }
  // For growing files or initial playback
  return `${base}?follow=${isGrowing.value}`
})

// Use file metadata duration if audio element returns Infinity
const effectiveDuration = computed(() => {
  if (duration.value && isFinite(duration.value) && duration.value > 0) {
    return duration.value
  }
  return fileDuration.value || 0
})

const volumeIcon = computed(() => {
  if (isMuted.value || volume.value === 0) return 'mdi-volume-off'
  if (volume.value < 30) return 'mdi-volume-low'
  if (volume.value < 70) return 'mdi-volume-medium'
  return 'mdi-volume-high'
})

// Methods
function formatTime(seconds) {
  if (!seconds || isNaN(seconds)) return '0:00'
  const mins = Math.floor(seconds / 60)
  const secs = Math.floor(seconds % 60)
  return `${mins}:${secs.toString().padStart(2, '0')}`
}

function getFileIcon(format) {
  switch (format) {
    case 'wav': return 'mdi-file-music'
    case 'opus':
    case 'ogg': return 'mdi-file-music-outline'
    case 'mp3': return 'mdi-file-music'
    default: return 'mdi-file'
  }
}

function getFileColor(format) {
  switch (format) {
    case 'wav': return 'blue'
    case 'opus':
    case 'ogg': return 'purple'
    case 'mp3': return 'green'
    default: return 'grey'
  }
}

function togglePlay() {
  if (!audioElement.value) return

  if (isPlaying.value) {
    audioElement.value.pause()
  } else {
    audioElement.value.play()
  }
}

function stop() {
  if (!audioElement.value) return
  audioElement.value.pause()
  audioElement.value.currentTime = 0
  currentTime.value = 0
}

function recue() {
  if (!audioElement.value) return
  // Seek to beginning and play
  if (isGrowing.value) {
    // For growing files, just seek the audio element
    audioElement.value.currentTime = 0
    currentTime.value = 0
    audioElement.value.play()
  } else {
    // For completed files, reload from start
    seekPosition.value = 0
    currentTime.value = 0
    audioElement.value.load()
    audioElement.value.play()
  }
}

function onSeekStart() {
  isSeeking.value = true
}

function onSeekEnd() {
  if (!audioElement.value || isGrowing.value) {
    isSeeking.value = false
    return
  }

  // For completed files, reload with new start position
  const newTime = currentTime.value
  seekPosition.value = newTime
  isSeeking.value = false

  // Reload the audio with the new start position
  audioElement.value.load()
  audioElement.value.play()
}

async function fetchFileInfo(filePath) {
  try {
    const response = await fetch(`/api/stream/info/${filePath}`)
    if (response.ok) {
      const info = await response.json()
      fileDuration.value = info.duration || 0
      isGrowing.value = info.growing || false
    }
  } catch (err) {
    console.error('Failed to fetch file info:', err)
  }
}

function setVolume(vol) {
  if (!audioElement.value) return
  audioElement.value.volume = vol / 100
  isMuted.value = vol === 0
}

function toggleMute() {
  if (!audioElement.value) return
  isMuted.value = !isMuted.value
  audioElement.value.muted = isMuted.value
}

function close() {
  stop()
  cleanupBlobUrl()
  playerStore.clearFile()
}

// Event handlers
function onLoadedMetadata() {
  if (audioElement.value) {
    duration.value = audioElement.value.duration
  }
}

function onTimeUpdate() {
  if (audioElement.value && !isNaN(audioElement.value.currentTime) && !isSeeking.value) {
    // Add seekPosition offset for completed files that were seeked
    currentTime.value = seekPosition.value + audioElement.value.currentTime
  }
}

function onEnded() {
  isPlaying.value = false
  playerStore.setPlaying(false)
}

function onPlay() {
  isPlaying.value = true
  playerStore.setPlaying(true)
}

function onPause() {
  isPlaying.value = false
  playerStore.setPlaying(false)
}

// Keyboard shortcuts
function handleKeydown(event) {
  // Only handle if no input is focused
  if (event.target.tagName === 'INPUT' || event.target.tagName === 'TEXTAREA') return
  if (!playerStore.currentFile) return

  switch (event.code) {
    case 'Space':
      event.preventDefault()
      togglePlay()
      break
    case 'KeyR':
      event.preventDefault()
      recue()
      break
  }
}

// Cleanup blob URL when no longer needed
function cleanupBlobUrl() {
  if (localBlobUrl.value) {
    URL.revokeObjectURL(localBlobUrl.value)
    localBlobUrl.value = null
  }
  isLocalPlayback.value = false
}

// Watch for file changes
watch(() => playerStore.currentFile, async (newFile) => {
  // Cleanup previous blob URL
  cleanupBlobUrl()

  if (newFile && audioElement.value) {
    // Reset state
    currentTime.value = 0
    duration.value = 0
    fileDuration.value = 0
    seekPosition.value = 0
    isGrowing.value = false

    // Fetch file info to get duration and growing status
    await fetchFileInfo(newFile.path)

    // Try to use local file first (if available and file is not growing)
    if (!isGrowing.value && localPlaybackStore.isAvailable) {
      const url = await localPlaybackStore.getLocalFileUrl(newFile.path)
      if (url) {
        localBlobUrl.value = url
        isLocalPlayback.value = true
        console.log('Using local playback for:', newFile.path)
      }
    }

    // Auto-play when new file is loaded
    audioElement.value.load()
  }
})

// Watch for external play/pause requests
watch(() => playerStore.isPlaying, (shouldPlay) => {
  if (!audioElement.value) return

  if (shouldPlay && audioElement.value.paused) {
    audioElement.value.play()
  } else if (!shouldPlay && !audioElement.value.paused) {
    audioElement.value.pause()
  }
})

onMounted(() => {
  window.addEventListener('keydown', handleKeydown)

  // Set initial volume
  if (audioElement.value) {
    audioElement.value.volume = volume.value / 100
  }
})

onUnmounted(() => {
  window.removeEventListener('keydown', handleKeydown)
  cleanupBlobUrl()
})
</script>

<style scoped>
.audio-player {
  position: fixed;
  bottom: 0;
  left: 256px; /* Account for nav drawer */
  right: 0;
  z-index: 1000;
  border-radius: 0;
  border-top: 1px solid rgba(var(--v-border-color), 0.1);
}

/* When nav drawer is collapsed (rail mode) */
.v-navigation-drawer--rail ~ .v-main .audio-player {
  left: 56px;
}

@media (max-width: 960px) {
  .audio-player {
    left: 0;
  }
}

.file-info {
  overflow: hidden;
}

.progress-container {
  min-width: 200px;
}
</style>
