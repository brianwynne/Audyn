<template>
  <v-container fluid class="pa-6">
    <!-- Studio Header -->
    <div
      class="studio-header mb-6 pa-4 rounded"
      :style="{ backgroundColor: studio?.color + '20', borderLeft: `4px solid ${studio?.color}` }"
    >
      <div class="d-flex align-center justify-space-between">
        <div class="d-flex align-center">
          <v-avatar :color="studio?.color" size="56" class="mr-4">
            <v-icon icon="mdi-broadcast" size="28" />
          </v-avatar>
          <div>
            <h1 class="text-h4">{{ studio?.name || 'Studio' }}</h1>
            <p class="text-subtitle-1 text-medium-emphasis mb-0">
              {{ studio?.description || 'No description' }}
            </p>
          </div>
        </div>
        <v-btn
          variant="outlined"
          prepend-icon="mdi-swap-horizontal"
          @click="switchStudio"
        >
          Switch Studio
        </v-btn>
      </div>
    </div>

    <v-row>
      <!-- Recorders Panel -->
      <v-col cols="12" md="4">
        <v-card class="mb-4">
          <v-card-title class="d-flex align-center justify-space-between">
            <div class="d-flex align-center">
              <v-icon icon="mdi-record-circle" class="mr-2" />
              Recorders
            </div>
            <!-- Record/Stop All Buttons -->
            <div v-if="studioRecorders.length > 0">
              <v-btn
                v-if="allRecording"
                color="error"
                size="small"
                variant="tonal"
                :loading="recordersStore.loading"
                @click="stopAllStudioRecorders"
              >
                <v-icon start size="small">mdi-stop</v-icon>
                Stop All
              </v-btn>
              <v-btn
                v-else-if="anyRecording"
                color="warning"
                size="small"
                variant="tonal"
                :loading="recordersStore.loading"
                @click="stopAllStudioRecorders"
              >
                <v-icon start size="small">mdi-stop</v-icon>
                Stop All
              </v-btn>
              <v-btn
                v-if="!allRecording"
                color="success"
                size="small"
                variant="tonal"
                class="ml-2"
                :loading="recordersStore.loading"
                @click="recordAllStudioRecorders"
              >
                <v-icon start size="small">mdi-record</v-icon>
                Record All
              </v-btn>
            </div>
          </v-card-title>
          <v-card-text>
            <div v-if="studioRecorders.length === 0" class="text-center py-4 text-medium-emphasis">
              No recorders assigned to this studio
            </div>

            <v-list v-else density="compact">
              <v-list-item
                v-for="recorder in studioRecorders"
                :key="recorder.id"
                class="recorder-item mb-2 rounded"
                :class="{ 'recording': recorder.state === 'recording' }"
              >
                <template v-slot:prepend>
                  <v-avatar
                    :color="recorder.state === 'recording' ? 'error' : 'grey'"
                    size="40"
                  >
                    <v-icon
                      :icon="recorder.state === 'recording' ? 'mdi-record' : 'mdi-stop'"
                      size="20"
                    />
                  </v-avatar>
                </template>

                <v-list-item-title class="d-flex align-center justify-space-between">
                  <span>{{ recorder.name }}</span>
                  <!-- Record/Stop Button -->
                  <v-btn
                    v-if="recorder.state === 'recording'"
                    color="error"
                    size="small"
                    variant="flat"
                    :loading="recordersStore.loading"
                    @click.stop="stopRecorder(recorder.id)"
                  >
                    <v-icon start size="small">mdi-stop</v-icon>
                    Stop
                  </v-btn>
                  <v-btn
                    v-else
                    color="success"
                    size="small"
                    variant="flat"
                    :loading="recordersStore.loading"
                    @click.stop="startRecorder(recorder.id)"
                  >
                    <v-icon start size="small">mdi-record</v-icon>
                    Record
                  </v-btn>
                </v-list-item-title>
                <v-list-item-subtitle>
                  <v-chip
                    :color="recorder.state === 'recording' ? 'error' : 'grey'"
                    size="x-small"
                    class="mr-2"
                  >
                    {{ recorder.state }}
                  </v-chip>
                  <span class="text-caption">{{ recorder.config?.multicast_addr }}</span>
                </v-list-item-subtitle>

                <!-- Audio Levels -->
                <div class="mt-2">
                  <div
                    v-for="(channel, idx) in recorder.levels"
                    :key="idx"
                    class="d-flex align-center mb-1"
                  >
                    <span class="text-caption mr-2" style="width: 16px">{{ channel.name }}</span>
                    <v-progress-linear
                      :model-value="levelToPercent(channel.level_db)"
                      :color="getLevelColor(channel.level_db)"
                      height="8"
                      rounded
                      class="flex-grow-1"
                    />
                    <span class="text-caption ml-2" style="width: 48px">
                      {{ channel.level_db.toFixed(1) }} dB
                    </span>
                  </div>
                </div>
              </v-list-item>
            </v-list>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Files Panel -->
      <v-col cols="12" md="8">
        <v-card>
          <v-tabs v-model="activeTab" color="primary">
            <v-tab value="my-studio">
              <v-icon start>mdi-folder-star</v-icon>
              My Studio Files
            </v-tab>
            <v-tab value="other-studios">
              <v-icon start>mdi-folder-multiple</v-icon>
              Other Studios
            </v-tab>
          </v-tabs>

          <v-card-text>
            <v-tabs-window v-model="activeTab">
              <!-- My Studio Files -->
              <v-tabs-window-item value="my-studio">
                <div v-if="loadingFiles" class="text-center py-8">
                  <v-progress-circular indeterminate color="primary" />
                </div>

                <div v-else-if="myStudioFiles.length === 0" class="text-center py-8 text-medium-emphasis">
                  <v-icon icon="mdi-folder-open" size="48" class="mb-2" />
                  <p>No audio files found for this studio</p>
                </div>

                <v-list v-else density="compact" class="file-list">
                  <v-list-item
                    v-for="file in myStudioFiles"
                    :key="file.path"
                    class="file-item"
                    :class="{ 'active': playerStore.currentFile?.path === file.path }"
                    @click="playFile(file)"
                  >
                    <template v-slot:prepend>
                      <v-icon
                        :icon="getFileIcon(file.format)"
                        :color="getFileColor(file.format)"
                      />
                    </template>

                    <v-list-item-title>{{ file.name }}</v-list-item-title>
                    <v-list-item-subtitle>
                      {{ formatSize(file.size) }} - {{ formatDate(file.modified) }}
                    </v-list-item-subtitle>

                    <template v-slot:append>
                      <v-btn
                        icon
                        size="small"
                        variant="text"
                        :href="`/api/assets/download/${file.path}`"
                        target="_blank"
                        @click.stop
                      >
                        <v-icon>mdi-download</v-icon>
                      </v-btn>
                    </template>
                  </v-list-item>
                </v-list>
              </v-tabs-window-item>

              <!-- Other Studios Files -->
              <v-tabs-window-item value="other-studios">
                <div v-if="loadingFiles" class="text-center py-8">
                  <v-progress-circular indeterminate color="primary" />
                </div>

                <v-expansion-panels v-else variant="accordion">
                  <v-expansion-panel
                    v-for="otherStudio in otherStudios"
                    :key="otherStudio.id"
                  >
                    <v-expansion-panel-title>
                      <div class="d-flex align-center">
                        <v-avatar :color="otherStudio.color" size="24" class="mr-3">
                          <v-icon icon="mdi-broadcast" size="12" />
                        </v-avatar>
                        {{ otherStudio.name }}
                        <v-chip size="x-small" class="ml-2">
                          {{ getStudioFileCount(otherStudio.id) }} files
                        </v-chip>
                      </div>
                    </v-expansion-panel-title>
                    <v-expansion-panel-text>
                      <v-list
                        v-if="otherStudioFiles[otherStudio.id]?.length"
                        density="compact"
                      >
                        <v-list-item
                          v-for="file in otherStudioFiles[otherStudio.id]"
                          :key="file.path"
                          class="file-item"
                          :class="{ 'active': playerStore.currentFile?.path === file.path }"
                          @click="playFile(file)"
                        >
                          <template v-slot:prepend>
                            <v-icon
                              :icon="getFileIcon(file.format)"
                              :color="getFileColor(file.format)"
                            />
                          </template>
                          <v-list-item-title>{{ file.name }}</v-list-item-title>
                          <v-list-item-subtitle>
                            {{ formatSize(file.size) }}
                          </v-list-item-subtitle>
                        </v-list-item>
                      </v-list>
                      <div v-else class="text-center py-4 text-medium-emphasis">
                        No files available
                      </div>
                    </v-expansion-panel-text>
                  </v-expansion-panel>
                </v-expansion-panels>
              </v-tabs-window-item>
            </v-tabs-window>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Audio Player (Fixed at Bottom) -->
    <AudioPlayer v-if="playerStore.currentFile" />
  </v-container>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import { useStudiosStore } from '@/stores/studios'
