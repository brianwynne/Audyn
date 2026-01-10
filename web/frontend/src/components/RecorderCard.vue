<template>
  <v-card :class="cardClass" elevation="2">
    <!-- Header with studio color -->
    <v-card-title
      class="d-flex align-center py-3"
      :style="headerStyle"
    >
      <v-icon :icon="stateIcon" :color="stateColor" class="mr-2" />
      <span>{{ recorder.name }}</span>
      <v-spacer />

      <!-- Recording status chip -->
      <v-chip
        :color="stateColor"
        size="small"
        variant="elevated"
      >
        {{ stateLabel }}
      </v-chip>
    </v-card-title>

    <v-divider />

    <v-card-text class="pa-4">
      <!-- Studio assignment -->
      <div v-if="studio" class="d-flex align-center mb-3">
        <v-icon icon="mdi-broadcast" size="small" class="mr-2" />
        <span class="text-subtitle-2">{{ studio.name }}</span>
        <v-chip
          :color="studio.color"
          size="x-small"
          class="ml-2"
        />
      </div>
      <div v-else class="d-flex align-center mb-3 text-medium-emphasis">
        <v-icon icon="mdi-broadcast-off" size="small" class="mr-2" />
        <span class="text-subtitle-2">Unassigned</span>
      </div>

      <!-- Audio meters -->
      <div class="meters-container">
        <CompactMeter
          v-for="channel in recorder.levels"
          :key="channel.name"
          :channel="channel"
        />
      </div>

      <!-- Recording info -->
      <div v-if="recorder.state === 'recording'" class="mt-3">
        <div class="d-flex align-center text-caption">
          <v-icon icon="mdi-clock-outline" size="small" class="mr-1" />
          <span>{{ recordingDuration }}</span>
          <v-spacer />
          <v-icon icon="mdi-harddisk" size="small" class="mr-1" />
          <span>{{ formatBytes(recorder.bytes_written || 0) }}</span>
        </div>
      </div>

      <!-- Source info -->
      <div class="mt-2 text-caption text-medium-emphasis">
        <v-icon icon="mdi-access-point" size="small" class="mr-1" />
        {{ recorder.config?.multicast_addr || 'Not configured' }}
      </div>
    </v-card-text>

    <!-- Actions (admin only) -->
    <v-card-actions v-if="authStore.isAdmin">
      <v-btn
        v-if="recorder.state !== 'recording'"
        color="success"
        variant="text"
        prepend-icon="mdi-play"
        :loading="loading"
        @click="start"
      >
        Start
      </v-btn>
      <v-btn
        v-else
        color="error"
        variant="text"
        prepend-icon="mdi-stop"
        :loading="loading"
        @click="stop"
      >
        Stop
      </v-btn>
      <v-spacer />
      <v-btn
        icon
        variant="text"
        size="small"
        :to="{ name: 'recorder', params: { id: recorder.id } }"
      >
        <v-icon>mdi-cog</v-icon>
      </v-btn>
    </v-card-actions>
  </v-card>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useAuthStore } from '@/stores/auth'
import { useRecordersStore } from '@/stores/recorders'
import CompactMeter from '@/components/CompactMeter.vue'

const props = defineProps({
  recorder: {
    type: Object,
    required: true
  },
  studio: {
    type: Object,
    default: null
  }
})

const authStore = useAuthStore()
const recordersStore = useRecordersStore()
const loading = ref(false)

const stateColor = computed(() => {
  switch (props.recorder.state) {
    case 'recording': return 'error'
    case 'paused': return 'warning'
    default: return 'grey'
  }
})

const stateIcon = computed(() => {
  switch (props.recorder.state) {
    case 'recording': return 'mdi-record-circle'
    case 'paused': return 'mdi-pause-circle'
    default: return 'mdi-stop-circle'
  }
})

const stateLabel = computed(() => {
  switch (props.recorder.state) {
    case 'recording': return 'REC'
    case 'paused': return 'PAUSED'
    default: return 'STOPPED'
  }
})

const cardClass = computed(() => ({
  'recorder-card': true,
  'recording': props.recorder.state === 'recording'
}))

const headerStyle = computed(() => {
  if (props.studio) {
    return {
      borderLeft: `4px solid ${props.studio.color}`
    }
  }
  return {}
})

const recordingDuration = computed(() => {
  if (!props.recorder.start_time) return '00:00:00'
  const start = new Date(props.recorder.start_time)
  const now = new Date()
  const diff = Math.floor((now - start) / 1000)
  const hours = Math.floor(diff / 3600)
  const minutes = Math.floor((diff % 3600) / 60)
  const seconds = diff % 60
  return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`
})

function formatBytes(bytes) {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i]
}

async function start() {
  loading.value = true
  await recordersStore.startRecorder(props.recorder.id)
  loading.value = false
}

async function stop() {
  loading.value = true
  await recordersStore.stopRecorder(props.recorder.id)
  loading.value = false
}
</script>

<style scoped>
.recorder-card {
  transition: all 0.3s ease;
}

.recorder-card.recording {
  box-shadow: 0 0 0 2px rgba(244, 67, 54, 0.5);
}

.meters-container {
  background: rgba(0, 0, 0, 0.2);
  border-radius: 4px;
  padding: 8px;
}
</style>
