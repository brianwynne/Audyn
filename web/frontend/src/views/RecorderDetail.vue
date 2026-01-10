<template>
  <v-container fluid class="pa-4">
    <!-- Header -->
    <div class="d-flex align-center mb-4">
      <v-btn
        icon
        variant="text"
        :to="{ name: 'recorders' }"
        class="mr-2"
      >
        <v-icon>mdi-arrow-left</v-icon>
      </v-btn>
      <h1 class="text-h4">{{ recorder?.name || 'Recorder' }}</h1>
      <v-spacer />

      <v-chip
        v-if="recorder"
        :color="getStateColor(recorder.state)"
        variant="elevated"
        class="mr-4"
      >
        <v-icon start :icon="getStateIcon(recorder.state)" />
        {{ recorder.state.toUpperCase() }}
      </v-chip>

      <v-btn
        v-if="recorder?.state !== 'recording'"
        color="success"
        variant="elevated"
        prepend-icon="mdi-play"
        :loading="loading"
        @click="start"
      >
        Start
      </v-btn>
      <v-btn
        v-else
        color="error"
        variant="elevated"
        prepend-icon="mdi-stop"
        :loading="loading"
        @click="stop"
      >
        Stop
      </v-btn>
    </div>

    <v-row v-if="recorder">
      <!-- Audio Meters -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon start>mdi-chart-bar</v-icon>
            Audio Levels
          </v-card-title>
          <v-card-text>
            <AudioMeter
              v-for="channel in recorder.levels"
              :key="channel.name"
              :channel="channel"
            />
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Recording Status -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon start>mdi-information</v-icon>
            Status
          </v-card-title>
          <v-card-text>
            <v-list density="compact">
              <v-list-item>
                <template #prepend>
                  <v-icon>mdi-broadcast</v-icon>
                </template>
                <v-list-item-title>Studio</v-list-item-title>
                <v-list-item-subtitle>
                  {{ studio?.name || 'Unassigned' }}
                </v-list-item-subtitle>
              </v-list-item>
              <v-list-item v-if="recorder.state === 'recording'">
                <template #prepend>
                  <v-icon>mdi-clock-outline</v-icon>
                </template>
                <v-list-item-title>Recording Duration</v-list-item-title>
                <v-list-item-subtitle>{{ recordingDuration }}</v-list-item-subtitle>
              </v-list-item>
              <v-list-item v-if="recorder.current_file">
                <template #prepend>
                  <v-icon>mdi-file-music</v-icon>
                </template>
                <v-list-item-title>Current File</v-list-item-title>
                <v-list-item-subtitle class="text-wrap">{{ recorder.current_file }}</v-list-item-subtitle>
              </v-list-item>
              <v-list-item>
                <template #prepend>
                  <v-icon>mdi-harddisk</v-icon>
                </template>
                <v-list-item-title>Bytes Written</v-list-item-title>
                <v-list-item-subtitle>{{ formatBytes(recorder.bytes_written || 0) }}</v-list-item-subtitle>
              </v-list-item>
            </v-list>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Configuration -->
      <v-col cols="12">
        <v-card>
          <v-card-title>
            <v-icon start>mdi-cog</v-icon>
            Configuration
          </v-card-title>
          <v-card-text>
            <v-form @submit.prevent="saveConfig">
              <v-row>
                <v-col cols="12" md="6">
                  <v-text-field
                    v-model="configForm.multicast_addr"
                    label="Multicast Address"
                    variant="outlined"
                    density="compact"
                    :disabled="recorder.state === 'recording'"
                  />
                </v-col>
                <v-col cols="12" md="6">
                  <v-text-field
                    v-model.number="configForm.port"
                    label="Port"
                    type="number"
                    variant="outlined"
                    density="compact"
                    :disabled="recorder.state === 'recording'"
                  />
                </v-col>
                <v-col cols="12" md="6">
                  <v-select
                    v-model="configForm.format"
                    :items="['wav', 'flac', 'mp3', 'ogg']"
                    label="Output Format"
                    variant="outlined"
                    density="compact"
                    :disabled="recorder.state === 'recording'"
                  />
                </v-col>
                <v-col cols="12" md="6">
                  <v-text-field
                    v-model="configForm.archive_root"
                    label="Archive Path"
                    variant="outlined"
                    density="compact"
                    :disabled="recorder.state === 'recording'"
                  />
                </v-col>
              </v-row>
              <v-btn
                type="submit"
                color="primary"
                :loading="loading"
                :disabled="recorder.state === 'recording'"
              >
                Save Configuration
              </v-btn>
            </v-form>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Errors -->
      <v-col v-if="recorder.errors?.length" cols="12">
        <v-card color="error" variant="tonal">
          <v-card-title>
            <v-icon start>mdi-alert-circle</v-icon>
            Errors
          </v-card-title>
          <v-card-text>
            <v-list density="compact" bg-color="transparent">
              <v-list-item v-for="(error, i) in recorder.errors" :key="i">
                {{ error }}
              </v-list-item>
            </v-list>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <v-alert v-else type="error" variant="tonal">
      Recorder not found
    </v-alert>
  </v-container>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'
import { useRoute } from 'vue-router'
import { useRecordersStore } from '@/stores/recorders'
import { useStudiosStore } from '@/stores/studios'
import AudioMeter from '@/components/AudioMeter.vue'

const route = useRoute()
const recordersStore = useRecordersStore()
const studiosStore = useStudiosStore()

const loading = ref(false)
const configForm = ref({
  multicast_addr: '',
  port: 5004,
  format: 'wav',
  archive_root: ''
})

const recorder = computed(() =>
  recordersStore.getRecorderById(parseInt(route.params.id))
)

const studio = computed(() => {
  if (!recorder.value?.studio_id) return null
  return studiosStore.getStudioById(recorder.value.studio_id)
})

const recordingDuration = computed(() => {
  if (!recorder.value?.start_time) return '00:00:00'
  const start = new Date(recorder.value.start_time)
  const now = new Date()
  const diff = Math.floor((now - start) / 1000)
  const hours = Math.floor(diff / 3600)
  const minutes = Math.floor((diff % 3600) / 60)
  const seconds = diff % 60
  return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`
})

function getStateColor(state) {
  switch (state) {
    case 'recording': return 'error'
    case 'paused': return 'warning'
    default: return 'grey'
  }
}

function getStateIcon(state) {
  switch (state) {
    case 'recording': return 'mdi-record-circle'
    case 'paused': return 'mdi-pause-circle'
    default: return 'mdi-stop-circle'
  }
}

function formatBytes(bytes) {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i]
}

async function start() {
  loading.value = true
  await recordersStore.startRecorder(recorder.value.id)
  loading.value = false
}

async function stop() {
  loading.value = true
  await recordersStore.stopRecorder(recorder.value.id)
  loading.value = false
}

async function saveConfig() {
  loading.value = true
  await recordersStore.updateRecorderConfig(recorder.value.id, configForm.value)
  loading.value = false
}

// Update form when recorder changes
watch(recorder, (rec) => {
  if (rec?.config) {
    configForm.value = {
      multicast_addr: rec.config.multicast_addr || '',
      port: rec.config.port || 5004,
      format: rec.config.format || 'wav',
      archive_root: rec.config.archive_root || ''
    }
  }
}, { immediate: true })

onMounted(async () => {
  await Promise.all([
    recordersStore.fetchRecorders(),
    studiosStore.fetchStudios()
  ])
  recordersStore.connectLevels()
})

onUnmounted(() => {
  recordersStore.disconnectLevels()
})
</script>
