<template>
  <v-dialog v-model="show" max-width="900" scrollable>
    <v-card>
      <v-card-title class="d-flex align-center">
        <v-icon icon="mdi-access-point-network" class="mr-2" />
        Discover AES67 Streams
        <v-spacer />
        <v-chip
          :color="discoveryStatus.running ? 'success' : 'error'"
          size="small"
          class="mr-2"
        >
          {{ discoveryStatus.running ? 'Listening' : 'Stopped' }}
        </v-chip>
        <v-btn
          icon
          size="small"
          variant="text"
          @click="show = false"
        >
          <v-icon>mdi-close</v-icon>
        </v-btn>
      </v-card-title>

      <v-card-text>
        <!-- Discovery Controls -->
        <v-row class="mb-4">
          <v-col>
            <v-btn
              v-if="!discoveryStatus.running"
              color="primary"
              prepend-icon="mdi-play"
              :loading="loading"
              @click="startDiscovery"
            >
              Start Discovery
            </v-btn>
            <v-btn
              v-else
              color="warning"
              prepend-icon="mdi-stop"
              :loading="loading"
              @click="stopDiscovery"
            >
              Stop Discovery
            </v-btn>
            <v-btn
              class="ml-2"
              variant="text"
              prepend-icon="mdi-refresh"
              :loading="loading"
              @click="refreshStreams"
            >
              Refresh
            </v-btn>
          </v-col>
          <v-col cols="auto" v-if="discoveryStatus.running">
            <v-chip size="small" class="mr-1">
              {{ discoveryStatus.active_streams }} streams
            </v-chip>
            <v-chip size="small" variant="outlined">
              {{ discoveryStatus.packets_received }} packets
            </v-chip>
          </v-col>
        </v-row>

        <!-- Stream List -->
        <v-table v-if="streams.length > 0" density="comfortable">
          <thead>
            <tr>
              <th>Name</th>
              <th>Address</th>
              <th>Format</th>
              <th>Channels</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="stream in streams" :key="stream.id">
              <td>
                <div class="font-weight-medium">{{ stream.session_name }}</div>
                <div class="text-caption text-medium-emphasis">{{ stream.origin_ip }}</div>
              </td>
              <td>
                <code>{{ stream.multicast_addr }}:{{ stream.port }}</code>
              </td>
              <td>
                {{ stream.encoding }} @ {{ stream.sample_rate }} Hz
                <div class="text-caption">{{ stream.ptime }} ms / {{ stream.samples_per_packet }} samples</div>
              </td>
              <td>
                <v-chip size="small">{{ stream.channels }} ch</v-chip>
                <div class="text-caption" v-if="stream.channel_labels.length">
                  {{ stream.channel_labels.slice(0, 4).join(', ') }}
                  <span v-if="stream.channel_labels.length > 4">...</span>
                </div>
              </td>
              <td>
                <v-btn
                  size="small"
                  color="primary"
                  variant="tonal"
                  prepend-icon="mdi-plus"
                  @click="openImportDialog(stream)"
                >
                  Add as Source
                </v-btn>
              </td>
            </tr>
          </tbody>
        </v-table>

        <v-alert
          v-else-if="discoveryStatus.running"
          type="info"
          variant="tonal"
          class="mt-4"
        >
          Listening for SAP announcements... Streams will appear here as they are discovered.
        </v-alert>

        <v-alert
          v-else
          type="warning"
          variant="tonal"
          class="mt-4"
        >
          Discovery is not running. Click "Start Discovery" to begin listening for AES67 streams.
        </v-alert>
      </v-card-text>
    </v-card>
  </v-dialog>

  <!-- Import Dialog with Channel Selection -->
  <v-dialog v-model="importDialog.show" max-width="500">
    <v-card>
      <v-card-title>Add Stream as Source</v-card-title>
      <v-card-text>
        <v-form ref="importForm" v-model="importDialog.valid">
          <v-text-field
            v-model="importDialog.name"
            label="Source Name"
            :placeholder="importDialog.stream?.session_name"
            hint="Leave blank to use stream name"
            persistent-hint
          />

          <v-alert
            v-if="importDialog.stream?.channels > 2"
            type="info"
            variant="tonal"
            class="my-4"
            density="compact"
          >
            This is a multi-channel stream ({{ importDialog.stream?.channels }} channels).
            Select which channels to record.
          </v-alert>

          <!-- Channel Selection -->
          <v-row v-if="importDialog.stream?.channels > 2" class="mt-2">
            <v-col cols="6">
              <v-select
                v-model="importDialog.outputChannels"
                label="Output Channels"
                :items="outputChannelOptions"
                hint="Number of channels to record"
                persistent-hint
              />
            </v-col>
            <v-col cols="6">
              <v-select
                v-model="importDialog.channelOffset"
                label="Start at Channel"
                :items="channelOffsetOptions"
                :disabled="channelOffsetOptions.length <= 1"
                hint="First channel to capture"
                persistent-hint
              />
            </v-col>
          </v-row>

          <!-- Channel Preview -->
          <v-card
            v-if="importDialog.stream?.channels > 2"
            variant="outlined"
            class="mt-4 pa-2"
          >
            <div class="text-caption text-medium-emphasis mb-2">Selected Channels:</div>
            <v-chip
              v-for="(label, idx) in selectedChannelLabels"
              :key="idx"
              size="small"
              class="mr-1 mb-1"
              color="primary"
            >
              {{ idx + 1 }}: {{ label }}
            </v-chip>
          </v-card>

          <v-textarea
            v-model="importDialog.description"
            label="Description"
            rows="2"
            class="mt-4"
          />
        </v-form>
      </v-card-text>
      <v-card-actions>
        <v-spacer />
        <v-btn variant="text" @click="importDialog.show = false">Cancel</v-btn>
        <v-btn
          color="primary"
          :loading="importDialog.loading"
          @click="importStream"
        >
          Add Source
        </v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>