import { useRecordersStore } from '@/stores/recorders'
import { usePlayerStore } from '@/stores/player'
import AudioPlayer from '@/components/AudioPlayer.vue'

const route = useRoute()
const router = useRouter()
const authStore = useAuthStore()
const studiosStore = useStudiosStore()
const recordersStore = useRecordersStore()
const playerStore = usePlayerStore()

const activeTab = ref('my-studio')
const loadingFiles = ref(true)
const myStudioFiles = ref([])
const otherStudioFiles = ref({})

// Get current studio
const studio = computed(() =>
  studiosStore.getStudioById(route.params.id)
)

// Get recorders for this studio
const studioRecorders = computed(() =>
  recordersStore.recorders.filter(r => r.studio_id === route.params.id)
)

// Check if all studio recorders are recording
const allRecording = computed(() =>
  studioRecorders.value.length > 0 &&
  studioRecorders.value.every(r => r.state === 'recording')
)

// Check if any studio recorders are recording
const anyRecording = computed(() =>
  studioRecorders.value.some(r => r.state === 'recording')
)

// Get other studios (excluding current)
const otherStudios = computed(() =>
  studiosStore.studios.filter(s => s.id !== route.params.id)
)

// Helper functions
function levelToPercent(db) {
  // Convert dB to percentage (0-100)
  // -60 dB = 0%, 0 dB = 100%
  return Math.max(0, Math.min(100, ((db + 60) / 60) * 100))
}

