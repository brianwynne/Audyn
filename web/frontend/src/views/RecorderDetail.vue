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
                <!-- Source Type Selection -->
                <v-col cols="12" md="6">
                  <v-select
                    v-model="configForm.source_type"
                    :items="sourceTypes"
                    item-title="title"
                    item-value="value"
                    label="Source Type"
                    variant="outlined"
                    density="compact"
                    :disabled="recorder.state === 'recording'"
                  />
                </v-col>

                <!-- AES67 Source Selection (when source_type=aes67) -->
                <v-col v-if="configForm.source_type === 'aes67'" cols="12" md="6">
                  <v-select
                    v-model="configForm.source_id"
                    :items="aesSourceOptions"
                    item-title="title"
                    item-value="value"
                    label="AES67 Source"
                    variant="outlined"
                    density="compact"
                    :disabled="recorder.state === 'recording'"
                    :hint="aesSourceOptions.length === 0 ? 'No AES67 sources configured' : 'Select from configured sources'"
                    persistent-hint
                  />
                </v-col>

                <!-- PipeWire Source Selection (when source_type=pipewire) -->
                <v-col v-if="configForm.source_type === 'pipewire'" cols="12" md="6">
                  <v-select
                    v-model="configForm.pipewire_target"
                    :items="pipewireSourceOptions"
                    item-title="title"
                    item-value="value"
                    label="PipeWire Source"
                    variant="outlined"
                    density="compact"
                    :disabled="recorder.state === 'recording'"
                    :hint="pipewireSourceOptions.length === 0 ? 'No PipeWire sources detected' : 'Select an audio input device'"
                    persistent-hint
                  />
                </v-col>
                <v-col cols="12" md="6">
                  <v-select
                    v-model="configForm.format"
                    :items="formatOptions"
                    item-title="title"
                    item-value="value"
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

                <!-- Format-specific options for non-WAV formats -->
                <template v-if="configForm.format !== 'wav'">
                  <v-col cols="12">
                    <v-divider class="my-2" />
                    <div class="text-subtitle-2 text-medium-emphasis mb-2">
                      {{ formatOptions.find(f => f.value === configForm.format)?.title }} Options
                    </div>
                  </v-col>

                  <v-col cols="12" md="3">
                    <v-select
                      v-model.number="configForm.sample_rate"
                      :items="sampleRateOptions"
                      label="Sample Rate"
                      variant="outlined"
                      density="compact"
                      suffix="Hz"
                      :disabled="recorder.state === 'recording'"
                    />
                  </v-col>

                  <v-col cols="12" md="3">
                    <v-select
                      v-model.number="configForm.bit_depth"
                      :items="bitDepthOptions"
                      label="Bit Depth"
                      variant="outlined"
                      density="compact"
                      suffix="bit"
                      :disabled="recorder.state === 'recording'"
                    />
                  </v-col>

                  <v-col cols="12" md="3">
                    <v-select
                      v-model.number="configForm.channels"
                      :items="channelOptions"
                      label="Channels"
                      variant="outlined"
                      density="compact"
                      :disabled="recorder.state === 'recording'"
                    />
                  </v-col>

                  <!-- Bitrate for MP3 and Opus only -->
                  <v-col v-if="configForm.format === 'mp3' || configForm.format === 'opus'" cols="12" md="3">
                    <v-select
                      v-model.number="configForm.bitrate"
                      :items="bitrateOptions"
                      item-title="title"
                      item-value="value"
                      label="Bitrate"
                      variant="outlined"
                      density="compact"
                      :disabled="recorder.state === 'recording'"
                    />
                  </v-col>

                  <!-- Compression level for FLAC only -->
                  <v-col v-if="configForm.format === 'flac'" cols="12" md="3">
                    <v-select
                      v-model.number="configForm.flac_compression"
                      :items="flacCompressionOptions"
                      label="Compression Level"
                      variant="outlined"
                      density="compact"
                      hint="0 = fastest, 8 = smallest"
                      persistent-hint
                      :disabled="recorder.state === 'recording'"
                    />
                  </v-col>
                </template>
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
const availableAesSources = ref([])
const availablePipewireSources = ref([])

// Source type options
const sourceTypes = [
  { title: 'AES67 (Network)', value: 'aes67' },
  { title: 'PipeWire (Local)', value: 'pipewire' }
]

// AES67 source options for dropdown
const aesSourceOptions = computed(() =>
  availableAesSources.value.map(s => ({
    title: s.name,
    value: s.id
  }))
)

// PipeWire source options for dropdown
const pipewireSourceOptions = computed(() =>
  availablePipewireSources.value.map(s => ({
    title: s.name,
    value: s.node_name || s.id
  }))
)

// Format options
const formatOptions = [
  { title: 'WAV (Uncompressed)', value: 'wav' },
  { title: 'FLAC (Lossless)', value: 'flac' },
  { title: 'MP3', value: 'mp3' },
  { title: 'Ogg OPUS', value: 'opus' }
]

// Sample rate options
const sampleRateOptions = [44100, 48000, 96000]

// Bit depth options
const bitDepthOptions = [16, 24, 32]

// Channel options
const channelOptions = [1, 2, 4, 6, 8]

// Bitrate options (in bps) - for MP3 and Opus only
const bitrateOptions = [
  { title: '64 kbps', value: 64000 },
  { title: '96 kbps', value: 96000 },
  { title: '128 kbps', value: 128000 },
  { title: '192 kbps', value: 192000 },
  { title: '256 kbps', value: 256000 },
  { title: '320 kbps', value: 320000 }
]

// FLAC compression level options (0-8, higher = more compression)
const flacCompressionOptions = [0, 1, 2, 3, 4, 5, 6, 7, 8]

const configForm = ref({
  source_type: 'aes67',
  source_id: null,
  multicast_addr: '',
  port: 5004,
  pipewire_target: '',
  format: 'wav',
  sample_rate: 48000,
  bit_depth: 24,
  channels: 2,
  bitrate: 128000,
  flac_compression: 5,
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
      source_type: rec.config.source_type || 'aes67',
      source_id: rec.config.source_id || null,
      multicast_addr: rec.config.multicast_addr || '',
      port: rec.config.port || 5004,
      pipewire_target: rec.config.pipewire_target || '',
      format: rec.config.format || 'wav',
      sample_rate: rec.config.sample_rate || 48000,
      bit_depth: rec.config.bit_depth || 24,
      channels: rec.config.channels || 2,
      bitrate: rec.config.bitrate || 128000,
      flac_compression: rec.config.flac_compression || 5,
      archive_root: rec.config.archive_root || ''
    }
  }
}, { immediate: true })

// Fetch available AES67 sources
async function fetchAesSources() {
  try {
    const response = await fetch('/api/sources/')
    if (response.ok) {
      availableAesSources.value = await response.json()
    }
  } catch (err) {
    console.error('Failed to fetch AES67 sources:', err)
  }
}

// Fetch available PipeWire sources
async function fetchPipewireSources() {
  try {
    const response = await fetch('/api/sources/pipewire')
    if (response.ok) {
      availablePipewireSources.value = await response.json()
    }
  } catch (err) {
    console.error('Failed to fetch PipeWire sources:', err)
  }
}

onMounted(async () => {
  await Promise.all([
    recordersStore.fetchRecorders(),
    studiosStore.fetchStudios(),
    fetchAesSources(),
    fetchPipewireSources()
  ])
  recordersStore.connectLevels()
})

onUnmounted(() => {
  recordersStore.disconnectLevels()
})
</script>