</template>

<script setup>
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'

const props = defineProps({
  modelValue: Boolean
})

const emit = defineEmits(['update:modelValue', 'imported'])

const show = computed({
  get: () => props.modelValue,
  set: (val) => emit('update:modelValue', val)
})

const loading = ref(false)
const streams = ref([])
const discoveryStatus = ref({
  running: false,
  multicast_addr: '239.255.255.255',
  packets_received: 0,
  active_streams: 0
})

const importDialog = ref({
  show: false,
  stream: null,
  name: '',
  description: '',
  outputChannels: 2,
  channelOffset: 0,
  valid: true,
  loading: false
})

let refreshInterval = null

// Computed options for channel selection
const outputChannelOptions = computed(() => {
  const streamCh = importDialog.value.stream?.channels || 2
  const options = []
  for (let ch = 1; ch <= Math.min(streamCh, 8); ch++) {
    if (ch === 1) options.push({ title: '1 (Mono)', value: 1 })
    else if (ch === 2) options.push({ title: '2 (Stereo)', value: 2 })
    else options.push({ title: String(ch), value: ch })
  }
  return options
})

const channelOffsetOptions = computed(() => {
  const streamCh = importDialog.value.stream?.channels || 2
  const outCh = importDialog.value.outputChannels || 2
  const maxOffset = streamCh - outCh
  const labels = importDialog.value.stream?.channel_labels || []

  const options = []
  for (let off = 0; off <= maxOffset; off++) {
    const label = labels[off] || `Ch ${off + 1}`
    options.push({
      title: `${off} (${label})`,
      value: off
    })
  }
  return options
})

const selectedChannelLabels = computed(() => {
  const stream = importDialog.value.stream
  if (!stream) return []

  const labels = stream.channel_labels.length
    ? stream.channel_labels
    : Array.from({ length: stream.channels }, (_, i) => `Ch ${i + 1}`)

  const offset = importDialog.value.channelOffset
  const count = importDialog.value.outputChannels

  return labels.slice(offset, offset + count)
})

// Watch for channel offset to stay valid
watch(() => importDialog.value.outputChannels, (newVal) => {
  const streamCh = importDialog.value.stream?.channels || 2
  const maxOffset = streamCh - newVal
  if (importDialog.value.channelOffset > maxOffset) {
    importDialog.value.channelOffset = maxOffset
  }
})

async function fetchStatus() {
  try {
    const res = await fetch('/api/discovery/status')
    if (res.ok) {
      discoveryStatus.value = await res.json()
    }
  } catch (err) {
    console.error('Failed to fetch discovery status:', err)
  }
}

async function fetchStreams() {
  try {
    const res = await fetch('/api/discovery/streams')
    if (res.ok) {
      streams.value = await res.json()
    }
  } catch (err) {
    console.error('Failed to fetch streams:', err)
  }
}

async function refreshStreams() {
  loading.value = true
  await fetchStatus()
  await fetchStreams()
  loading.value = false
}

async function startDiscovery() {
  loading.value = true
  try {
    const res = await fetch('/api/discovery/start', { method: 'POST' })
    if (res.ok) {
      await refreshStreams()
    }
  } catch (err) {
    console.error('Failed to start discovery:', err)
  }
  loading.value = false
}

async function stopDiscovery() {
  loading.value = true
  try {
    const res = await fetch('/api/discovery/stop', { method: 'POST' })
    if (res.ok) {
      await fetchStatus()
    }
  } catch (err) {
    console.error('Failed to stop discovery:', err)
  }
  loading.value = false
}

function openImportDialog(stream) {
  importDialog.value = {
    show: true,
    stream,
    name: '',
    description: '',
    outputChannels: Math.min(2, stream.channels),
    channelOffset: 0,
    valid: true,
    loading: false
  }
}

async function importStream() {
  importDialog.value.loading = true

  try {
    const stream = importDialog.value.stream
    const payload = {
      stream_id: stream.id,
      name: importDialog.value.name || null,
      description: importDialog.value.description || null,
      channels: importDialog.value.outputChannels,
      channel_offset: importDialog.value.channelOffset,
      stream_channels: stream.channels > importDialog.value.outputChannels ? stream.channels : null
    }

    const res = await fetch('/api/sources/from-discovery', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })

    if (res.ok) {
      const newSource = await res.json()
      emit('imported', newSource)
      importDialog.value.show = false
    } else {
      const error = await res.json()
      console.error('Import failed:', error)
    }
  } catch (err) {
    console.error('Failed to import stream:', err)
  }

  importDialog.value.loading = false
}

// Auto-refresh when dialog is open
watch(show, (isOpen) => {
  if (isOpen) {
    refreshStreams()
    refreshInterval = setInterval(refreshStreams, 5000)
  } else if (refreshInterval) {
    clearInterval(refreshInterval)
    refreshInterval = null
  }
})

onMounted(() => {
  if (show.value) {
    refreshStreams()
  }
})

onUnmounted(() => {
  if (refreshInterval) {
    clearInterval(refreshInterval)
  }
})
</script>
