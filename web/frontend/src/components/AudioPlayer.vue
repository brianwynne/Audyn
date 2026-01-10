<template>
  <div class="audio-player">
    <div v-if="!selectedFile" class="text-center text-grey pa-4">
      <v-icon icon="mdi-music-note" size="48" class="mb-2" />
      <p>Select a file from the Files page to preview</p>
    </div>

    <div v-else>
      <div class="d-flex align-center mb-4">
        <v-btn
          icon
          :color="isPlaying ? 'primary' : 'default'"
          @click="togglePlay"
        >
          <v-icon>{{ isPlaying ? 'mdi-pause' : 'mdi-play' }}</v-icon>
        </v-btn>

        <v-btn icon class="ml-2" @click="stop">
          <v-icon>mdi-stop</v-icon>
        </v-btn>

        <div class="flex-grow-1 mx-4">
          <v-slider
            v-model="progress"
            :max="duration"
            hide-details
            @update:model-value="seek"
          />
          <div class="d-flex justify-space-between text-caption">
            <span>{{ formatTime(currentTime) }}</span>
            <span>{{ formatTime(duration) }}</span>
          </div>
        </div>

        <v-btn icon @click="showVolumeSlider = !showVolumeSlider">
          <v-icon>{{ volumeIcon }}</v-icon>
        </v-btn>

        <v-slider
          v-if="showVolumeSlider"
          v-model="volume"
          :max="100"
          hide-details
          style="max-width: 100px"
          class="ml-2"
        />
      </div>

      <div class="text-caption text-grey">
        {{ selectedFile.name }}
      </div>

      <audio
        ref="audioEl"
        :src="audioSrc"
        @timeupdate="onTimeUpdate"
        @loadedmetadata="onLoaded"
        @ended="onEnded"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, watch } from 'vue'

// Props
const props = defineProps({
  file: {
    type: Object,
    default: null
  }
})

// Refs
const audioEl = ref(null)
const selectedFile = ref(props.file)
const isPlaying = ref(false)
const currentTime = ref(0)
const duration = ref(0)
const progress = ref(0)
const volume = ref(75)
const showVolumeSlider = ref(false)

// Computed
const audioSrc = computed(() => {
  if (!selectedFile.value) return ''
  return `/api/stream/preview/${selectedFile.value.path}`
})

const volumeIcon = computed(() => {
  if (volume.value === 0) return 'mdi-volume-off'
  if (volume.value < 50) return 'mdi-volume-low'
  return 'mdi-volume-high'
})

// Methods
function togglePlay() {
  if (!audioEl.value) return

  if (isPlaying.value) {
    audioEl.value.pause()
  } else {
    audioEl.value.play()
  }
  isPlaying.value = !isPlaying.value
}

function stop() {
  if (!audioEl.value) return
  audioEl.value.pause()
  audioEl.value.currentTime = 0
  isPlaying.value = false
  currentTime.value = 0
  progress.value = 0
}

function seek(value) {
  if (!audioEl.value) return
  audioEl.value.currentTime = value
}

function onTimeUpdate() {
  if (!audioEl.value) return
  currentTime.value = audioEl.value.currentTime
  progress.value = audioEl.value.currentTime
}

function onLoaded() {
  if (!audioEl.value) return
  duration.value = audioEl.value.duration
}

function onEnded() {
  isPlaying.value = false
  currentTime.value = 0
  progress.value = 0
}

function formatTime(seconds) {
  if (!seconds || isNaN(seconds)) return '00:00'
  const m = Math.floor(seconds / 60)
  const s = Math.floor(seconds % 60)
  return `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`
}

// Watchers
watch(volume, (val) => {
  if (audioEl.value) {
    audioEl.value.volume = val / 100
  }
})

watch(() => props.file, (newFile) => {
  selectedFile.value = newFile
  stop()
})

// Expose for parent components
defineExpose({
  setFile(file) {
    selectedFile.value = file
    stop()
  }
})
</script>

<style scoped>
.audio-player {
  padding: 8px;
}
</style>