function getLevelColor(db) {
  if (db > -3) return 'red'
  if (db > -6) return 'orange'
  if (db > -12) return 'yellow'
  return 'green'
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

function formatSize(bytes) {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`
}

function formatDate(dateStr) {
  return new Date(dateStr).toLocaleString()
}

function getStudioFileCount(studioId) {
  return otherStudioFiles.value[studioId]?.length || 0
}

function playFile(file) {
  playerStore.loadFile(file)
  playerStore.play()
}

function switchStudio() {
  // Clear selection and go back to selector
  fetch('/api/studios/clear-selection', { method: 'POST' })
  authStore.clearSelectedStudio()
  router.push({ name: 'studio-select' })
}

// Recorder control methods
async function startRecorder(recorderId) {
  await recordersStore.startRecorder(recorderId)
}

async function stopRecorder(recorderId) {
  await recordersStore.stopRecorder(recorderId)
}

async function recordAllStudioRecorders() {
  // Start all stopped recorders for this studio
  for (const recorder of studioRecorders.value) {
    if (recorder.state !== 'recording') {
      await recordersStore.startRecorder(recorder.id)
    }
  }
}

async function stopAllStudioRecorders() {
  // Stop all recording recorders for this studio
  for (const recorder of studioRecorders.value) {
    if (recorder.state === 'recording') {
      await recordersStore.stopRecorder(recorder.id)
    }
  }
}

async function fetchFiles() {
  loadingFiles.value = true

  try {
    // Fetch files for current studio
    const myResponse = await fetch(`/api/assets/browse?studio_id=${route.params.id}`)
    if (myResponse.ok) {
      const data = await myResponse.json()
      myStudioFiles.value = data.files || []
    }

    // Fetch files for other studios
    for (const otherStudio of otherStudios.value) {
      const response = await fetch(`/api/assets/browse?studio_id=${otherStudio.id}`)
      if (response.ok) {
        const data = await response.json()
        otherStudioFiles.value[otherStudio.id] = data.files || []
      }
    }
  } catch (err) {
    console.error('Failed to fetch files:', err)
  } finally {
    loadingFiles.value = false
  }
}

// Watch for route changes
watch(() => route.params.id, async (newId) => {
  if (newId) {
    await fetchFiles()
  }
})

// File polling for real-time updates when recording
let filePollingInterval = null

function startFilePolling() {
  if (filePollingInterval) return

  // Poll every 3 seconds while recording
  filePollingInterval = setInterval(async () => {
    if (anyRecording.value) {
      // Only fetch current studio files (not other studios) for performance
      try {
        const response = await fetch(`/api/assets/browse?studio_id=${route.params.id}`)
        if (response.ok) {
          const data = await response.json()
          myStudioFiles.value = data.files || []
        }
      } catch (err) {
        console.error('Failed to refresh files:', err)
      }
    }
  }, 3000)
}

function stopFilePolling() {
  if (filePollingInterval) {
    clearInterval(filePollingInterval)
    filePollingInterval = null
  }
}

// Watch recording state to start/stop polling
watch(anyRecording, (isRecording) => {
  if (isRecording) {
    startFilePolling()
  } else {
    stopFilePolling()
    // Do one final refresh when recording stops
    fetchFiles()
  }
})

onMounted(async () => {
  await studiosStore.fetchStudios()
  await recordersStore.fetchRecorders()
  recordersStore.connectLevels()
  await fetchFiles()

  // Update selected studio in auth store
  authStore.setSelectedStudio(route.params.id)

  // Start polling if already recording
  if (anyRecording.value) {
    startFilePolling()
  }
})

onUnmounted(() => {
  // Clean up file polling
  stopFilePolling()
  // Don't disconnect levels as other components may use them
})
</script>

<style scoped>
.studio-header {
  transition: background-color 0.3s;
}

.recorder-item {
  border: 1px solid rgba(var(--v-border-color), 0.1);
}

.recorder-item.recording {
  animation: pulse 2s infinite;
  border-color: rgb(var(--v-theme-error));
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.8;
  }
}

.file-item {
  cursor: pointer;
  border-radius: 4px;
  transition: background-color 0.2s;
}

.file-item:hover {
  background-color: rgba(var(--v-theme-primary), 0.08);
}

.file-item.active {
  background-color: rgba(var(--v-theme-primary), 0.15);
}

.file-list {
  max-height: 400px;
  overflow-y: auto;
}
</style>
