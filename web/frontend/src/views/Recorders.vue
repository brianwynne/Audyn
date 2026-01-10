<template>
  <v-container fluid class="pa-4">
    <!-- Header -->
    <div class="d-flex align-center mb-4">
      <h1 class="text-h4">Recorders</h1>
      <v-spacer />

      <!-- Active recorder count selector -->
      <v-select
        v-model="recordersStore.activeCount"
        :items="recorderCountOptions"
        label="Active Recorders"
        density="compact"
        variant="outlined"
        hide-details
        style="max-width: 180px"
        class="mr-4"
        @update:model-value="updateActiveCount"
      />

      <v-btn
        color="success"
        variant="elevated"
        prepend-icon="mdi-play"
        class="mr-2"
        :loading="recordersStore.loading"
        @click="startAll"
      >
        Start All
      </v-btn>
      <v-btn
        color="error"
        variant="elevated"
        prepend-icon="mdi-stop"
        :loading="recordersStore.loading"
        @click="stopAll"
      >
        Stop All
      </v-btn>
    </div>

    <!-- Recorders table -->
    <v-card>
      <v-data-table
        :headers="headers"
        :items="recordersStore.activeRecorders"
        item-key="id"
        class="elevation-1"
      >
        <!-- Status column -->
        <template #item.state="{ item }">
          <v-chip
            :color="getStateColor(item.state)"
            size="small"
            variant="elevated"
          >
            <v-icon start :icon="getStateIcon(item.state)" size="small" />
            {{ item.state.toUpperCase() }}
          </v-chip>
        </template>

        <!-- Studio column -->
        <template #item.studio_id="{ item }">
          <div v-if="getStudio(item.studio_id)" class="d-flex align-center">
            <v-chip
              :color="getStudio(item.studio_id).color"
              size="x-small"
              class="mr-2"
            />
            {{ getStudio(item.studio_id).name }}
          </div>
          <span v-else class="text-medium-emphasis">Unassigned</span>
        </template>

        <!-- Source column -->
        <template #item.config="{ item }">
          <span class="font-monospace">
            {{ item.config?.multicast_addr }}:{{ item.config?.port }}
          </span>
        </template>

        <!-- Levels column -->
        <template #item.levels="{ item }">
          <div class="d-flex align-center" style="min-width: 150px">
            <MiniMeter :levels="item.levels" />
          </div>
        </template>

        <!-- Actions column -->
        <template #item.actions="{ item }">
          <v-btn
            v-if="item.state !== 'recording'"
            icon
            variant="text"
            color="success"
            size="small"
            :loading="recordersStore.loading"
            @click="startRecorder(item.id)"
          >
            <v-icon>mdi-play</v-icon>
          </v-btn>
          <v-btn
            v-else
            icon
            variant="text"
            color="error"
            size="small"
            :loading="recordersStore.loading"
            @click="stopRecorder(item.id)"
          >
            <v-icon>mdi-stop</v-icon>
          </v-btn>
          <v-btn
            icon
            variant="text"
            size="small"
            :to="{ name: 'recorder', params: { id: item.id } }"
          >
            <v-icon>mdi-cog</v-icon>
          </v-btn>
        </template>
      </v-data-table>
    </v-card>

    <!-- Quick stats -->
    <v-row class="mt-4">
      <v-col cols="12" md="3">
        <v-card variant="tonal" color="primary">
          <v-card-text class="text-center">
            <div class="text-h4">{{ recordersStore.activeCount }}</div>
            <div class="text-subtitle-2">Active Recorders</div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="12" md="3">
        <v-card variant="tonal" color="success">
          <v-card-text class="text-center">
            <div class="text-h4">{{ recordersStore.recordingRecorders.length }}</div>
            <div class="text-subtitle-2">Recording</div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="12" md="3">
        <v-card variant="tonal" color="warning">
          <v-card-text class="text-center">
            <div class="text-h4">{{ stoppedCount }}</div>
            <div class="text-subtitle-2">Stopped</div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="12" md="3">
        <v-card variant="tonal" color="secondary">
          <v-card-text class="text-center">
            <div class="text-h4">{{ assignedCount }}</div>
            <div class="text-subtitle-2">Assigned to Studios</div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </v-container>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRecordersStore } from '@/stores/recorders'
import { useStudiosStore } from '@/stores/studios'
import MiniMeter from '@/components/MiniMeter.vue'

const recordersStore = useRecordersStore()
const studiosStore = useStudiosStore()

const headers = [
  { title: 'ID', key: 'id', width: '60px' },
  { title: 'Name', key: 'name' },
  { title: 'Status', key: 'state', width: '120px' },
  { title: 'Studio', key: 'studio_id' },
  { title: 'Source', key: 'config' },
  { title: 'Levels', key: 'levels', sortable: false },
  { title: 'Actions', key: 'actions', sortable: false, width: '100px' }
]

const recorderCountOptions = computed(() =>
  Array.from({ length: recordersStore.maxRecorders }, (_, i) => ({
    title: `${i + 1} Recorder${i > 0 ? 's' : ''}`,
    value: i + 1
  }))
)

const stoppedCount = computed(() =>
  recordersStore.activeRecorders.filter(r => r.state === 'stopped').length
)

const assignedCount = computed(() =>
  recordersStore.activeRecorders.filter(r => r.studio_id).length
)

function getStudio(studioId) {
  if (!studioId) return null
  return studiosStore.getStudioById(studioId)
}

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

async function updateActiveCount(count) {
  await recordersStore.setActiveCount(count)
}

async function startRecorder(id) {
  await recordersStore.startRecorder(id)
}

async function stopRecorder(id) {
  await recordersStore.stopRecorder(id)
}

async function startAll() {
  await recordersStore.startAllRecorders()
}

async function stopAll() {
  await recordersStore.stopAllRecorders()
}

onMounted(async () => {
  await Promise.all([
    recordersStore.fetchRecorders(),
    recordersStore.fetchActiveCount(),
    studiosStore.fetchStudios()
  ])
  recordersStore.connectLevels()
})

onUnmounted(() => {
  recordersStore.disconnectLevels()
})
</script>
