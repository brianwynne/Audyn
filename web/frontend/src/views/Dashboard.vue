<template>
  <v-container fluid class="pa-6">
    <v-row>
      <!-- Audio Meters -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-tune-vertical" class="mr-2" />
            Audio Levels
          </v-card-title>
          <v-card-text>
            <AudioMeter
              v-for="(channel, index) in captureStore.levels"
              :key="index"
              :channel="channel"
              class="mb-4"
            />
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Status Panel -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-information" class="mr-2" />
            Capture Status
          </v-card-title>
          <v-card-text>
            <v-list density="compact">
              <v-list-item>
                <template v-slot:prepend>
                  <v-icon :color="statusColor" icon="mdi-circle" />
                </template>
                <v-list-item-title>Status</v-list-item-title>
                <v-list-item-subtitle>{{ captureStore.status.state.toUpperCase() }}</v-list-item-subtitle>
              </v-list-item>

              <v-list-item v-if="captureStore.isRecording">
                <template v-slot:prepend>
                  <v-icon icon="mdi-timer" />
                </template>
                <v-list-item-title>Duration</v-list-item-title>
                <v-list-item-subtitle>{{ formatDuration(recordingDuration) }}</v-list-item-subtitle>
              </v-list-item>

              <v-list-item v-if="captureStore.activeSource">
                <template v-slot:prepend>
                  <v-icon icon="mdi-access-point" />
                </template>
                <v-list-item-title>Source</v-list-item-title>
                <v-list-item-subtitle>{{ captureStore.activeSource.name }}</v-list-item-subtitle>
              </v-list-item>

              <v-list-item v-if="captureStore.status.currentFile">
                <template v-slot:prepend>
                  <v-icon icon="mdi-file-music" />
                </template>
                <v-list-item-title>Current File</v-list-item-title>
                <v-list-item-subtitle class="text-truncate">
                  {{ captureStore.status.currentFile }}
                </v-list-item-subtitle>
              </v-list-item>
            </v-list>

            <v-divider class="my-4" />

            <!-- Control Buttons -->
            <div class="d-flex gap-4">
              <v-btn
                v-if="captureStore.isStopped"
                color="success"
                size="large"
                prepend-icon="mdi-record"
                :loading="captureStore.loading"
                @click="captureStore.startCapture"
              >
                Start Recording
              </v-btn>

              <v-btn
                v-else
                color="error"
                size="large"
                prepend-icon="mdi-stop"
                :loading="captureStore.loading"
                @click="captureStore.stopCapture"
              >
                Stop Recording
              </v-btn>
            </div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Audio Preview -->
    <v-row class="mt-4">
      <v-col cols="12">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-play-circle" class="mr-2" />
            Audio Preview
          </v-card-title>
          <v-card-text>
            <AudioPlayer />
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Quick Stats -->
    <v-row class="mt-4">
      <v-col cols="12" sm="6" md="3">
        <v-card color="primary" variant="tonal">
          <v-card-text class="text-center">
            <div class="text-h4">{{ stats.totalFiles }}</div>
            <div class="text-caption">Total Files</div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="12" sm="6" md="3">
        <v-card color="secondary" variant="tonal">
          <v-card-text class="text-center">
            <div class="text-h4">{{ stats.totalSize }}</div>
            <div class="text-caption">Archive Size</div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="12" sm="6" md="3">
        <v-card color="info" variant="tonal">
          <v-card-text class="text-center">
            <div class="text-h4">{{ captureStore.sources.length }}</div>
            <div class="text-caption">Sources</div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="12" sm="6" md="3">
        <v-card :color="captureStore.isRecording ? 'recording' : 'default'" variant="tonal">
          <v-card-text class="text-center">
            <div class="text-h4">{{ uptime }}</div>
            <div class="text-caption">Uptime</div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </v-container>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useCaptureStore } from '@/stores/capture'
import AudioMeter from '@/components/AudioMeter.vue'
import AudioPlayer from '@/components/AudioPlayer.vue'

const captureStore = useCaptureStore()

// Recording duration timer
const recordingDuration = ref(0)
let durationInterval = null

// Stats
const stats = ref({
  totalFiles: 0,
  totalSize: '0 GB'
})

// Computed
const statusColor = computed(() => {
  switch (captureStore.status.state) {
    case 'recording': return 'error'
    case 'starting':
    case 'stopping': return 'warning'
    default: return 'grey'
  }
})

const uptime = computed(() => {
  // Placeholder - would come from backend
  return '24:00:00'
})

// Format duration
function formatDuration(seconds) {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = seconds % 60
  return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`
}

// Fetch archive stats
async function fetchStats() {
  try {
    const response = await fetch('/api/assets/stats')
    if (response.ok) {
      const data = await response.json()
      stats.value = {
        totalFiles: data.total_files,
        totalSize: data.total_size_human || '0 GB'
      }
    }
  } catch (err) {
    console.error('Failed to fetch stats:', err)
  }
}

// Start duration timer
function startDurationTimer() {
  if (durationInterval) return

  durationInterval = setInterval(() => {
    if (captureStore.isRecording) {
      recordingDuration.value++
    }
  }, 1000)
}

onMounted(() => {
  fetchStats()
  startDurationTimer()

  // Refresh status periodically
  const statusInterval = setInterval(() => {
    captureStore.fetchStatus()
  }, 5000)

  onUnmounted(() => {
    clearInterval(statusInterval)
    if (durationInterval) {
      clearInterval(durationInterval)
    }
  })
})
</script>
