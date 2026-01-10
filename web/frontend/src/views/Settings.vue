<template>
  <v-container fluid class="pa-6">
    <h1 class="text-h4 mb-6">Settings</h1>

    <v-row>
      <!-- Capture Settings -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-record-circle" class="mr-2" />
            Capture Settings
          </v-card-title>
          <v-card-text>
            <v-form>
              <v-select
                v-model="config.sourceType"
                label="Source Type"
                :items="[
                  { title: 'AES67 (Network)', value: 'aes67' },
                  { title: 'PipeWire (Local)', value: 'pipewire' }
                ]"
                item-title="title"
                item-value="value"
              />

              <v-row>
                <v-col cols="6">
                  <v-select
                    v-model="config.sampleRate"
                    label="Sample Rate"
                    :items="[44100, 48000, 96000]"
                    suffix="Hz"
                  />
                </v-col>
                <v-col cols="6">
                  <v-select
                    v-model="config.channels"
                    label="Channels"
                    :items="[1, 2, 4, 6, 8]"
                  />
                </v-col>
              </v-row>

              <v-select
                v-model="config.format"
                label="Output Format"
                :items="[
                  { title: 'WAV (Uncompressed)', value: 'wav' },
                  { title: 'Opus (Compressed)', value: 'opus' }
                ]"
                item-title="title"
                item-value="value"
              />

              <v-text-field
                v-if="config.format === 'opus'"
                v-model.number="config.bitrate"
                label="Opus Bitrate"
                type="number"
                suffix="bps"
              />
            </v-form>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Archive Settings -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-archive" class="mr-2" />
            Archive Settings
          </v-card-title>
          <v-card-text>
            <v-form>
              <v-text-field
                v-model="config.archiveRoot"
                label="Archive Directory"
                prepend-inner-icon="mdi-folder"
              />

              <v-select
                v-model="config.archiveLayout"
                label="Naming Layout"
                :items="layouts"
                item-title="title"
                item-value="value"
              />

              <v-text-field
                v-model.number="config.archivePeriod"
                label="Rotation Period"
                type="number"
                suffix="seconds"
                hint="3600 = 1 hour, 86400 = 24 hours"
                persistent-hint
              />

              <v-select
                v-model="config.archiveClock"
                label="Clock Source"
                :items="[
                  { title: 'Local Time', value: 'localtime' },
                  { title: 'UTC', value: 'utc' },
                  { title: 'PTP/TAI', value: 'ptp' }
                ]"
                item-title="title"
                item-value="value"
              />
            </v-form>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- PTP Settings -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-clock-outline" class="mr-2" />
            PTP Settings
          </v-card-title>
          <v-card-text>
            <v-form>
              <v-text-field
                v-model="config.ptpInterface"
                label="PTP Network Interface"
                placeholder="eth0"
                hint="Leave empty to disable PTP"
                persistent-hint
              />
            </v-form>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- System Info -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-information" class="mr-2" />
            System Information
          </v-card-title>
          <v-card-text>
            <v-list density="compact">
              <v-list-item>
                <v-list-item-title>Version</v-list-item-title>
                <v-list-item-subtitle>Audyn v1.0.0</v-list-item-subtitle>
              </v-list-item>
              <v-list-item>
                <v-list-item-title>Backend Status</v-list-item-title>
                <v-list-item-subtitle>
                  <v-chip color="success" size="small">Connected</v-chip>
                </v-list-item-subtitle>
              </v-list-item>
              <v-list-item>
                <v-list-item-title>Archive Stats</v-list-item-title>
                <v-list-item-subtitle>
                  {{ stats.totalFiles }} files, {{ stats.totalSize }}
                </v-list-item-subtitle>
              </v-list-item>
            </v-list>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Save Button -->
    <v-row class="mt-4">
      <v-col>
        <v-btn
          color="primary"
          size="large"
          prepend-icon="mdi-content-save"
          :loading="saving"
          @click="saveSettings"
        >
          Save Settings
        </v-btn>

        <v-btn
          variant="outlined"
          size="large"
          class="ml-4"
          @click="resetSettings"
        >
          Reset to Defaults
        </v-btn>
      </v-col>
    </v-row>
  </v-container>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useCaptureStore } from '@/stores/capture'

const captureStore = useCaptureStore()

// Local config copy
const config = ref({ ...captureStore.config })
const saving = ref(false)
const stats = ref({
  totalFiles: 0,
  totalSize: '0 GB'
})

// Layout options
const layouts = [
  { title: 'Flat (YYYY-MM-DD-HH.ext)', value: 'flat' },
  { title: 'Hierarchy (YYYY/MM/DD/HH/archive.ext)', value: 'hierarchy' },
  { title: 'Combo (YYYY/MM/DD/HH/YYYY-MM-DD-HH.ext)', value: 'combo' },
  { title: 'Daily Directory (YYYY-MM-DD/...)', value: 'dailydir' },
  { title: 'Accurate (with seconds)', value: 'accurate' }
]

// Methods
async function saveSettings() {
  saving.value = true

  try {
    // Update the store
    captureStore.config = { ...config.value }

    // Optionally save to backend
    await fetch('/api/control/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        source_type: config.value.sourceType,
        multicast_addr: config.value.multicastAddr,
        port: config.value.port,
        sample_rate: config.value.sampleRate,
        channels: config.value.channels,
        format: config.value.format,
        bitrate: config.value.bitrate,
        archive_root: config.value.archiveRoot,
        archive_layout: config.value.archiveLayout,
        archive_period: config.value.archivePeriod,
        archive_clock: config.value.archiveClock,
        ptp_interface: config.value.ptpInterface
      })
    })
  } finally {
    saving.value = false
  }
}

function resetSettings() {
  config.value = {
    sourceType: 'aes67',
    multicastAddr: '239.69.1.1',
    port: 5004,
    sampleRate: 48000,
    channels: 2,
    format: 'wav',
    bitrate: 128000,
    archiveRoot: '/var/lib/audyn',
    archiveLayout: 'dailydir',
    archivePeriod: 3600,
    archiveClock: 'localtime',
    ptpInterface: null
  }
}

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

onMounted(() => {
  config.value = { ...captureStore.config }
  fetchStats()
})
</script>
